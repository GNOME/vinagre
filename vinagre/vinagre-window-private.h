/*
 * vinagre-window-private.h
 * This file is part of vinagre
 *
 * Copyright (C) 2007,2008 - Jonh Wendell <wendell@bani.com.br>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANWINDOWILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __VINAGRE_WINDOW_PRIVATE_H__
#define __VINAGRE_WINDOW_PRIVATE_H__

#include "vinagre-window.h"
#include "vinagre-notebook.h"
#include "vinagre-reverse-vnc-listener.h"

G_BEGIN_DECLS

/* WindowPrivate is in a separate .h so that we can access it from vinagre-commands */

struct _VinagreWindowPrivate
{
  VinagreNotebook *notebook;
  VinagreReverseVncListener *listener;
  GtkWidget       *infobar;
  GtkWidget       *statusbar;	
  guint           generic_message_cid;
  guint           tip_message_cid;

  /* Menus & Toolbars */
  GtkUIManager   *manager;

  GtkActionGroup *always_sensitive_action_group;
  GtkActionGroup *remote_connected_action_group;
  GtkActionGroup *remote_initialized_action_group;

  GtkActionGroup *bookmarks_list_action_group;
  GtkActionGroup *recent_action_group;
  GtkAction      *recent_action;
  guint           bookmarks_list_menu_ui_id;
  guint           recents_menu_ui_id;
  guint           update_recents_menu_ui_id;

  GtkWidget       *toolbar;
  GtkWidget       *menubar;

  gint            width;
  gint            height;
  GdkWindowState  window_state;

  gboolean        fullscreen;
  gboolean        toolbar_visible;
  gboolean        statusbar_visible;
  gboolean        dispose_has_run;
};


G_END_DECLS

#endif  /* __VINAGRE_WINDOW_PRIVATE_H__  */
