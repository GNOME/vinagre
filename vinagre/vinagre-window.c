/*
 * vinagre-window.c
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

#include <errno.h>
#include <time.h>
#include <sys/types.h>
#include <fcntl.h>
#include <string.h>

#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <gdk/gdkkeysyms.h>

#include "vinagre-window.h"
#include "vinagre-notebook.h"
#include "vinagre-prefs.h"
#include "vinagre-cache-prefs.h"
#include "vinagre-bookmarks.h"
#include "vinagre-ui.h"
#include "vinagre-window-private.h"
#include "vinagre-bookmarks-entry.h"
#include "vinagre-plugins-engine.h"
#include "vinagre-vala.h"

#ifdef VINAGRE_HAVE_AVAHI
#include "vinagre-mdns.h"
#endif

#define VINAGRE_WINDOW_GET_PRIVATE(object)(G_TYPE_INSTANCE_GET_PRIVATE ((object),\
					 VINAGRE_TYPE_WINDOW,                    \
					 VinagreWindowPrivate))

G_DEFINE_TYPE(VinagreWindow, vinagre_window, GTK_TYPE_WINDOW)

static void
vinagre_window_dispose (GObject *object)
{
  VinagreWindow *window = VINAGRE_WINDOW (object);

  if (window->priv->manager)
    {
      g_object_unref (window->priv->manager);
      window->priv->manager = NULL;
    }

  if (window->priv->update_recents_menu_ui_id != 0)
    {
      GtkRecentManager *recent_manager = gtk_recent_manager_get_default ();

      g_signal_handler_disconnect (recent_manager,
				   window->priv->update_recents_menu_ui_id);
      window->priv->update_recents_menu_ui_id = 0;
    }

    if (window->priv->listener)
    {
        g_object_unref (window->priv->listener);
        window->priv->listener = NULL;
    }
  G_OBJECT_CLASS (vinagre_window_parent_class)->dispose (object);
}

static gboolean
vinagre_window_delete_event (GtkWidget   *widget,
			     GdkEventAny *event)
{
  VinagreWindow *window = VINAGRE_WINDOW (widget);

  vinagre_window_close_all_tabs (window);

  if (GTK_WIDGET_CLASS (vinagre_window_parent_class)->delete_event)
    return GTK_WIDGET_CLASS (vinagre_window_parent_class)->delete_event (widget, event);
  else
    return FALSE;
}

static void
vinagre_window_show_hide_controls (VinagreWindow *window)
{
  if (window->priv->fullscreen)
    {
      window->priv->toolbar_visible = gtk_widget_get_visible (window->priv->toolbar);
      gtk_widget_hide (window->priv->toolbar);

      gtk_widget_hide (window->priv->menubar);

      window->priv->statusbar_visible = gtk_widget_get_visible (window->priv->statusbar);
      gtk_widget_hide (window->priv->statusbar);

      gtk_notebook_set_show_tabs (GTK_NOTEBOOK (window->priv->notebook), FALSE);
      gtk_notebook_set_show_border (GTK_NOTEBOOK (window->priv->notebook), FALSE);
    }
  else
    {
      if (window->priv->toolbar_visible)
        gtk_widget_show_all (window->priv->toolbar);

      gtk_widget_show_all (window->priv->menubar);

      if (window->priv->statusbar_visible)
        gtk_widget_show_all (window->priv->statusbar);

      vinagre_notebook_show_hide_tabs (VINAGRE_NOTEBOOK (window->priv->notebook));
      gtk_notebook_set_show_border (GTK_NOTEBOOK (window->priv->notebook), TRUE);
    }
}

static gboolean
vinagre_window_state_event_cb (GtkWidget *widget,
			       GdkEventWindowState *event)
{
  VinagreWindow *window = VINAGRE_WINDOW (widget);

  window->priv->window_state = event->new_window_state;
  vinagre_cache_prefs_set_integer ("window", "window-state", window->priv->window_state);

  if ((event->changed_mask & GDK_WINDOW_STATE_FULLSCREEN) == 0)
    return FALSE;

  if (event->new_window_state & GDK_WINDOW_STATE_FULLSCREEN)
    window->priv->fullscreen = TRUE;
  else
    window->priv->fullscreen = FALSE;

  vinagre_window_show_hide_controls (window);

  if (GTK_WIDGET_CLASS (vinagre_window_parent_class)->window_state_event)
    return GTK_WIDGET_CLASS (vinagre_window_parent_class)->window_state_event (widget, event);
  else
    return FALSE;
}

static gboolean 
vinagre_window_configure_event (GtkWidget         *widget,
			        GdkEventConfigure *event)
{
  VinagreWindow *window = VINAGRE_WINDOW (widget);

  if ((vinagre_cache_prefs_get_integer ("window", "window-state", 0) & GDK_WINDOW_STATE_MAXIMIZED) == 0)
    {
      window->priv->width  = event->width;
      window->priv->height = event->height;

      vinagre_cache_prefs_set_integer ("window", "window-width", window->priv->width);
      vinagre_cache_prefs_set_integer ("window", "window-height", window->priv->height);
    }

  return GTK_WIDGET_CLASS (vinagre_window_parent_class)->configure_event (widget, event);
}

static void
vinagre_window_class_init (VinagreWindowClass *klass)
{
  GObjectClass *object_class    = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class  = GTK_WIDGET_CLASS (klass);

  object_class->dispose  = vinagre_window_dispose;

  widget_class->window_state_event = vinagre_window_state_event_cb;
  widget_class->configure_event    = vinagre_window_configure_event;
  widget_class->delete_event       = vinagre_window_delete_event;

  g_type_class_add_private (object_class, sizeof(VinagreWindowPrivate));
}

static void
menu_item_select_cb (GtkMenuItem   *proxy,
		     VinagreWindow *window)
{
  GtkAction *action;
  char *message;

  action = g_object_get_data (G_OBJECT (proxy),  "gtk-action");
  g_return_if_fail (action != NULL);

  g_object_get (G_OBJECT (action), "tooltip", &message, NULL);
  if (message)
    {
      gtk_statusbar_push (GTK_STATUSBAR (window->priv->statusbar),
			  window->priv->tip_message_cid, message);
      g_free (message);
    }
}

static void
menu_item_deselect_cb (GtkMenuItem   *proxy,
                       VinagreWindow *window)
{
  gtk_statusbar_pop (GTK_STATUSBAR (window->priv->statusbar),
		     window->priv->tip_message_cid);
}

static void
connect_proxy_cb (GtkUIManager  *manager,
                  GtkAction     *action,
                  GtkWidget     *proxy,
                  VinagreWindow *window)
{
  if (GTK_IS_MENU_ITEM (proxy))
    {
       g_signal_connect (proxy, "select",
			 G_CALLBACK (menu_item_select_cb), window);
       g_signal_connect (proxy, "deselect",
			 G_CALLBACK (menu_item_deselect_cb), window);
    }
}

static void
disconnect_proxy_cb (GtkUIManager  *manager,
                     GtkAction     *action,
                     GtkWidget     *proxy,
                     VinagreWindow *window)
{
  if (GTK_IS_MENU_ITEM (proxy))
    {
      g_signal_handlers_disconnect_by_func
		(proxy, G_CALLBACK (menu_item_select_cb), window);
      g_signal_handlers_disconnect_by_func
		(proxy, G_CALLBACK (menu_item_deselect_cb), window);
    }
}

static void
activate_recent_cb (GtkRecentChooser *action, VinagreWindow *window)
{
  VinagreConnection *conn;
  gchar             *error, *uri;

  uri = gtk_recent_chooser_get_current_uri (action);
  conn = vinagre_connection_new_from_string (uri,
					     &error,
					     TRUE);
  if (conn)
    {
      vinagre_cmd_open_bookmark (window, conn);
      g_object_unref (conn);
    }
  else
    vinagre_utils_show_error_dialog (NULL, error ? error : _("Unknown error"), GTK_WINDOW (window));

  g_free (error);
  g_free (uri);
}

static void
update_recent_connections (VinagreWindow *window)
{
  VinagreWindowPrivate *p = window->priv;

  g_return_if_fail (p->recent_action_group != NULL);

  if (p->recents_menu_ui_id != 0)
    gtk_ui_manager_remove_ui (p->manager, p->recents_menu_ui_id);

  p->recents_menu_ui_id = gtk_ui_manager_new_merge_id (p->manager);

  gtk_ui_manager_add_ui (p->manager,
			 p->recents_menu_ui_id,
			 "/MenuBar/RemoteMenu/FileRecentsPlaceholder",
			 "recent_connections",
			 "recent_connections",
			 GTK_UI_MANAGER_MENUITEM,
			 FALSE);
}

static void
recent_manager_changed (GtkRecentManager *manager,
			VinagreWindow     *window)
{
  update_recent_connections (window);
}

static void
show_hide_accels (VinagreWindow *window)
{
  static gchar* menu_accel_default;
  gboolean show_accels;

  if (!menu_accel_default)
    g_object_get (gtk_settings_get_default (), "gtk-menu-bar-accel", &menu_accel_default, NULL);

  show_accels = g_settings_get_boolean (vinagre_prefs_get_default_gsettings (), "show-accels");

  g_object_set (gtk_settings_get_default (),
		"gtk-enable-accels", show_accels,
		"gtk-enable-mnemonics", show_accels,
                "gtk-menu-bar-accel", show_accels ? menu_accel_default : NULL,
		NULL);
}

static void
create_menu_bar_and_toolbar (VinagreWindow *window, 
			     GtkWidget     *main_box)
{
  GtkActionGroup   *action_group;
  GtkUIManager     *manager;
  GError           *error = NULL;
  GtkRecentManager *recent_manager;
  GtkRecentFilter  *filter;
  GtkAction        *action;
  gchar            *filename;

  manager = gtk_ui_manager_new ();
  window->priv->manager = manager;

  /* show tooltips in the statusbar */
  g_signal_connect (manager,
		    "connect-proxy",
		    G_CALLBACK (connect_proxy_cb),
		    window);
  g_signal_connect (manager,
		   "disconnect-proxy",
		    G_CALLBACK (disconnect_proxy_cb),
		    window);

  gtk_window_add_accel_group (GTK_WINDOW (window),
			      gtk_ui_manager_get_accel_group (manager));

  /* Normal actions */
  action_group = gtk_action_group_new ("VinagreWindowAlwaysSensitiveActions");
  gtk_action_group_set_translation_domain (action_group, NULL);
  gtk_action_group_add_actions (action_group,
				vinagre_always_sensitive_entries,
				G_N_ELEMENTS (vinagre_always_sensitive_entries),
				window);
  
  gtk_action_group_add_toggle_actions (action_group,
				       vinagre_always_sensitive_toggle_entries,
				       G_N_ELEMENTS (vinagre_always_sensitive_toggle_entries),
				       window);

  gtk_ui_manager_insert_action_group (manager, action_group, 0);
  g_object_unref (action_group);
  window->priv->always_sensitive_action_group = action_group;

  action = gtk_action_group_get_action (action_group, "RemoteConnect");
  gtk_action_set_is_important (action, TRUE);

  action = gtk_action_group_get_action (action_group, "ViewKeyboardShortcuts");
  g_settings_bind (vinagre_prefs_get_default_gsettings (), "show-accels",
    action, "active", G_SETTINGS_BIND_DEFAULT);

  /* Remote connected actions */
  action_group = gtk_action_group_new ("VinagreWindowRemoteConnectedActions");
  gtk_action_group_set_translation_domain (action_group, NULL);
  gtk_action_group_add_actions (action_group,
				vinagre_remote_connected_entries,
				G_N_ELEMENTS (vinagre_remote_connected_entries),
				window);
  gtk_action_group_set_sensitive (action_group, FALSE);
  gtk_ui_manager_insert_action_group (manager, action_group, 0);
  g_object_unref (action_group);
  window->priv->remote_connected_action_group = action_group;

  /* Remote initialized actions */
  action_group = gtk_action_group_new ("VinagreWindowRemoteInitializedActions");
  gtk_action_group_set_translation_domain (action_group, NULL);
  gtk_action_group_add_actions (action_group,
				vinagre_remote_initialized_entries,
				G_N_ELEMENTS (vinagre_remote_initialized_entries),
				window);
  gtk_action_group_set_sensitive (action_group, FALSE);
  gtk_ui_manager_insert_action_group (manager, action_group, 0);
  g_object_unref (action_group);
  window->priv->remote_initialized_action_group = action_group;

  /* now load the UI definition */
  filename = vinagre_dirs_get_package_data_file ("vinagre-ui.xml");
  gtk_ui_manager_add_ui_from_file (manager, filename, &error);
  g_free (filename);

  if (error != NULL)
    {
      g_critical (_("Could not merge UI XML file: %s"), error->message);
      g_error_free (error);
      return;
    }

  /* Bookmarks */
  action_group = gtk_action_group_new ("BookmarksActions");
  gtk_action_group_set_translation_domain (action_group, NULL);
  window->priv->bookmarks_list_action_group = action_group;
  gtk_ui_manager_insert_action_group (manager, action_group, 0);
  g_object_unref (action_group);

  window->priv->menubar = gtk_ui_manager_get_widget (manager, "/MenuBar");
  gtk_box_pack_start (GTK_BOX (main_box), 
		      window->priv->menubar, 
		      FALSE, 
		      FALSE, 
		      0);

  window->priv->toolbar = gtk_ui_manager_get_widget (manager, "/ToolBar");
  gtk_style_context_add_class (gtk_widget_get_style_context (window->priv->toolbar),
                               GTK_STYLE_CLASS_PRIMARY_TOOLBAR);
  gtk_widget_hide (window->priv->toolbar);
  gtk_box_pack_start (GTK_BOX (main_box),
		      window->priv->toolbar,
		      FALSE,
		      FALSE,
		      0);

  /* Recent connections */
  window->priv->recent_action = gtk_recent_action_new ("recent_connections",
						     _("_Recent Connections"),
						       NULL, NULL);
  g_object_set (G_OBJECT (window->priv->recent_action),
		"show-not-found", TRUE,
		"local-only", FALSE,
		"show-private", TRUE,
		"show-tips", TRUE,
		"sort-type", GTK_RECENT_SORT_MRU,
		NULL);
  g_signal_connect (window->priv->recent_action,
		    "item-activated",
		    G_CALLBACK (activate_recent_cb),
		    window);

  action_group = gtk_action_group_new ("VinagreRecentConnectionsActions");
  gtk_action_group_set_translation_domain (action_group, NULL);

  gtk_ui_manager_insert_action_group (manager, action_group, 0);
  g_object_unref (action_group);
  window->priv->recent_action_group = action_group;
  gtk_action_group_add_action (window->priv->recent_action_group,
			       window->priv->recent_action);

  filter = gtk_recent_filter_new ();
  gtk_recent_filter_add_group (filter, "vinagre");
  gtk_recent_chooser_add_filter (GTK_RECENT_CHOOSER (window->priv->recent_action),
				 filter);

  update_recent_connections (window);

  recent_manager = gtk_recent_manager_get_default ();
  window->priv->update_recents_menu_ui_id = g_signal_connect (
		    recent_manager,
		    "changed",
		    G_CALLBACK (recent_manager_changed),
		    window);

  g_signal_connect_swapped (vinagre_prefs_get_default_gsettings (),
			    "changed::show-accels",
			     G_CALLBACK (show_hide_accels),
			     window);
  show_hide_accels (window);
}

/*
 * Doubles underscore to avoid spurious menu accels.
 */
static gchar *
_escape_underscores (const gchar* text,
	gssize       length)
{
	GString *str;
	const gchar *p;
	const gchar *end;

	g_return_val_if_fail (text != NULL, NULL);

	if (length < 0)
		length = strlen (text);

	str = g_string_sized_new (length);

	p = text;
	end = text + length;

	while (p != end)
	{
		const gchar *next;
		next = g_utf8_next_char (p);

		switch (*p)
		{
			case '_':
				g_string_append (str, "__");
				break;
			default:
				g_string_append_len (str, p, next - p);
				break;
		}

		p = next;
	}

	return g_string_free (str, FALSE);
}

static void
_open_bookmark (GtkAction *action, gpointer user_data)
{
    VinagreWindow *window = VINAGRE_WINDOW (user_data);
    VinagreConnection *connection = VINAGRE_CONNECTION (g_object_get_data (G_OBJECT (action), "conn"));

    vinagre_cmd_open_bookmark (window, connection);
}

static void
vinagre_window_populate_bookmarks (VinagreWindow *window,
				   const gchar   *group,
				   GSList        *entries,
				   const gchar   *parent)
{
  static guint           i = 0;
  GSList                *l;
  VinagreBookmarksEntry *entry;
  gchar                 *action_name, *action_label, *path, *tooltip;
  GtkAction             *action;
  VinagreWindowPrivate  *p = window->priv;
  VinagreConnection     *conn;
  VinagreProtocol       *ext;

  for (l = entries; l; l = l->next)
    {
      entry = VINAGRE_BOOKMARKS_ENTRY (l->data);
      switch (vinagre_bookmarks_entry_get_node (entry))
	{
	  case VINAGRE_BOOKMARKS_ENTRY_NODE_FOLDER:
	    action_label = _escape_underscores (vinagre_bookmarks_entry_get_name (entry), -1);
	    action_name = g_strdup_printf ("BOOKMARK_FOLDER_ACTION_%d", ++i);
	    action = gtk_action_new (action_name,
				     action_label,
				     NULL,
				     NULL);
	    g_object_set (G_OBJECT (action), "icon-name", "folder", "hide-if-empty", FALSE, NULL);
	    gtk_action_group_add_action (p->bookmarks_list_action_group,
					 action);
	    g_object_unref (action);

	    path = g_strdup_printf ("/MenuBar/BookmarksMenu/%s%s", group, parent?parent:"");
	    gtk_ui_manager_add_ui (p->manager,
				   p->bookmarks_list_menu_ui_id,
				   path,
				   action_label, action_name,
				   GTK_UI_MANAGER_MENU,
				   FALSE);
	    g_free (path);
    	    g_free (action_name);

	    path = g_strdup_printf ("%s/%s", parent?parent:"", action_label);
	    g_free (action_label);

	    vinagre_window_populate_bookmarks (window, group, vinagre_bookmarks_entry_get_children (entry), path);
	    g_free (path);
	    break;

	  case VINAGRE_BOOKMARKS_ENTRY_NODE_CONN:
	    conn = vinagre_bookmarks_entry_get_conn (entry);
	    ext = vinagre_plugins_engine_get_plugin_by_protocol (vinagre_plugins_engine_get_default (),
								 vinagre_connection_get_protocol (conn));
	    if (!ext)
	     continue;

	    action_name = vinagre_connection_get_best_name (conn);
	    action_label = _escape_underscores (action_name, -1);
	    g_free (action_name);

	    action_name = g_strdup_printf ("BOOKMARK_ITEM_ACTION_%d", ++i);

	    /* Translators: This is server:port, a statusbar tooltip when mouse is over a bookmark item on menu */
	    tooltip = g_strdup_printf (_("Open %s:%d"),
					vinagre_connection_get_host (conn),
					vinagre_connection_get_port (conn));
	    action = gtk_action_new (action_name,
				     action_label,
				     tooltip,
				     NULL);
	    g_object_set (G_OBJECT (action),
			  "icon-name",
			  vinagre_protocol_get_icon_name (ext),
			  NULL);
	    g_object_set_data (G_OBJECT (action), "conn", conn);
	    gtk_action_group_add_action (p->bookmarks_list_action_group,
					 action);

	    path = g_strdup_printf ("/MenuBar/BookmarksMenu/%s%s", group, parent?parent:"");
	    gtk_ui_manager_add_ui (p->manager,
				   p->bookmarks_list_menu_ui_id,
				   path,
				   action_label, action_name,
				   GTK_UI_MANAGER_MENUITEM,
				   FALSE);

            g_signal_connect (action, "activate", G_CALLBACK (_open_bookmark), window);

	    g_object_unref (action);
	    g_free (action_name);
	    g_free (action_label);
	    g_free (tooltip);
	    g_free (path);
	    break;

	  default:
	    g_assert_not_reached ();
	}
    }
}

void
vinagre_window_update_bookmarks_list_menu (VinagreWindow *window)
{
  VinagreWindowPrivate *p = window->priv;
  GList  *actions, *l;
  GSList *favs, *mdnss = NULL;

  g_return_if_fail (p->bookmarks_list_action_group != NULL);

  if (p->bookmarks_list_menu_ui_id != 0)
    gtk_ui_manager_remove_ui (p->manager, p->bookmarks_list_menu_ui_id);

  actions = gtk_action_group_list_actions (p->bookmarks_list_action_group);
  for (l = actions; l != NULL; l = l->next)
    {
      gtk_action_group_remove_action (p->bookmarks_list_action_group,
				      GTK_ACTION (l->data));
    }
  g_list_free (actions);

  favs = vinagre_bookmarks_get_all (vinagre_bookmarks_get_default ());

#ifdef VINAGRE_HAVE_AVAHI
  mdnss = vinagre_mdns_get_all (vinagre_mdns_get_default ());
#endif

  p->bookmarks_list_menu_ui_id = (g_slist_length (favs) > 0||g_slist_length (mdnss) > 0) ? gtk_ui_manager_new_merge_id (p->manager) : 0;
  vinagre_window_populate_bookmarks (window, "BookmarksList", favs, NULL);
  vinagre_window_populate_bookmarks (window, "AvahiList", mdnss, NULL);
}

static void
init_widgets_visibility (VinagreWindow *window)
{
  GtkAction *action;
  gboolean visible;

  /* toolbar visibility */
  action = gtk_action_group_get_action (window->priv->always_sensitive_action_group,
					"ViewToolbar");
  visible = vinagre_cache_prefs_get_boolean ("window", "toolbar-visible", TRUE);
  if (gtk_toggle_action_get_active (GTK_TOGGLE_ACTION (action)) != visible)
    gtk_toggle_action_set_active (GTK_TOGGLE_ACTION (action), visible);
  vinagre_utils_set_widget_visible (window->priv->toolbar, visible);

  /* statusbar visibility */
  action = gtk_action_group_get_action (window->priv->always_sensitive_action_group,
					"ViewStatusbar");
  visible = vinagre_cache_prefs_get_boolean ("window", "statusbar-visible", TRUE);
  if (gtk_toggle_action_get_active (GTK_TOGGLE_ACTION (action)) != visible)
    gtk_toggle_action_set_active (GTK_TOGGLE_ACTION (action), visible);
  vinagre_utils_set_widget_visible (window->priv->statusbar, visible);

  gtk_window_set_default_size (GTK_WINDOW (window),
			       vinagre_cache_prefs_get_integer ("window", "window-width", 650),
			       vinagre_cache_prefs_get_integer ("window", "window-height", 500));

  if ((vinagre_cache_prefs_get_integer ("window", "window-state", 0) & GDK_WINDOW_STATE_MAXIMIZED) != 0)
    gtk_window_maximize (GTK_WINDOW (window));
  else
    gtk_window_unmaximize (GTK_WINDOW (window));
}

static void
_create_infobar (VinagreWindow *window, GtkWidget *main_box)
{
    window->priv->infobar = gtk_info_bar_new ();
    gtk_container_add (GTK_CONTAINER (main_box), window->priv->infobar);
}

static void
create_statusbar (VinagreWindow *window, 
		  GtkWidget   *main_box)
{
  window->priv->statusbar = gtk_statusbar_new ();

  window->priv->generic_message_cid = gtk_statusbar_get_context_id
	(GTK_STATUSBAR (window->priv->statusbar), "generic_message");
  window->priv->tip_message_cid = gtk_statusbar_get_context_id
	(GTK_STATUSBAR (window->priv->statusbar), "tip_message");

  gtk_box_pack_end (GTK_BOX (main_box),
		    window->priv->statusbar,
		    FALSE, 
		    TRUE, 
		    0);
}

static void
_create_notebook (VinagreWindow *window, GtkWidget *main_box)
{
    window->priv->notebook = vinagre_notebook_new (window);

    gtk_box_pack_start (GTK_BOX (main_box), GTK_WIDGET (window->priv->notebook),
        TRUE, TRUE, 0);

    gtk_widget_show (GTK_WIDGET (window->priv->notebook));
}

/* Initialise the reverse connections dialog, and start the listener if it is
 * enabled. */
static void
_init_reverse_connections (VinagreWindow *window)
{
    gboolean always;
    VinagreReverseVncListener *listener = window->priv->listener;

    listener = vinagre_reverse_vnc_listener_get_default ();

    vinagre_reverse_vnc_listener_set_window (listener, window);

    g_object_get (vinagre_prefs_get_default (), "always-enable-listening",
        &always, NULL);
    if (always)
        vinagre_reverse_vnc_listener_start (listener);
}

static void
_on_infobar_response (GtkInfoBar *infobar, gint response_id, gpointer user_data)
{
    GtkWindow *window = GTK_WINDOW (user_data);

    switch (response_id)
    {
    case GTK_RESPONSE_CLOSE:
        break;
    case GTK_RESPONSE_HELP:
        vinagre_utils_show_help (window, "keyboard-shortcuts");
        break;
    case GTK_RESPONSE_YES:
        /* Enable keyboard shortcuts. */
        g_settings_set_boolean (vinagre_prefs_get_default_gsettings (), "show-accels", TRUE);
        break;
    default:
        g_assert_not_reached ();
    }

    gtk_widget_hide (GTK_WIDGET (infobar));
}

static gboolean
vinagre_window_check_first_run (VinagreWindow *window)
{
    gchar *dir, *filename;

    dir = vinagre_dirs_get_user_data_dir ();
    filename = g_build_filename (dir, "first_run", NULL);
    g_free (dir);

    if (!g_file_test (filename, G_FILE_TEST_EXISTS))
    {
        GError *error = NULL;
        GtkWidget *infobar = window->priv->infobar;
        GtkWidget *content_area =
            gtk_info_bar_get_content_area (GTK_INFO_BAR (infobar));
        GtkWidget *label =
            gtk_label_new (_("Vinagre disables keyboard shortcuts by default, so that any keyboard shortcuts are sent to the remote desktop.\n\nThis message will appear only once."));

        gtk_label_set_line_wrap (GTK_LABEL (label), TRUE);

        gtk_container_add (GTK_CONTAINER (content_area), label);
        gtk_info_bar_add_buttons (GTK_INFO_BAR (infobar),
            _("Enable shortcuts"), GTK_RESPONSE_YES, GTK_STOCK_CLOSE,
            GTK_RESPONSE_CLOSE, GTK_STOCK_HELP, GTK_RESPONSE_HELP, NULL);
        gtk_info_bar_set_default_response (GTK_INFO_BAR (infobar),
            GTK_RESPONSE_CLOSE);
        g_signal_connect (G_OBJECT (infobar), "response",
            G_CALLBACK (_on_infobar_response), window);

        gtk_widget_show_all (infobar);

        if (vinagre_utils_create_dir_for_file (filename, &error))
        {
            int fd = g_creat (filename, 0644);
            if (fd < 0)
                g_warning (_("Error while creating the file %s: %s"), filename, strerror (errno));
            else
                close (fd);
        }
        else
        {
          g_warning (_("Error while creating the file %s: %s"), filename, error ? error->message: _("Unknown error"));
          g_clear_error (&error);
        }
    }

    g_free (filename);

    return FALSE;
}

static void
protocol_added_removed_cb (VinagrePluginsEngine *engine,
			   VinagreProtocol      *protocol,
			   VinagreWindow        *window)
{
  vinagre_window_update_bookmarks_list_menu (window);
}

static void
vinagre_window_init (VinagreWindow *window)
{
  GtkWidget *main_box;
  VinagrePluginsEngine *engine;

  gtk_window_set_default_icon_name ("vinagre");

  window->priv = VINAGRE_WINDOW_GET_PRIVATE (window);
  window->priv->fullscreen = FALSE;
  window->priv->dispose_has_run = FALSE;

  main_box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
  gtk_container_add (GTK_CONTAINER (window), main_box);
  gtk_widget_show (main_box);

  /* Add menu bar and toolbar bar  */
  create_menu_bar_and_toolbar (window, main_box);

  /* Add info bar. */
  _create_infobar (window, main_box);

  /* Add status bar */
  create_statusbar (window, main_box);

  /* setup notebook area */
  _create_notebook (window, main_box);

  init_widgets_visibility (window);
//  vinagre_window_merge_tab_ui (window);

  vinagre_window_update_bookmarks_list_menu (window);
  g_signal_connect_swapped (vinagre_bookmarks_get_default (),
                            "changed",
                            G_CALLBACK (vinagre_window_update_bookmarks_list_menu),
                            window);
#ifdef VINAGRE_HAVE_AVAHI
  g_signal_connect_swapped (vinagre_mdns_get_default (),
                            "changed",
                            G_CALLBACK (vinagre_window_update_bookmarks_list_menu),
                            window);
#endif

  engine = vinagre_plugins_engine_get_default ();

  g_signal_connect_after (engine,
			  "protocol-added",
			  G_CALLBACK (protocol_added_removed_cb),
			  window);
  g_signal_connect_after (engine,
			  "protocol-removed",
			  G_CALLBACK (protocol_added_removed_cb),
			  window);

  _init_reverse_connections (window);
  g_idle_add ((GSourceFunc) vinagre_window_check_first_run, window);
}

/**
 * vinagre_window_get_notebook:
 * @window: A window
 *
 * Return value: (transfer none):
 */
VinagreNotebook *
vinagre_window_get_notebook (VinagreWindow *window)
{
  g_return_val_if_fail (VINAGRE_IS_WINDOW (window), NULL);

  return window->priv->notebook;
}

void
vinagre_window_close_all_tabs (VinagreWindow *window)
{
  g_return_if_fail (VINAGRE_IS_WINDOW (window));

  vinagre_notebook_close_all_tabs (VINAGRE_NOTEBOOK (window->priv->notebook));
}

void
vinagre_window_set_active_tab (VinagreWindow *window,
			       VinagreTab    *tab)
{
  gint page_num;
	
  g_return_if_fail (VINAGRE_IS_WINDOW (window));
  g_return_if_fail (VINAGRE_IS_TAB (tab));
	
  page_num = gtk_notebook_page_num (GTK_NOTEBOOK (window->priv->notebook),
				    GTK_WIDGET (tab));
  g_return_if_fail (page_num != -1);
	
  gtk_notebook_set_current_page (GTK_NOTEBOOK (window->priv->notebook),
				 page_num);
}

VinagreWindow *
vinagre_window_new ()
{
  return g_object_new (VINAGRE_TYPE_WINDOW,
		       "type",      GTK_WINDOW_TOPLEVEL,
		       "title",     g_get_application_name (),
		       NULL); 
}

gboolean
vinagre_window_is_fullscreen (VinagreWindow *window)
{
  return window->priv->fullscreen;
}

void
vinagre_window_toggle_fullscreen (VinagreWindow *window)
{
  if (window->priv->fullscreen)
    gtk_window_unfullscreen (GTK_WINDOW (window));
  else
    gtk_window_fullscreen (GTK_WINDOW (window));
}

void
vinagre_window_minimize (VinagreWindow *window)
{
  gtk_window_iconify (GTK_WINDOW (window));
}

/**
 * vinagre_window_get_statusbar:
 * @window: A window
 *
 * Return value: (transfer none):
 */
GtkWidget *
vinagre_window_get_statusbar (VinagreWindow *window)
{
  g_return_val_if_fail (VINAGRE_IS_WINDOW (window), NULL);

  return window->priv->statusbar;
}

/**
 * vinagre_window_get_toolbar:
 * @window: A window
 *
 * Return value: (transfer none):
 */
GtkWidget *
vinagre_window_get_toolbar (VinagreWindow *window)
{
  g_return_val_if_fail (VINAGRE_IS_WINDOW (window), NULL);

  return window->priv->toolbar;
}

/**
 * vinagre_window_get_menubar:
 * @window: A window
 *
 * Return value: (transfer none):
 */
GtkWidget *
vinagre_window_get_menubar (VinagreWindow *window)
{
  g_return_val_if_fail (VINAGRE_IS_WINDOW (window), NULL);

  return window->priv->menubar;
}

/**
 * vinagre_window_get_initialized_action:
 * @window: A window
 *
 * Return value: (transfer none):
 */
GtkActionGroup *
vinagre_window_get_initialized_action (VinagreWindow *window)
{
  g_return_val_if_fail (VINAGRE_IS_WINDOW (window), NULL);

  return window->priv->remote_initialized_action_group;
}

/**
 * vinagre_window_get_always_sensitive_action:
 * @window: A window
 *
 * Return value: (transfer none):
 */
GtkActionGroup *
vinagre_window_get_always_sensitive_action (VinagreWindow *window)
{
  g_return_val_if_fail (VINAGRE_IS_WINDOW (window), NULL);

  return window->priv->always_sensitive_action_group;
}

/**
 * vinagre_window_get_connected_action:
 * @window: A window
 *
 * Return value: (transfer none):
 */
GtkActionGroup *
vinagre_window_get_connected_action (VinagreWindow *window)
{
  g_return_val_if_fail (VINAGRE_IS_WINDOW (window), NULL);

  return window->priv->remote_connected_action_group;
}

void
vinagre_window_close_active_tab	(VinagreWindow *window)
{
  g_return_if_fail (VINAGRE_IS_WINDOW (window));

  vinagre_notebook_close_active_tab (window->priv->notebook);
}

/**
 * vinagre_window_get_active_tab:
 * @window: A window
 *
 * Return value: (allow-none) (transfer none):
 */
VinagreTab *
vinagre_window_get_active_tab (VinagreWindow *window)
{
  g_return_val_if_fail (VINAGRE_IS_WINDOW (window), NULL);

  return vinagre_notebook_get_active_tab (window->priv->notebook);
}

/**
 * vinagre_window_get_ui_manager:
 * @window: A window
 *
 * Return value: (transfer none):
 */
GtkUIManager *
vinagre_window_get_ui_manager (VinagreWindow *window)
{
  g_return_val_if_fail (VINAGRE_IS_WINDOW (window), NULL);

  return window->priv->manager;
}

static void
add_connection (VinagreTab *tab, GList **res)
{
  VinagreConnection *conn;
	
  conn = vinagre_tab_get_conn (tab);
	
  *res = g_list_prepend (*res, conn);
}

/**
 * vinagre_window_get_connections: Returns a newly allocated
 * list with all the connections in the window
 * 
 * Return value: (element-type VinagreConnection) (transfer full):
 */
GList *
vinagre_window_get_connections (VinagreWindow *window)
{
  GList *res = NULL;

  g_return_val_if_fail (VINAGRE_IS_WINDOW (window), NULL);
	
  gtk_container_foreach (GTK_CONTAINER (window->priv->notebook),
			 (GtkCallback)add_connection,
			  &res);
			       
  res = g_list_reverse (res);
	
  return res;
}

/**
 * vinagre_window_conn_exists:
 * @window: A window
 *
 * Return value: (allow-none) (transfer none):
 */
VinagreTab *
vinagre_window_conn_exists (VinagreWindow *window, VinagreConnection *conn)
{
  VinagreConnection *c;
  VinagreTab *tab;
  const gchar *host, *protocol, *username;
  gint port;
  GList *conns, *l;

  g_return_val_if_fail (VINAGRE_IS_WINDOW (window), NULL);
  g_return_val_if_fail (VINAGRE_IS_CONNECTION (conn), NULL);

  host = vinagre_connection_get_host (conn);
  username = vinagre_connection_get_username (conn);
  protocol = vinagre_connection_get_protocol (conn);
  port = vinagre_connection_get_port (conn);

  if (!host || !protocol)
    return NULL;

  l = conns = vinagre_window_get_connections (window);
  tab = NULL;

  while (l != NULL)
    {
      c = VINAGRE_CONNECTION (l->data);

      if (!strcmp (host, vinagre_connection_get_host (c)) &&
	  g_strcmp0 (username, vinagre_connection_get_username (c)) == 0 &&
	  !strcmp (protocol, vinagre_connection_get_protocol (c)) &&
	  port == vinagre_connection_get_port (c))
	{
	  tab = g_object_get_data (G_OBJECT (c), VINAGRE_TAB_KEY);
	  break;
	}
      l = l->next;
    }
  g_list_free (conns);

  return tab;
}
