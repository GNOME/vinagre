/*
 * vinagre-main.c
 * This file is part of vinagre
 *
 * Copyright (C) 2007,2008,2009 - Jonh Wendell <wendell@bani.com.br>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <locale.h>

#include "vinagre-window.h"
#include "vinagre-prefs.h"
#include "vinagre-cache-prefs.h"
#include "vinagre-debug.h"
#include "vinagre-ssh.h"
#include "vinagre-options.h"
#include "vinagre-plugins-engine.h"

#ifdef VINAGRE_HAVE_TELEPATHY_GLIB
#include "vinagre-tubes-manager.h"
#endif

#ifdef VINAGRE_HAVE_AVAHI
#include "vinagre-mdns.h"
#endif

static gboolean startup_called = FALSE;
static GtkWindow *window = NULL;

#ifdef VINAGRE_HAVE_TELEPATHY_GLIB
static VinagreTubesManager *vinagre_tubes_manager = NULL;
#endif

static void
app_init (GtkApplication *app)
{
  vinagre_debug_init ();
  vinagre_debug_message (DEBUG_APP, "Startup");

  vinagre_cache_prefs_init ();

  window = GTK_WINDOW (vinagre_window_new ());
  gtk_window_set_application (window, app);
  gtk_widget_show (GTK_WIDGET (window));

#ifdef VINAGRE_HAVE_TELEPATHY_GLIB
  vinagre_tubes_manager = vinagre_tubes_manager_new (VINAGRE_WINDOW (window));
#endif

  /* fake call, just to ensure this symbol will be present at vinagre.so */
  vinagre_ssh_connect (NULL, NULL, -1, NULL, NULL, NULL, NULL, NULL);
}

static void
app_startup (GApplication *app,
	     void         *user_data)
{
  /* We don't do anything here, as we need to know the options
   * when we set everything up.
   * Note that this will break D-Bus activation of the application */
  startup_called = TRUE;
}

static GOptionContext *
get_option_context (void)
{
  GOptionContext *context;
  VinagreProtocol *extension;
  GHashTable *extensions;
  GHashTableIter iter;

  /* Setup command line options */
  context = g_option_context_new (_("— Remote Desktop Viewer"));
  g_option_context_set_help_enabled (context, FALSE);
  g_option_context_add_main_entries (context, all_options, GETTEXT_PACKAGE);
  g_option_context_set_translation_domain (context, GETTEXT_PACKAGE);
  g_option_context_add_group (context, gtk_get_option_group (TRUE));

  extensions = vinagre_plugins_engine_get_plugins_by_protocol (vinagre_plugins_engine_get_default ());

  g_hash_table_iter_init (&iter, extensions);
  while (g_hash_table_iter_next (&iter, NULL, (gpointer *)&extension))
    {
      GSList *groups, *l;

      groups = vinagre_protocol_get_context_groups (extension);
      for (l = groups; l; l = l->next)
	g_option_context_add_group (context, (GOptionGroup *)l->data);
      g_slist_free (groups);
    }

  return context;
}

static int
app_command_line (GApplication            *app,
		  GApplicationCommandLine *command_line,
		  void                    *user_data)
{
  GError *error = NULL;
  int argc;
  char **argv;
  GOptionContext *context;
  int ret;

  ret = 0;

  context = get_option_context ();

  argv = g_application_command_line_get_arguments (command_line, &argc);

  optionstate.help = FALSE;

  if (!g_option_context_parse (context, &argc, &argv, &error))
    {
      g_application_command_line_printerr (command_line, "%s\n", error->message);
      g_error_free (error);
      ret = 1;
      goto out;
    }
  else if (optionstate.help)
    {
      gchar *text;
      text = g_option_context_get_help (context, FALSE, NULL);
      g_application_command_line_print (command_line, "%s", text);
      g_free (text);
      goto out;
    }

  /* Don't create another window if we're remote.
   * We can't use g_application_get_is_remote() because it's not registered yet */
  if (startup_called != FALSE)
    {
      app_init (GTK_APPLICATION (app));
      startup_called = FALSE;
    }
  else
    {
      gtk_window_present_with_time (window, GDK_CURRENT_TIME);
    }

  vinagre_options_process_command_line (GTK_APPLICATION (app), window, &optionstate);

out:
  g_strfreev (argv);

  g_option_context_free (context);

  return ret;
}

int main (int argc, char **argv) {
  GtkApplication *app;
  int res;

  /* i18n */
  setlocale (LC_ALL, "");
  bindtextdomain (GETTEXT_PACKAGE, PACKAGE_LOCALE_DIR);
  bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
  textdomain (GETTEXT_PACKAGE);

  g_set_prgname (PACKAGE_TARNAME);
  g_type_init ();
  g_set_application_name (_("Remote Desktop Viewer"));
  optionstate.new_window = FALSE;

  app = gtk_application_new ("org.gnome.vinagre", G_APPLICATION_HANDLES_COMMAND_LINE);
  /* https://bugzilla.gnome.org/show_bug.cgi?id=634990 */
  /* g_application_set_option_context (G_APPLICATION (app), context); */
  g_signal_connect (app,
		    "command-line",
		    G_CALLBACK (app_command_line),
		    NULL);
  g_signal_connect (app,
		    "startup",
		    G_CALLBACK (app_startup),
		    NULL);
  res = g_application_run (G_APPLICATION (app), argc, argv);

  if (res == 0)
    {
#ifdef VINAGRE_HAVE_TELEPATHY_GLIB
      if (vinagre_tubes_manager != NULL)
	g_object_unref (vinagre_tubes_manager);
#endif

      g_object_unref (vinagre_bookmarks_get_default ());
      g_object_unref (vinagre_prefs_get_default ());
      vinagre_cache_prefs_finalize ();

#ifdef VINAGRE_HAVE_AVAHI
	g_object_unref (vinagre_mdns_get_default ());
#endif
    }

  g_object_unref (app);

  return res;
}
/* vim: set ts=8: */
