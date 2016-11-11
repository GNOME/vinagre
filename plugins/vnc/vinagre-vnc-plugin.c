/*
 * vinagre-vnc-plugin.c
 * This file is part of vinagre
 *
 * Copyright (C) 2009 Jonh Wendell <wendell@bani.com.br>
 * 
 * vinagre-vnc-plugin.c is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 * 
 * vinagre-vnc-plugin.c is distributed in the hope that it will be useful, but
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

#include <string.h>
#include <glib/gi18n-lib.h>
#include <gmodule.h>
#include <vncdisplay.h>

#include <vinagre/vinagre-prefs.h>
#include <vinagre/vinagre-cache-prefs.h>
#include <vinagre/vinagre-protocol.h>

#include "vinagre-vnc-plugin.h"
#include "vinagre-vnc-connection.h"
#include "vinagre-vnc-tab.h"

void vnc_register_types (void);

static void vinagre_vnc_protocol_iface_init (VinagreProtocolInterface *iface);
G_DEFINE_TYPE_EXTENDED (VinagreVncPlugin,
			vinagre_vnc_plugin,
			VINAGRE_TYPE_STATIC_EXTENSION,
			0,
			G_IMPLEMENT_INTERFACE (VINAGRE_TYPE_PROTOCOL,
					       vinagre_vnc_protocol_iface_init))

static const GOptionEntry vinagre_vnc_args[] =
{
  { "vnc-scale", 0, 0, G_OPTION_ARG_NONE, &scaling_command_line,
  /* Translators: this is a command line option (run vinagre --help) */
  N_("Enable scaled mode"), 0 },
  { NULL }
};

static GSList *
impl_get_context_groups (VinagreProtocol *plugin)
{
  GOptionGroup *group;
  GSList       *groups = NULL;

  scaling_command_line = FALSE;
  group = g_option_group_new ("vnc",
			      /* Translators: this is a command line option (run vinagre --help) */
			      _("VNC Options:"),
			      /* Translators: this is a command line option (run vinagre --help) */
			      _("Show VNC Options"),
			      NULL,
			      NULL);
  g_option_group_add_entries (group, vinagre_vnc_args);

  groups = g_slist_append (groups, group);
  groups = g_slist_append (groups, vnc_display_get_option_group ());

  return groups;
}

static const gchar *
impl_get_protocol (VinagreProtocol *plugin)
{
  return "vnc";
}

static gchar **
impl_get_public_description (VinagreProtocol *plugin)
{
  gchar **result = g_new (gchar *, 3);

  result[0] = g_strdup (_("VNC"));
  result[1] = g_strdup (_("Access Unix/Linux, Windows and other remote desktops."));
  result[2] = NULL;

  return result;
}

static const gchar *
impl_get_mdns_service (VinagreProtocol *plugin)
{
  return "_rfb._tcp";
}

static VinagreConnection *
impl_new_connection (VinagreProtocol *plugin)
{
  VinagreConnection *conn;

  conn = vinagre_vnc_connection_new ();
  vinagre_vnc_connection_set_scaling (VINAGRE_VNC_CONNECTION (conn),
				      scaling_command_line);

  return conn;
}

static VinagreConnection *
impl_new_connection_from_file (VinagreProtocol *plugin,
			       const gchar   *data,
			       gboolean       use_bookmarks,
			       gchar        **error_msg)
{
  GKeyFile          *file;
  GError            *error;
  gboolean           loaded;
  gchar             *host, *actual_host, *protocol;
  gint               port;
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

  if (!g_key_file_has_group (file, "Connection"))
    {
      /* Translators: Do not translate "Connection". It's the name of a group in the .vnc (.ini like) file. */
      *error_msg = g_strdup (_("The file is not a VNC one: Missing the group “Connection”."));
      goto the_end;
    }

  if (!g_key_file_has_key (file, "Connection", "Host", NULL))
    {
      /* Translators: Do not translate "Host". It's the name of a key in the .vnc (.ini like) file. */
      *error_msg = g_strdup (_("The file is not a VNC one: Missing the key “Host”."));
      goto the_end;
    }

  host = g_key_file_get_string (file, "Connection", "Host", NULL);
  port = g_key_file_get_integer (file, "Connection", "Port", NULL);
  if (!port)
    {
      if (!vinagre_connection_split_string (host, "vnc", &protocol, &actual_host, &port, error_msg))
	goto the_end;

      g_free (host);
      host = actual_host;
    }

  if (use_bookmarks)
    conn = vinagre_bookmarks_exists (vinagre_bookmarks_get_default (), "vnc", host, port);
  if (!conn)
    {
      gchar *s_value;
      gint shared;

      conn = vinagre_vnc_connection_new ();
      vinagre_connection_set_host (conn, host);
      vinagre_connection_set_port (conn, port);

      s_value = g_key_file_get_string  (file, "Connection", "Username", NULL);
      vinagre_connection_set_username (conn, s_value);
      g_free (s_value);

      s_value = g_key_file_get_string  (file, "Connection", "Password", NULL);
      vinagre_connection_set_password (conn, s_value);
      g_free (s_value);

      shared = g_key_file_get_integer (file, "Options", "Shared", NULL);
      if (shared == 0 || shared == 1)
	vinagre_vnc_connection_set_shared (VINAGRE_VNC_CONNECTION (conn), shared);
      else
        /* Translators: 'shared' here is a VNC protocol specific flag. You can translate it, but I think it's better to let it untranslated */
	g_message (_("Bad value for “shared” flag: %d. It is supposed to be 0 or 1. Ignoring it."), shared);
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
      result = g_str_has_suffix (filename, ".vnc");
      g_free (filename);
    }

  return result;
}

static GtkWidget *
impl_new_tab (VinagreProtocol *plugin,
	      VinagreConnection *conn,
	      VinagreWindow     *window)
{
  return vinagre_vnc_tab_new (conn, window);
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

static void
scaling_check_toggled_cb (GtkToggleButton *button, GObject *box)
{
  gboolean active = gtk_toggle_button_get_active (button);
  GtkWidget *ratio = g_object_get_data (G_OBJECT (box), "ratio");

  gtk_widget_set_sensitive (ratio, active);
}

static GtkWidget *
impl_get_connect_widget (VinagreProtocol *plugin, VinagreConnection *conn)
{
  GtkWidget *box, *check, *label, *combo, *box2, *ssh_host_entry;
  gchar     *str, *ssh_host;
  gboolean has_conn = VINAGRE_IS_VNC_CONNECTION (conn), active;

  box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);

  str = g_strdup_printf ("<b>%s</b>", _("VNC Options"));
  label = gtk_label_new (str);
  g_free (str);
  gtk_label_set_use_markup (GTK_LABEL (label), TRUE);
  gtk_misc_set_alignment (GTK_MISC (label), 0, 0.5);
  gtk_box_pack_start (GTK_BOX (box), label, FALSE, FALSE, 0);

  /* View only check button */
  check = gtk_check_button_new_with_mnemonic (_("_View only"));
  g_object_set_data (G_OBJECT (box), "view_only", check);
  gtk_widget_set_margin_left (check, 12);
  gtk_container_add (GTK_CONTAINER (box), check);
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (check),
				has_conn ? vinagre_vnc_connection_get_view_only (VINAGRE_VNC_CONNECTION (conn))
				: vinagre_cache_prefs_get_boolean ("vnc-connection", "view-only", FALSE));

  /* Scaling check button */
  check = gtk_check_button_new_with_mnemonic (_("_Scaling"));
  g_object_set_data (G_OBJECT (box), "scaling", check);
  gtk_widget_set_margin_left (check, 12);
  gtk_container_add (GTK_CONTAINER (box), check);
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (check),
				has_conn ? vinagre_vnc_connection_get_scaling (VINAGRE_VNC_CONNECTION (conn))
				: vinagre_cache_prefs_get_boolean ("vnc-connection", "scaling", FALSE));
  active = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (check));
  g_signal_connect (check,
		    "toggled",
		    G_CALLBACK (scaling_check_toggled_cb),
		    box);

  /* Keep ratio check button */
  check = gtk_check_button_new_with_mnemonic (_("_Keep aspect ratio"));
  g_object_set_data (G_OBJECT (box), "ratio", check);
  gtk_widget_set_margin_left (check, 24);
  gtk_container_add (GTK_CONTAINER (box), check);
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (check),
				has_conn ? vinagre_vnc_connection_get_keep_ratio (VINAGRE_VNC_CONNECTION (conn))
				: vinagre_cache_prefs_get_boolean ("vnc-connection", "keep-ratio", TRUE));
  gtk_widget_set_sensitive (check, active);

  /* JPEG Compression check button */
  check = gtk_check_button_new_with_mnemonic (_("_Use JPEG Compression"));
  gtk_widget_set_tooltip_text (check, _("This might not work on all VNC servers"));
  g_object_set_data (G_OBJECT (box), "lossy", check);
  gtk_widget_set_margin_left (check, 12);
  gtk_container_add (GTK_CONTAINER (box), check);
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (check),
				has_conn ? vinagre_vnc_connection_get_lossy_encoding (VINAGRE_VNC_CONNECTION (conn))
				: vinagre_cache_prefs_get_boolean ("vnc-connection", "lossy-encoding", FALSE));

  /* Color depth combo box */
  box2 = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
  label = gtk_label_new_with_mnemonic (_("Color _Depth:"));
  gtk_misc_set_alignment (GTK_MISC (label), 0, 0.5);
  gtk_box_pack_start (GTK_BOX (box2), GTK_WIDGET (label), FALSE, FALSE, 0);

  combo = gtk_combo_box_text_new ();
  gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (combo), _("Use Server Settings"));
  gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (combo), _("True Color (24 bits)"));
  gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (combo), _("High Color (16 bits)"));
  gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (combo), _("Low Color (8 bits)"));
  gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (combo), _("Ultra Low Color (3 bits)"));
  gtk_combo_box_set_active (GTK_COMBO_BOX (combo),
			    has_conn ? vinagre_vnc_connection_get_depth_profile (VINAGRE_VNC_CONNECTION (conn))
			    : vinagre_cache_prefs_get_integer ("vnc-connection", "depth-profile", 0));
  g_object_set_data (G_OBJECT (box), "depth_combo", combo);
  gtk_label_set_mnemonic_widget (GTK_LABEL (label), combo);
  gtk_box_pack_start (GTK_BOX (box2), GTK_WIDGET (combo), FALSE, FALSE, 0);
  gtk_widget_set_margin_left (box2, 12);
  gtk_container_add (GTK_CONTAINER (box), box2);

  /* SSH Tunneling */
  box2 = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);

  /* Translators: the whole sentence will be: Use Host <hostname> as a SSH tunnel*/
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

  ssh_host = has_conn ? g_strdup (vinagre_vnc_connection_get_ssh_tunnel_host (VINAGRE_VNC_CONNECTION (conn)))
                      : vinagre_cache_prefs_get_string  ("vnc-connection", "ssh-tunnel-host", NULL);
  if (ssh_host)
    gtk_entry_set_text (GTK_ENTRY (ssh_host_entry), ssh_host);
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (check), ssh_host && *ssh_host);
  g_free (ssh_host);

  gtk_widget_set_margin_left (box2, 12);
  gtk_container_add (GTK_CONTAINER (box), box2);

  return box;
}

static gint
impl_get_default_port (VinagreProtocol *plugin)
{
  return 5900;
}

static GtkFileFilter *
impl_get_file_filter (VinagreProtocol *plugin)
{
  GtkFileFilter *filter;

  filter = gtk_file_filter_new ();
  /* Translators: this is a pattern to open *.vnc files in a open dialog. */
  gtk_file_filter_set_name (filter, _("VNC Files"));
  gtk_file_filter_add_pattern (filter, "*.vnc");

  return filter;
}

static void
vinagre_vnc_plugin_class_init (VinagreVncPluginClass *klass)
{
}
static void
vinagre_vnc_plugin_class_finalize (VinagreVncPluginClass *klass)
{
}
static void
vinagre_vnc_plugin_init (VinagreVncPlugin *plugin)
{
}

static void
vinagre_vnc_protocol_iface_init (VinagreProtocolInterface *iface)
{
  iface->get_context_groups = impl_get_context_groups;
  iface->get_protocol  = impl_get_protocol;
  iface->get_public_description  = impl_get_public_description;
  iface->new_connection = impl_new_connection;
  iface->new_connection_from_file = impl_new_connection_from_file;
  iface->recognize_file = impl_recognize_file;
  iface->get_mdns_service  = impl_get_mdns_service;
  iface->new_tab = impl_new_tab;
  iface->get_connect_widget = impl_get_connect_widget;
  iface->get_default_port = impl_get_default_port;
  iface->get_file_filter = impl_get_file_filter;
}

__attribute__((constructor)) void
vnc_register_types (void)
{
  g_type_init ();
  volatile dontoptimiseaway = vinagre_vnc_plugin_get_type ();
}

/* vim: set ts=8: */
