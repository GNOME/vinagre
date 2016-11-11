/*
 * vinagre-spice-plugin.c
 * This file is part of vinagre
 *
 * Copyright (C) 2011 Red Hat, Inc.
 * Author: Marc-Andre Lureau <marcandre.lureau@redhat.com>
 *
 * vinagre-spice-plugin.c is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * vinagre-spice-plugin.c is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <glib/gi18n-lib.h>
#include <gmodule.h>

#include <vinagre/vinagre-cache-prefs.h>
#include <vinagre/vinagre-protocol.h>

#include "vinagre-spice-plugin.h"
#include "vinagre-spice-connection.h"
#include "vinagre-spice-tab.h"

void spice_register_types (void);
static void vinagre_spice_protocol_iface_init (VinagreProtocolInterface *iface);

G_DEFINE_TYPE_EXTENDED (VinagreSpicePlugin,
			vinagre_spice_plugin,
			VINAGRE_TYPE_STATIC_EXTENSION,
			0,
			G_IMPLEMENT_INTERFACE (VINAGRE_TYPE_PROTOCOL,
					       vinagre_spice_protocol_iface_init))

static const gchar *
impl_get_protocol (VinagreProtocol *plugin)
{
  return "spice";
}

static const gchar *
impl_get_mdns_service (VinagreProtocol *plugin)
{
  return "_spice._tcp";
}

static VinagreConnection *
impl_new_connection (VinagreProtocol *plugin)
{
  VinagreConnection *conn;

  conn = vinagre_spice_connection_new ();

  return conn;
}

static VinagreConnection *
impl_new_connection_from_file (VinagreProtocol *plugin,
			       const gchar   *data,
			       gboolean	      use_bookmarks,
			       gchar	    **error_msg)
{
  GKeyFile	    *file;
  GError	    *error;
  gboolean	     loaded;
  gchar		    *host, *actual_host, *protocol;
  gint		     port;
  VinagreConnection *conn;

  *error_msg = NULL;
  conn = NULL;
  host = NULL;
  protocol = NULL;
  error = NULL;

  file = g_key_file_new ();
  loaded = g_key_file_load_from_data (file,
				      data,
				      -1,
				      0,
				      &error);
  if (!loaded)
    {
      if (error)
	{
	  *error_msg = g_strdup (error->message);
	  g_error_free (error);
	}
      else
	*error_msg = g_strdup (_("Could not parse the file."));

      goto the_end;
    }
  if (!g_key_file_has_group (file, "connection"))
    {
      /* Translators: Do not translate "connection". It's the name of a group in the .spice (.ini like) file. */
      *error_msg = g_strdup (_("The file is not a Spice one: Missing the group “connection”."));
      goto the_end;
    }

  if (!g_key_file_has_key (file, "connection", "host", NULL))
    {
      /* Translators: Do not translate "host". It's the name of a key in the .spice (.ini like) file. */
      *error_msg = g_strdup (_("The file is not a Spice one: Missing the key “host”."));
      goto the_end;
    }

  host = g_key_file_get_string (file, "connection", "host", NULL);
  port = g_key_file_get_integer (file, "connection", "port", NULL);
  if (!port)
    {
      if (!vinagre_connection_split_string (host, "spice", &protocol, &actual_host, &port, error_msg))
	goto the_end;

      g_free (host);
      host = actual_host;
    }

  if (use_bookmarks)
    conn = vinagre_bookmarks_exists (vinagre_bookmarks_get_default (), "spice", host, port);
  if (!conn)
    {
      gchar *s_value;

      conn = vinagre_spice_connection_new ();
      vinagre_connection_set_host (conn, host);
      vinagre_connection_set_port (conn, port);

      s_value = g_key_file_get_string  (file, "connection", "password", NULL);
      vinagre_connection_set_password (conn, s_value);
      g_free (s_value);
    }

 the_end:

  g_free (host);
  g_free (protocol);
  g_key_file_free (file);
  return conn;

}

static gboolean
impl_recognize_file (VinagreProtocol *plugin, GFile *file)
{
  gboolean result = FALSE;
  gchar *filename = g_file_get_basename (file);

  if (filename)
    {
      result = g_str_has_suffix (filename, ".spice");
      g_free (filename);
    }

  return result;
}

static gchar **
impl_get_public_description (VinagreProtocol *plugin)
{
  gchar **result = g_new (gchar *, 3);

  result[0] = g_strdup (_("SPICE"));
  /* Translators: This is a description of the SPICE protocol. It appears at Connect dialog. */
  result[1] = g_strdup (_("Access Spice desktop server"));
  result[2] = NULL;

  return result;
}

static gint
impl_get_default_port (VinagreProtocol *plugin)
{
  return 5900;
}

static void
vinagre_spice_plugin_init (VinagreSpicePlugin *plugin)
{
}

static GtkWidget *
impl_new_tab (VinagreProtocol *plugin,
	      VinagreConnection *conn,
	      VinagreWindow	*window)
{
  return vinagre_spice_tab_new (conn, window);
}

static void
ssh_tunnel_check_toggled_cb (GtkToggleButton *button, GObject *box)
{
  gboolean active = gtk_toggle_button_get_active (button);
  GtkWidget *ssh_host_entry = g_object_get_data (G_OBJECT (box), "ssh_host");

  gtk_widget_set_sensitive (ssh_host_entry, active);

  if (active)
    gtk_widget_grab_focus (ssh_host_entry);
  else
    gtk_entry_set_text (GTK_ENTRY (ssh_host_entry), "");
}

static GtkWidget *
impl_get_connect_widget (VinagreProtocol *plugin, VinagreConnection *conn)
{
  GtkWidget *box, *label, *check, *p_entry, *box2, *ssh_host_entry, *grid;
  gchar	    *str, *ssh_host;
  gboolean  has_conn = VINAGRE_IS_SPICE_CONNECTION (conn);

  box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);

  str = g_strdup_printf ("<b>%s</b>", _("SPICE Options"));
  label = gtk_label_new (str);
  g_free (str);
  gtk_label_set_use_markup (GTK_LABEL (label), TRUE);
  gtk_misc_set_alignment (GTK_MISC (label), 0, 0.5);
  gtk_misc_set_padding (GTK_MISC (label), 0, 6);
  gtk_box_pack_start (GTK_BOX (box), label, FALSE, FALSE, 0);

  grid = gtk_grid_new ();
  label = gtk_label_new ("  ");
  gtk_grid_attach (GTK_GRID (grid), label, 0, 0, 1, 1);

  /* View only check button - not fully ready on spice-gtk side */
  check = gtk_check_button_new_with_mnemonic (_("_View only"));
  g_object_set_data (G_OBJECT (box), "view_only", check);
  /* gtk_table_attach_defaults (table, check, 1, 2, 0, 1); */
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (check),
				has_conn ? vinagre_spice_connection_get_view_only (VINAGRE_SPICE_CONNECTION (conn))
				: vinagre_cache_prefs_get_boolean ("spice-connection", "view-only", FALSE));

  /* Resize guest check button */
  check = gtk_check_button_new_with_mnemonic (_("_Resize guest"));
  g_object_set_data (G_OBJECT (box), "resize_guest", check);
  gtk_grid_attach (GTK_GRID (grid), check, 1, 0, 1, 1);
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (check),
				has_conn ? vinagre_spice_connection_get_resize_guest (VINAGRE_SPICE_CONNECTION (conn))
				: vinagre_cache_prefs_get_boolean ("spice-connection", "resize-guest", TRUE));

  /* Clipboard sharing check button */
  check = gtk_check_button_new_with_mnemonic (_("_Share clipboard"));
  g_object_set_data (G_OBJECT (box), "auto_clipboard", check);
  gtk_grid_attach (GTK_GRID (grid), check, 1, 1, 1, 1);
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (check),
				has_conn ? vinagre_spice_connection_get_auto_clipboard (VINAGRE_SPICE_CONNECTION (conn))
				: vinagre_cache_prefs_get_boolean ("spice-connection", "auto-clipboard", TRUE));

  /* Scaling check button */
  check = gtk_check_button_new_with_mnemonic (_("_Scaling"));
  g_object_set_data (G_OBJECT (box), "scaling", check);
  gtk_grid_attach (GTK_GRID (grid), check, 1, 2, 1, 1);
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (check),
				has_conn ? vinagre_spice_connection_get_scaling (VINAGRE_SPICE_CONNECTION (conn))
				: vinagre_cache_prefs_get_boolean ("spice-connection", "scaling", FALSE));

  /* SSH Tunneling */
  box2 = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);

  /* Translators: the whole sentence will be: Use host <hostname> as a SSH tunnel*/
  check = gtk_check_button_new_with_mnemonic (_("Use h_ost"));
  g_object_set_data (G_OBJECT (box), "use_ssh", check);
  gtk_box_pack_start (GTK_BOX (box2), check, FALSE, FALSE, 0);

  ssh_host_entry = gtk_entry_new ();
  gtk_widget_set_sensitive (ssh_host_entry, FALSE);
  g_object_set_data (G_OBJECT (box), "ssh_host", ssh_host_entry);
  str = g_strdup_printf ("%s\n%s\n%s",
			 /* Translators: This is the tooltip of the SSH tunneling entry */
			 _("hostname or user@hostname"),
			 _("Supply an alternative port using colon"),
			 _("For instance: joe@example.com:5022"));
  gtk_widget_set_tooltip_text (ssh_host_entry, str);
  g_free (str);
  gtk_box_pack_start (GTK_BOX (box2), ssh_host_entry, FALSE, FALSE, 0);

  /* Translators: the whole sentence will be: Use host <hostname> as a SSH tunnel*/
  label = gtk_label_new_with_mnemonic (_("as a SSH tunnel"));
  gtk_misc_set_alignment (GTK_MISC (label), 0, 0.5);
  gtk_box_pack_start (GTK_BOX (box2), label, FALSE, FALSE, 0);

  g_signal_connect (check,
		    "toggled",
		    G_CALLBACK (ssh_tunnel_check_toggled_cb),
		    box);

  ssh_host = has_conn ? g_strdup (vinagre_spice_connection_get_ssh_tunnel_host (VINAGRE_SPICE_CONNECTION (conn)))
    : vinagre_cache_prefs_get_string  ("spice-connection", "ssh-tunnel-host", NULL);
  if (ssh_host)
    gtk_entry_set_text (GTK_ENTRY (ssh_host_entry), ssh_host);
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (check), ssh_host && *ssh_host);
  g_free (ssh_host);

  gtk_grid_attach (GTK_GRID (grid), box2, 1, 3, 1, 1);

  /* Password */
  box2 = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);

  label = gtk_label_new_with_mnemonic (_("_Password:"));
  gtk_box_pack_start (GTK_BOX (box2), label, FALSE, FALSE, 0);

  p_entry = gtk_entry_new ();
  /* Translators: This is the tooltip for the password field in a SPICE connection */
  gtk_widget_set_tooltip_text (p_entry, _("Optional"));
  g_object_set_data (G_OBJECT (box), "password_entry", p_entry);
  gtk_box_pack_start (GTK_BOX (box2), p_entry, FALSE, FALSE, 0);
  gtk_label_set_mnemonic_widget (GTK_LABEL (label), p_entry);
  str = g_strdup (vinagre_cache_prefs_get_string  ("spice-connection", "password", ""));
  gtk_entry_set_text (GTK_ENTRY (p_entry), str);
  gtk_entry_set_activates_default (GTK_ENTRY (p_entry), TRUE);
  g_free (str);
  /* FIXME: how do you set the password here? gtk_table_attach_defaults (table, box2, 1, 2, 4, 5); */

  gtk_box_pack_start (GTK_BOX (box), grid, FALSE, FALSE, 0);
  return box;
}

static GtkFileFilter *
impl_get_file_filter (VinagreProtocol *plugin)
{
  GtkFileFilter *filter;

  filter = gtk_file_filter_new ();
  /* Translators: this is a pattern to open *.spice files in a open dialog. */
  gtk_file_filter_set_name (filter, _("Spice Files"));
  gtk_file_filter_add_pattern (filter, "*.spice");

  return filter;
}

static void
vinagre_spice_protocol_iface_init (VinagreProtocolInterface *iface)
{
  iface->get_protocol  = impl_get_protocol;
  iface->get_public_description	 = impl_get_public_description;
  iface->get_default_port = impl_get_default_port;
  iface->get_connect_widget = impl_get_connect_widget;
  iface->get_mdns_service  = impl_get_mdns_service;
  iface->get_file_filter = impl_get_file_filter;
  iface->new_connection = impl_new_connection;
  iface->new_tab = impl_new_tab;
  iface->new_connection_from_file = impl_new_connection_from_file;
  iface->recognize_file = impl_recognize_file;
}

static void
vinagre_spice_plugin_class_finalize (VinagreSpicePluginClass *klass)
{
}

static void
vinagre_spice_plugin_class_init (VinagreSpicePluginClass *klass)
{
}

__attribute__((constructor)) void
spice_register_types (void)
{
  g_type_init ();
  volatile dontoptimiseaway = vinagre_spice_plugin_get_type ();
}

/* vim: set ts=8: */
