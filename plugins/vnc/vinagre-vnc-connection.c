/*
 * vinagre-vnc-connection.c
 * Child class of abstract VinagreConnection, specific to VNC protocol
 * This file is part of vinagre
 *
 * Copyright (C) 2009-2010 - Jonh Wendell <wendell@bani.com.br>
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

#include <config.h>
#include <glib/gi18n.h>
#include <stdlib.h>
#include <vinagre/vinagre-cache-prefs.h>

#include "vinagre-vnc-connection.h"
#include "vinagre-vala.h"

struct _VinagreVncConnectionPrivate
{
  gchar    *desktop_name;
  gboolean view_only;
  gboolean scaling;
  gboolean keep_ratio;
  gint     shared;
  gint     fd;
  gint     depth_profile;
  gboolean lossy_encoding;
  gchar    *ssh_tunnel_host;
  GSocket  *socket;
};

enum
{
  PROP_0,
  PROP_DESKTOP_NAME,
  PROP_VIEW_ONLY,
  PROP_SCALING,
  PROP_KEEP_RATIO,
  PROP_SHARED,
  PROP_FD,
  PROP_DEPTH_PROFILE,
  PROP_LOSSY_ENCODING,
  PROP_SSH_TUNNEL_HOST,
  PROP_SOCKET
};

#define VINAGRE_VNC_CONNECTION_PRIVATE(o)  (G_TYPE_INSTANCE_GET_PRIVATE ((o), VINAGRE_TYPE_VNC_CONNECTION, VinagreVncConnectionPrivate))
G_DEFINE_TYPE (VinagreVncConnection, vinagre_vnc_connection, VINAGRE_TYPE_CONNECTION);

static void
vinagre_vnc_connection_init (VinagreVncConnection *conn)
{
  conn->priv = G_TYPE_INSTANCE_GET_PRIVATE (conn, VINAGRE_TYPE_VNC_CONNECTION, VinagreVncConnectionPrivate);

  conn->priv->desktop_name = NULL;
  conn->priv->view_only = FALSE;
  conn->priv->scaling = FALSE;
  conn->priv->keep_ratio = FALSE;
  conn->priv->shared = -1;
  conn->priv->fd = 0;
  conn->priv->depth_profile = 0;
  conn->priv->lossy_encoding = FALSE;
  conn->priv->ssh_tunnel_host = NULL;
  conn->priv->socket = NULL;
}

static void
vinagre_vnc_connection_constructed (GObject *object)
{
  vinagre_connection_set_protocol (VINAGRE_CONNECTION (object), "vnc");
}

static void
vinagre_vnc_connection_finalize (GObject *object)
{
  VinagreVncConnection *conn = VINAGRE_VNC_CONNECTION (object);

  g_free (conn->priv->desktop_name);
  g_free (conn->priv->ssh_tunnel_host);

  G_OBJECT_CLASS (vinagre_vnc_connection_parent_class)->finalize (object);
}

static void
vinagre_vnc_connection_dispose (GObject *object)
{
  VinagreVncConnection *conn = VINAGRE_VNC_CONNECTION (object);

  if (conn->priv->socket)
    {
      g_object_unref (conn->priv->socket);
      conn->priv->socket = NULL;
    }

  G_OBJECT_CLASS (vinagre_vnc_connection_parent_class)->dispose (object);
}

static void
vinagre_vnc_connection_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
  VinagreVncConnection *conn;

  g_return_if_fail (VINAGRE_IS_VNC_CONNECTION (object));

  conn = VINAGRE_VNC_CONNECTION (object);

  switch (prop_id)
    {
      case PROP_DESKTOP_NAME:
	vinagre_vnc_connection_set_desktop_name (conn, g_value_get_string (value));
	break;

      case PROP_VIEW_ONLY:
	vinagre_vnc_connection_set_view_only (conn, g_value_get_boolean (value));
	break;

      case PROP_SCALING:
	vinagre_vnc_connection_set_scaling (conn, g_value_get_boolean (value));
	break;

      case PROP_KEEP_RATIO:
	vinagre_vnc_connection_set_keep_ratio (conn, g_value_get_boolean (value));
	break;

      case PROP_SHARED:
	vinagre_vnc_connection_set_shared (conn, g_value_get_int (value));
	break;

      case PROP_FD:
	vinagre_vnc_connection_set_fd (conn, g_value_get_int (value));
	break;

      case PROP_DEPTH_PROFILE:
	vinagre_vnc_connection_set_depth_profile (conn, g_value_get_int (value));
	break;

      case PROP_LOSSY_ENCODING:
	vinagre_vnc_connection_set_lossy_encoding (conn, g_value_get_boolean (value));
	break;

      case PROP_SSH_TUNNEL_HOST:
	vinagre_vnc_connection_set_ssh_tunnel_host (conn, g_value_get_string (value));
	break;

      case PROP_SOCKET:
	vinagre_vnc_connection_set_socket (conn, g_value_get_object (value));
	break;

      default:
	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	break;
    }
}

static void
vinagre_vnc_connection_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
  VinagreVncConnection *conn;

  g_return_if_fail (VINAGRE_IS_VNC_CONNECTION (object));

  conn = VINAGRE_VNC_CONNECTION (object);

  switch (prop_id)
    {
      case PROP_DESKTOP_NAME:
	g_value_set_string (value, conn->priv->desktop_name);
	break;

      case PROP_VIEW_ONLY:
	g_value_set_boolean (value, conn->priv->view_only);
	break;

      case PROP_SCALING:
	g_value_set_boolean (value, conn->priv->scaling);
	break;

      case PROP_KEEP_RATIO:
	g_value_set_boolean (value, conn->priv->keep_ratio);
	break;

      case PROP_SHARED:
	g_value_set_int (value, conn->priv->shared);
	break;

      case PROP_FD:
	g_value_set_int (value, vinagre_vnc_connection_get_fd (conn));
	break;

      case PROP_DEPTH_PROFILE:
	g_value_set_int (value, conn->priv->depth_profile);
	break;

      case PROP_LOSSY_ENCODING:
	g_value_set_boolean (value, conn->priv->lossy_encoding);
	break;

      case PROP_SSH_TUNNEL_HOST:
	g_value_set_string (value, conn->priv->ssh_tunnel_host);
	break;

      case PROP_SOCKET:
	g_value_set_object (value, conn->priv->socket);
	break;

      default:
	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	break;
    }
}

static void
vnc_fill_writer (VinagreConnection *conn, xmlTextWriter *writer)
{
  VinagreVncConnection *vnc_conn = VINAGRE_VNC_CONNECTION (conn);
  VINAGRE_CONNECTION_CLASS (vinagre_vnc_connection_parent_class)->impl_fill_writer (conn, writer);

  xmlTextWriterWriteFormatElement (writer, BAD_CAST "view_only", "%d", vnc_conn->priv->view_only);
  xmlTextWriterWriteFormatElement (writer, BAD_CAST "scaling", "%d", vnc_conn->priv->scaling);
  xmlTextWriterWriteFormatElement (writer, BAD_CAST "keep_ratio", "%d", vnc_conn->priv->keep_ratio);
  xmlTextWriterWriteFormatElement (writer, BAD_CAST "depth_profile", "%d", vnc_conn->priv->depth_profile);
  xmlTextWriterWriteFormatElement (writer, BAD_CAST "lossy_encoding", "%d", vnc_conn->priv->lossy_encoding);

  if (vnc_conn->priv->ssh_tunnel_host && *vnc_conn->priv->ssh_tunnel_host)
    xmlTextWriterWriteFormatElement (writer, BAD_CAST "ssh_tunnel_host", "%s", vnc_conn->priv->ssh_tunnel_host);
}

static void
vnc_parse_item (VinagreConnection *conn, xmlNode *root)
{
  xmlNode *curr;
  xmlChar *s_value;
  VinagreVncConnection *vnc_conn = VINAGRE_VNC_CONNECTION (conn);

  VINAGRE_CONNECTION_CLASS (vinagre_vnc_connection_parent_class)->impl_parse_item (conn, root);

  for (curr = root->children; curr; curr = curr->next)
    {
      s_value = xmlNodeGetContent (curr);

      if (!xmlStrcmp(curr->name, BAD_CAST "view_only"))
	{
	  vinagre_vnc_connection_set_view_only (vnc_conn, vinagre_utils_parse_boolean ((const gchar *)s_value));
	}
      else if (!xmlStrcmp(curr->name, BAD_CAST "scaling"))
	{
	  if (!scaling_command_line)
	    vinagre_vnc_connection_set_scaling (vnc_conn, vinagre_utils_parse_boolean ((const gchar *)s_value));
	}
      else if (!xmlStrcmp(curr->name, BAD_CAST "keep_ratio"))
	{
	  vinagre_vnc_connection_set_keep_ratio (vnc_conn, vinagre_utils_parse_boolean ((const gchar *)s_value));
	}
      else if (!xmlStrcmp(curr->name, BAD_CAST "depth_profile"))
	{
	  vinagre_vnc_connection_set_depth_profile (vnc_conn, atoi((const char *)s_value));
	}
      else if (!xmlStrcmp(curr->name, BAD_CAST "lossy_encoding"))
	{
	  vinagre_vnc_connection_set_lossy_encoding (vnc_conn, vinagre_utils_parse_boolean ((const gchar *)s_value));
	}
      else if (!xmlStrcmp(curr->name, BAD_CAST "ssh_tunnel_host"))
	{
	  vinagre_vnc_connection_set_ssh_tunnel_host (vnc_conn, (const gchar *)s_value);
	}

      xmlFree (s_value);
    }
}

static gchar *
vnc_get_best_name (VinagreConnection *conn)
{
  VinagreVncConnection *vnc_conn = VINAGRE_VNC_CONNECTION (conn);

  if (vinagre_connection_get_name (conn))
    return g_strdup (vinagre_connection_get_name (conn));

  if (vnc_conn->priv->desktop_name)
    return g_strdup (vnc_conn->priv->desktop_name);

  if (vinagre_connection_get_host (conn))
    return vinagre_connection_get_string_rep (conn, FALSE);

  return NULL;
}

static void
vnc_fill_conn_from_file (VinagreConnection *conn, GKeyFile *file)
{
  gint shared;
  GError *e = NULL;

  shared = g_key_file_get_integer (file, "options", "shared", &e);
  if (e)
    {
      g_error_free (e);
      return;
    }
  else
    if (shared == 0 || shared == 1)
      vinagre_vnc_connection_set_shared (VINAGRE_VNC_CONNECTION (conn), shared);
    else
      /* Translators: 'shared' here is a VNC protocol specific flag. You can translate it, but I think it's better to let it untranslated */
      g_message (_("Bad value for “shared” flag: %d. It is supposed to be 0 or 1. Ignoring it."), shared);
}

static void
vnc_parse_options_widget (VinagreConnection *conn, GtkWidget *widget)
{
  GtkWidget *view_only, *scaling, *depth_combo, *lossy, *ssh_host, *ratio;

  view_only = g_object_get_data (G_OBJECT (widget), "view_only");
  scaling = g_object_get_data (G_OBJECT (widget), "scaling");
  ratio = g_object_get_data (G_OBJECT (widget), "ratio");
  depth_combo = g_object_get_data (G_OBJECT (widget), "depth_combo");
  lossy = g_object_get_data (G_OBJECT (widget), "lossy");
  ssh_host = g_object_get_data (G_OBJECT (widget), "ssh_host");
  if (!view_only || !scaling || !depth_combo || !lossy || !ssh_host || !ratio)
    {
      g_warning ("Wrong widget passed to vnc_parse_options_widget()");
      return;
    }

  vinagre_cache_prefs_set_boolean ("vnc-connection", "view-only", gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (view_only)));
  vinagre_cache_prefs_set_boolean ("vnc-connection", "scaling", gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (scaling)));
  vinagre_cache_prefs_set_boolean ("vnc-connection", "keep-ratio", gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (ratio)));
  vinagre_cache_prefs_set_integer ("vnc-connection", "depth-profile", gtk_combo_box_get_active (GTK_COMBO_BOX (depth_combo)));
  vinagre_cache_prefs_set_boolean ("vnc-connection", "lossy-encoding", gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (lossy)));
  vinagre_cache_prefs_set_string  ("vnc-connection", "ssh-tunnel-host", gtk_entry_get_text (GTK_ENTRY (ssh_host)));

  g_object_set (conn,
		"view-only", gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (view_only)),
		"scaling", gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (scaling)),
		"keep-ratio", gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (ratio)),
		"depth-profile", gtk_combo_box_get_active (GTK_COMBO_BOX (depth_combo)),
		"lossy-encoding", gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (lossy)),
		"ssh-tunnel-host", gtk_entry_get_text (GTK_ENTRY (ssh_host)),
		NULL);
}

static void
vinagre_vnc_connection_class_init (VinagreVncConnectionClass *klass)
{
  GObjectClass* object_class = G_OBJECT_CLASS (klass);
  VinagreConnectionClass* parent_class = VINAGRE_CONNECTION_CLASS (klass);

  g_type_class_add_private (klass, sizeof (VinagreVncConnectionPrivate));

  object_class->finalize = vinagre_vnc_connection_finalize;
  object_class->dispose = vinagre_vnc_connection_dispose;
  object_class->set_property = vinagre_vnc_connection_set_property;
  object_class->get_property = vinagre_vnc_connection_get_property;
  object_class->constructed  = vinagre_vnc_connection_constructed;

  parent_class->impl_fill_writer = vnc_fill_writer;
  parent_class->impl_parse_item  = vnc_parse_item;
  parent_class->impl_get_best_name = vnc_get_best_name;
  parent_class->impl_fill_conn_from_file = vnc_fill_conn_from_file;
  parent_class->impl_parse_options_widget = vnc_parse_options_widget;

  g_object_class_install_property (object_class,
                                   PROP_DESKTOP_NAME,
                                   g_param_spec_string ("desktop-name",
                                                        "desktop-name",
	                                                "name of this connection as reported by the server",
                                                        NULL,
	                                                G_PARAM_READWRITE |
                                                        G_PARAM_CONSTRUCT |
                                                        G_PARAM_STATIC_NICK |
                                                        G_PARAM_STATIC_NAME |
                                                        G_PARAM_STATIC_BLURB));

  g_object_class_install_property (object_class,
                                   PROP_VIEW_ONLY,
                                   g_param_spec_boolean ("view-only",
                                                        "View-only connection",
	                                                "Whether this connection is a view-only one",
                                                        FALSE,
	                                                G_PARAM_READWRITE |
                                                        G_PARAM_CONSTRUCT |
                                                        G_PARAM_STATIC_NICK |
                                                        G_PARAM_STATIC_NAME |
                                                        G_PARAM_STATIC_BLURB));
  g_object_class_install_property (object_class,
                                   PROP_SCALING,
                                   g_param_spec_boolean ("scaling",
                                                        "Use scaling",
	                                                "Whether to use scaling on this connection",
                                                        FALSE,
	                                                G_PARAM_READWRITE |
                                                        G_PARAM_CONSTRUCT |
                                                        G_PARAM_STATIC_NICK |
                                                        G_PARAM_STATIC_NAME |
                                                        G_PARAM_STATIC_BLURB));

  g_object_class_install_property (object_class,
                                   PROP_KEEP_RATIO,
                                   g_param_spec_boolean ("keep-ratio",
                                                        "Keep Ratio",
	                                                "Whether to keep the aspect ratio when using scaling",
                                                        FALSE,
	                                                G_PARAM_READWRITE |
                                                        G_PARAM_CONSTRUCT |
                                                        G_PARAM_STATIC_NICK |
                                                        G_PARAM_STATIC_NAME |
                                                        G_PARAM_STATIC_BLURB));

  g_object_class_install_property (object_class,
                                   PROP_SHARED,
                                   g_param_spec_int ("shared",
                                                     "shared flag",
	                                              "if the server should allow more than one client connected",
                                                      -1,
                                                      1,
                                                      -1,
	                                              G_PARAM_READWRITE |
                                                      G_PARAM_CONSTRUCT |
                                                      G_PARAM_STATIC_NICK |
                                                      G_PARAM_STATIC_NAME |
                                                      G_PARAM_STATIC_BLURB));

  g_object_class_install_property (object_class,
                                   PROP_FD,
                                   g_param_spec_int ("fd",
                                                     "file descriptor",
	                                              "the file descriptor for this connection",
                                                      0,
                                                      G_MAXINT,
                                                      0,
	                                              G_PARAM_READWRITE |
                                                      G_PARAM_CONSTRUCT |
                                                      G_PARAM_STATIC_NICK |
                                                      G_PARAM_STATIC_NAME |
                                                      G_PARAM_STATIC_BLURB));

  g_object_class_install_property (object_class,
                                   PROP_DEPTH_PROFILE,
                                   g_param_spec_int ("depth-profile",
                                                     "Depth Profile",
	                                              "The profile of depth color to be used in gtk-vnc widget",
                                                      0,
                                                      5,
                                                      0,
	                                              G_PARAM_READWRITE |
                                                      G_PARAM_CONSTRUCT |
                                                      G_PARAM_STATIC_NICK |
                                                      G_PARAM_STATIC_NAME |
                                                      G_PARAM_STATIC_BLURB));

  g_object_class_install_property (object_class,
                                   PROP_LOSSY_ENCODING,
                                   g_param_spec_boolean ("lossy-encoding",
                                                        "Lossy encoding",
	                                                "Whether to use a lossy encoding",
                                                        FALSE,
	                                                G_PARAM_READWRITE |
                                                        G_PARAM_CONSTRUCT |
                                                        G_PARAM_STATIC_NICK |
                                                        G_PARAM_STATIC_NAME |
                                                        G_PARAM_STATIC_BLURB));

  g_object_class_install_property (object_class,
                                   PROP_SSH_TUNNEL_HOST,
                                   g_param_spec_string ("ssh-tunnel-host",
                                                        "SSH Tunnel Host",
	                                                "hostname used to create the SSH tunnel",
                                                        NULL,
	                                                G_PARAM_READWRITE |
                                                        G_PARAM_CONSTRUCT |
                                                        G_PARAM_STATIC_NICK |
                                                        G_PARAM_STATIC_NAME |
                                                        G_PARAM_STATIC_BLURB));

  g_object_class_install_property (object_class,
                                   PROP_SOCKET,
                                   g_param_spec_object ("socket",
                                                        "Socket",
	                                                "A GSocket for this connection",
	                                                G_TYPE_SOCKET,
	                                                G_PARAM_READWRITE |
                                                        G_PARAM_CONSTRUCT |
                                                        G_PARAM_STATIC_NICK |
                                                        G_PARAM_STATIC_NAME |
                                                        G_PARAM_STATIC_BLURB));

}

VinagreConnection *
vinagre_vnc_connection_new (void)
{
  return VINAGRE_CONNECTION (g_object_new (VINAGRE_TYPE_VNC_CONNECTION, NULL));
}

void
vinagre_vnc_connection_set_desktop_name (VinagreVncConnection *conn,
					 const gchar *desktop_name)
{
  g_return_if_fail (VINAGRE_IS_VNC_CONNECTION (conn));

  g_free (conn->priv->desktop_name);
  conn->priv->desktop_name = g_strdup (desktop_name);
}
const gchar *
vinagre_vnc_connection_get_desktop_name (VinagreVncConnection *conn)
{
  g_return_val_if_fail (VINAGRE_IS_VNC_CONNECTION (conn), NULL);

  return conn->priv->desktop_name;
}

void
vinagre_vnc_connection_set_shared (VinagreVncConnection *conn,
				   gint value)
{
  g_return_if_fail (VINAGRE_IS_VNC_CONNECTION (conn));
  g_return_if_fail (value >=-1 && value <=1);

  conn->priv->shared = value;
}
gint
vinagre_vnc_connection_get_shared (VinagreVncConnection *conn)
{
  g_return_val_if_fail (VINAGRE_IS_VNC_CONNECTION (conn), -1);

  return conn->priv->shared;
}

void
vinagre_vnc_connection_set_view_only (VinagreVncConnection *conn,
				      gboolean value)
{
  g_return_if_fail (VINAGRE_IS_VNC_CONNECTION (conn));

  conn->priv->view_only = value;
}
gboolean
vinagre_vnc_connection_get_view_only (VinagreVncConnection *conn)
{
  g_return_val_if_fail (VINAGRE_IS_VNC_CONNECTION (conn), FALSE);

  return conn->priv->view_only;
}

void
vinagre_vnc_connection_set_scaling (VinagreVncConnection *conn,
				    gboolean value)
{
  g_return_if_fail (VINAGRE_IS_VNC_CONNECTION (conn));

  conn->priv->scaling = value;
}
gboolean
vinagre_vnc_connection_get_scaling (VinagreVncConnection *conn)
{
  g_return_val_if_fail (VINAGRE_IS_VNC_CONNECTION (conn), FALSE);

  return conn->priv->scaling;
}

void
vinagre_vnc_connection_set_keep_ratio (VinagreVncConnection *conn,
				       gboolean value)
{
  g_return_if_fail (VINAGRE_IS_VNC_CONNECTION (conn));

  conn->priv->keep_ratio = value;
}
gboolean
vinagre_vnc_connection_get_keep_ratio (VinagreVncConnection *conn)
{
  g_return_val_if_fail (VINAGRE_IS_VNC_CONNECTION (conn), FALSE);

  return conn->priv->keep_ratio;
}

void
vinagre_vnc_connection_set_fd (VinagreVncConnection *conn,
			       gint                 value)
{
  g_return_if_fail (VINAGRE_IS_VNC_CONNECTION (conn));
  g_return_if_fail (value >= 0);

  conn->priv->fd = value;
}
gint
vinagre_vnc_connection_get_fd (VinagreVncConnection *conn)
{
  g_return_val_if_fail (VINAGRE_IS_VNC_CONNECTION (conn), 0);

  if (conn->priv->socket)
    return g_socket_get_fd (conn->priv->socket);
  else
    return conn->priv->fd;
}

void
vinagre_vnc_connection_set_socket (VinagreVncConnection *conn,
				   GSocket              *socket)
{
  g_return_if_fail (VINAGRE_IS_VNC_CONNECTION (conn));

  if (socket)
    conn->priv->socket = g_object_ref (socket);
}
GSocket *
vinagre_vnc_connection_get_socket (VinagreVncConnection *conn)
{
  g_return_val_if_fail (VINAGRE_IS_VNC_CONNECTION (conn), 0);

  return conn->priv->socket;
}

void
vinagre_vnc_connection_set_depth_profile (VinagreVncConnection *conn,
					  gint                 value)
{
  g_return_if_fail (VINAGRE_IS_VNC_CONNECTION (conn));
  g_return_if_fail (value >= 0);

  conn->priv->depth_profile = value;
}
gint
vinagre_vnc_connection_get_depth_profile (VinagreVncConnection *conn)
{
  g_return_val_if_fail (VINAGRE_IS_VNC_CONNECTION (conn), 0);

  return conn->priv->depth_profile;
}

void
vinagre_vnc_connection_set_lossy_encoding (VinagreVncConnection *conn,
					   gboolean value)
{
  g_return_if_fail (VINAGRE_IS_VNC_CONNECTION (conn));

  conn->priv->lossy_encoding = value;
}
gboolean
vinagre_vnc_connection_get_lossy_encoding (VinagreVncConnection *conn)
{
  g_return_val_if_fail (VINAGRE_IS_VNC_CONNECTION (conn), FALSE);

  return conn->priv->lossy_encoding;
}

void
vinagre_vnc_connection_set_ssh_tunnel_host (VinagreVncConnection *conn,
					    const gchar *host)
{
  g_return_if_fail (VINAGRE_IS_VNC_CONNECTION (conn));

  g_free (conn->priv->ssh_tunnel_host);
  conn->priv->ssh_tunnel_host = g_strdup (host);
}
const gchar *
vinagre_vnc_connection_get_ssh_tunnel_host (VinagreVncConnection *conn)
{
  g_return_val_if_fail (VINAGRE_IS_VNC_CONNECTION (conn), NULL);

  return conn->priv->ssh_tunnel_host;
}

/* vim: set ts=8: */
