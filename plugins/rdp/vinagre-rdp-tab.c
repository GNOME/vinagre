/*
 * vinagre-rdp-tab.c
 * RDP Implementation for VinagreRdpTab widget
 * This file is part of vinagre
 *
 * Copyright (C) 2010 - Jonh Wendell <wendell@bani.com.br>
 * Copyright (C) 2014 - Marek Kasik <mkasik@redhat.com>
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
#include <errno.h>
#include <glib/gi18n.h>
#include <gdk/gdkkeysyms.h>
#include <math.h>
#include <freerdp/api.h>
#include <freerdp/types.h>
#include <freerdp/freerdp.h>
#include <freerdp/gdi/gdi.h>
#if HAVE_FREERDP_1_1
#include <freerdp/locale/keyboard.h>
#else
#include <freerdp/kbd/vkcodes.h>
#include <gdk/gdkx.h>
#endif

#include "vinagre-rdp-tab.h"
#include "vinagre-rdp-connection.h"
#include "vinagre-vala.h"

#define VINAGRE_RDP_TAB_GET_PRIVATE(object)(G_TYPE_INSTANCE_GET_PRIVATE ((object), VINAGRE_TYPE_RDP_TAB, VinagreRdpTabPrivate))

#define SELECT_TIMEOUT 50

#if !HAVE_FREERDP_1_1
typedef boolean BOOL;
typedef uint8   UINT8;
typedef uint16  UINT16;
#endif

struct _VinagreRdpTabPrivate
{
  freerdp         *freerdp_session;
  GtkWidget       *display;
  cairo_surface_t *surface;
  GQueue          *events;

  guint            update_id;
  guint            button_press_handler_id;
  guint            button_release_handler_id;
  guint            key_press_handler_id;
  guint            key_release_handler_id;
  guint            motion_notify_handler_id;

  GSList          *connected_actions;
  GtkWidget       *scaling_button;
  GtkAction       *scaling_action;
  gboolean         scaling;
  double           scale;
  double           offset_x, offset_y;

  guint            authentication_attempts;
};

G_DEFINE_TYPE (VinagreRdpTab, vinagre_rdp_tab, VINAGRE_TYPE_TAB)

static void open_freerdp (VinagreRdpTab *rdp_tab);
static void setup_toolbar (VinagreRdpTab *rdp_tab);
static void vinagre_rdp_tab_set_scaling (VinagreRdpTab *tab,
                                         gboolean       scaling);
static void scaling_button_clicked (GtkToggleToolButton *button,
                                    VinagreRdpTab       *rdp_tab);

struct frdp_context
{
  rdpContext     context;
  VinagreRdpTab *rdp_tab;
};
typedef struct frdp_context frdpContext;

typedef enum
{
  FRDP_EVENT_TYPE_BUTTON = 0,
  FRDP_EVENT_TYPE_KEY    = 1
} frdpEventType;

typedef struct _frdpEventButton frdpEventButton;
typedef struct _frdpEventKey    frdpEventKey;
typedef union  _frdpEvent       frdpEvent;

struct _frdpEventKey
{
  frdpEventType type;
  UINT16        code;
  BOOL          extended;
  UINT16        flags;
};

struct _frdpEventButton
{
  frdpEventType type;
  UINT16        x;
  UINT16        y;
  UINT16        flags;
};

union _frdpEvent
{
  frdpEventType   type;
  frdpEventKey    key;
  frdpEventButton button;
};

static gchar *
rdp_tab_get_tooltip (VinagreTab *tab)
{
  VinagreConnection *conn = vinagre_tab_get_conn (tab);

  return  g_markup_printf_escaped (
				  "<b>%s</b> %s\n"
				  "<b>%s</b> %d",
				  _("Host:"), vinagre_connection_get_host (conn),
				  _("Port:"), vinagre_connection_get_port (conn));
}

static void
free_frdpEvent (gpointer event,
                G_GNUC_UNUSED gpointer user_data)
{
    g_free (event);
}

static void
view_scaling_cb (GtkAction     *action,
                 VinagreRdpTab *rdp_tab)
{
  VinagreRdpTabPrivate *priv = rdp_tab->priv;
  gboolean              scaling;

  scaling = gtk_toggle_action_get_active (GTK_TOGGLE_ACTION (action));

  vinagre_rdp_tab_set_scaling (rdp_tab, scaling);

  g_signal_handlers_block_by_func (priv->scaling_button, scaling_button_clicked, rdp_tab);
  gtk_toggle_tool_button_set_active (GTK_TOGGLE_TOOL_BUTTON (priv->scaling_button), scaling);
  g_signal_handlers_unblock_by_func (priv->scaling_button, scaling_button_clicked, rdp_tab);
}

const static GSList *
rdp_get_connected_actions (VinagreTab *tab)
{
  VinagreRdpTab *rdp_tab = VINAGRE_RDP_TAB (tab);

  return rdp_tab->priv->connected_actions;
}

static void
vinagre_rdp_tab_dispose (GObject *object)
{
  VinagreRdpTab        *rdp_tab = VINAGRE_RDP_TAB (object);
  VinagreRdpTabPrivate *priv = rdp_tab->priv;

  if (priv->connected_actions)
    {
      vinagre_tab_free_actions (priv->connected_actions);
      priv->connected_actions = NULL;
    }

  if (priv->freerdp_session)
    {
      gdi_free (priv->freerdp_session);
      freerdp_disconnect (priv->freerdp_session);
      freerdp_context_free (priv->freerdp_session);
      g_clear_pointer (&priv->freerdp_session, freerdp_free);
    }

  if (priv->events)
    {
      g_queue_foreach (priv->events, free_frdpEvent, NULL);
      g_clear_pointer (&priv->events, g_queue_free);
    }

  if (priv->update_id > 0)
    {
      g_source_remove (rdp_tab->priv->update_id);
      rdp_tab->priv->update_id = 0;
    }

  if (priv->motion_notify_handler_id > 0)
    {
      g_signal_handler_disconnect (priv->display, priv->motion_notify_handler_id);
      priv->motion_notify_handler_id = 0;
    }

  if (priv->button_press_handler_id > 0)
    {
      g_signal_handler_disconnect (priv->display, priv->button_press_handler_id);
      priv->button_press_handler_id = 0;
    }

  if (priv->button_release_handler_id > 0)
    {
      g_signal_handler_disconnect (priv->display, priv->button_release_handler_id);
      priv->button_release_handler_id = 0;
    }

  if (priv->key_press_handler_id > 0)
    {
      g_signal_handler_disconnect (GTK_WIDGET (object), priv->key_press_handler_id);
      priv->key_press_handler_id = 0;
    }

  if (priv->key_release_handler_id > 0)
    {
      g_signal_handler_disconnect (GTK_WIDGET (object), priv->key_release_handler_id);
      priv->key_release_handler_id = 0;
    }

  G_OBJECT_CLASS (vinagre_rdp_tab_parent_class)->dispose (object);
}

static gboolean
emit_delayed_signal (GObject *object)
{
  g_signal_emit_by_name (object, "tab-initialized");

  return G_SOURCE_REMOVE;
}

static void
vinagre_rdp_tab_constructed (GObject *object)
{
  VinagreRdpTab *rdp_tab = VINAGRE_RDP_TAB (object);

  if (G_OBJECT_CLASS (vinagre_rdp_tab_parent_class)->constructed)
    G_OBJECT_CLASS (vinagre_rdp_tab_parent_class)->constructed (object);

  setup_toolbar (rdp_tab);
  open_freerdp (rdp_tab);

  g_idle_add ((GSourceFunc) emit_delayed_signal, object);
}

static void
vinagre_rdp_tab_class_init (VinagreRdpTabClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  VinagreTabClass* tab_class = VINAGRE_TAB_CLASS (klass);

  object_class->constructed = vinagre_rdp_tab_constructed;
  object_class->dispose = vinagre_rdp_tab_dispose;

  tab_class->impl_get_tooltip = rdp_tab_get_tooltip;
  tab_class->impl_get_connected_actions = rdp_get_connected_actions;

  g_type_class_add_private (object_class, sizeof (VinagreRdpTabPrivate));
}

static gboolean
idle_close (VinagreTab *tab)
{
  vinagre_notebook_close_tab (vinagre_tab_get_notebook (tab), tab);

  return FALSE;
}

static GSList *
create_connected_actions (VinagreRdpTab *tab)
{
  GSList *list = NULL;
  VinagreTabUiAction *action;

  /* View->Scaling */
  action = g_slice_new (VinagreTabUiAction);
  action->paths = g_new (gchar *, 3);
  action->paths[0] = g_strdup ("/MenuBar/ViewMenu");
  action->paths[1] = g_strdup ("/ToolBar");
  action->paths[2] = NULL;
  action->action = GTK_ACTION (gtk_toggle_action_new ("RDPViewScaling",
                                                 _("S_caling"),
                                                 _("Fit the remote screen into the current window size"),
                                                 "zoom-fit-best"));
  gtk_action_set_icon_name (action->action, "zoom-fit-best");
  g_signal_connect (action->action, "activate", G_CALLBACK (view_scaling_cb), tab);
  list = g_slist_append (list, action);
  tab->priv->scaling_action = action->action;

  return list;
}

static void
scaling_button_clicked (GtkToggleToolButton *button,
                        VinagreRdpTab       *rdp_tab)
{
  vinagre_rdp_tab_set_scaling (rdp_tab,
                               gtk_toggle_tool_button_get_active (button));
}

static void
vinagre_rdp_tab_set_scaling (VinagreRdpTab *tab,
                             gboolean       scaling)
{
  VinagreRdpTabPrivate *priv = tab->priv;
  VinagreConnection    *conn = vinagre_tab_get_conn (VINAGRE_TAB (tab));
  GtkWidget            *scrolled;
  gint                  window_width, window_height;

  priv->scaling = scaling;

  gtk_toggle_action_set_active (GTK_TOGGLE_ACTION (priv->scaling_action),
                                priv->scaling);

  if (scaling)
    {
      scrolled = gtk_widget_get_ancestor (priv->display, GTK_TYPE_SCROLLED_WINDOW);
      window_width = gtk_widget_get_allocated_width (scrolled);
      window_height = gtk_widget_get_allocated_height (scrolled);

      gtk_widget_set_size_request (priv->display,
                                   window_width,
                                   window_height);

      gtk_widget_set_halign (priv->display, GTK_ALIGN_FILL);
      gtk_widget_set_valign (priv->display, GTK_ALIGN_FILL);
    }
  else
    {
      gtk_widget_set_size_request (priv->display,
                                   vinagre_connection_get_width (VINAGRE_CONNECTION (conn)),
                                   vinagre_connection_get_height (VINAGRE_CONNECTION (conn)));
      gtk_widget_set_halign (priv->display, GTK_ALIGN_CENTER);
      gtk_widget_set_valign (priv->display, GTK_ALIGN_CENTER);
    }

  gtk_widget_queue_draw_area (priv->display, 0, 0,
                              gtk_widget_get_allocated_width (priv->display),
                              gtk_widget_get_allocated_height (priv->display));
}

static void
setup_toolbar (VinagreRdpTab *rdp_tab)
{
  GtkWidget *toolbar = vinagre_tab_get_toolbar (VINAGRE_TAB (rdp_tab));
  GtkWidget *button;

  /* Space */
  button = GTK_WIDGET (gtk_separator_tool_item_new ());
  gtk_tool_item_set_expand (GTK_TOOL_ITEM (button), TRUE);
  gtk_widget_show (GTK_WIDGET (button));
  gtk_toolbar_insert (GTK_TOOLBAR (toolbar), GTK_TOOL_ITEM (button), -1);

  /* Scaling */
  button = GTK_WIDGET (gtk_toggle_tool_button_new ());
  gtk_tool_button_set_label (GTK_TOOL_BUTTON (button), _("Scaling"));
  gtk_tool_item_set_tooltip_text (GTK_TOOL_ITEM (button), _("Scaling"));
  gtk_tool_button_set_icon_name (GTK_TOOL_BUTTON (button), "zoom-fit-best");
  gtk_widget_show (GTK_WIDGET (button));
  gtk_toolbar_insert (GTK_TOOLBAR (toolbar), GTK_TOOL_ITEM (button), -1);
  g_signal_connect (button, "toggled", G_CALLBACK (scaling_button_clicked), rdp_tab);
  rdp_tab->priv->scaling_button = button;
}

static void
frdp_process_events (freerdp *instance,
                     GQueue  *events)
{
  VinagreRdpTab        *rdp_tab = ((frdpContext *) instance->context)->rdp_tab;
  VinagreRdpTabPrivate *priv = rdp_tab->priv;
  frdpEvent            *event;
  gint                  x, y;

  while (!g_queue_is_empty (events))
    {
      event = g_queue_pop_head (events);
      if (event != NULL)
        {
          switch (event->type)
            {
              case FRDP_EVENT_TYPE_KEY:
                instance->input->KeyboardEvent (instance->input,
                                                ((frdpEventKey *) event)->flags,
                                                ((frdpEventKey *) event)->code);
                break;
              case FRDP_EVENT_TYPE_BUTTON:
                if (priv->scaling)
                  {
                    x = (((frdpEventButton *) event)->x - priv->offset_x) / priv->scale;
                    y = (((frdpEventButton *) event)->y - priv->offset_y) / priv->scale;
                  }
                else
                  {
                    x = ((frdpEventButton *) event)->x;
                    y = ((frdpEventButton *) event)->y;
                  }

                if (x < 0)
                  x = 0;

                if (y < 0)
                  y = 0;

                instance->input->MouseEvent (instance->input,
                                             ((frdpEventButton *) event)->flags,
                                             x,
                                             y);
                break;
              default:
                break;
            }

          g_free (event);
        }
    }
}

static gboolean
frdp_drawing_area_draw (GtkWidget *area,
                        cairo_t   *cr,
                        gpointer   user_data)
{
  VinagreRdpTab        *rdp_tab = (VinagreRdpTab *) user_data;
  VinagreRdpTabPrivate *priv = rdp_tab->priv;
  VinagreRdpConnection *conn = VINAGRE_RDP_CONNECTION (vinagre_tab_get_conn (VINAGRE_TAB (rdp_tab)));
  GtkWidget            *scrolled;
  double                scale_x, scale_y;
  gint                  window_width, window_height;

  if (priv->surface == NULL)
    return FALSE;

  if (priv->scaling)
    {
      scrolled = gtk_widget_get_ancestor (area, GTK_TYPE_SCROLLED_WINDOW);
      window_width = gtk_widget_get_allocated_width (scrolled);
      window_height = gtk_widget_get_allocated_height (scrolled);

      scale_x = (double) window_width / vinagre_connection_get_width (VINAGRE_CONNECTION (conn));
      scale_y = (double) window_height / vinagre_connection_get_height (VINAGRE_CONNECTION (conn));

      priv->scale = scale_x < scale_y ? scale_x : scale_y;

      priv->offset_x = (window_width - vinagre_connection_get_width (VINAGRE_CONNECTION (conn)) * priv->scale) / 2.0;
      priv->offset_y = (window_height - vinagre_connection_get_height (VINAGRE_CONNECTION (conn)) * priv->scale) / 2.0;

      if (priv->offset_x < 0)
        priv->offset_x = 0;

      if (priv->offset_y < 0)
        priv->offset_y = 0;

      cairo_translate (cr, priv->offset_x, priv->offset_y);
      cairo_scale (cr, priv->scale, priv->scale);

      if (window_width != gtk_widget_get_allocated_width (area) ||
          window_height != gtk_widget_get_allocated_height (area))
        gtk_widget_set_size_request (area,
                                     window_width,
                                     window_height);
    }

  cairo_set_source_surface (cr, priv->surface, 0, 0);
  cairo_paint (cr);

  return TRUE;
}

static void
frdp_begin_paint (rdpContext *context)
{
  rdpGdi *gdi = context->gdi;

  gdi->primary->hdc->hwnd->invalid->null = 1;
  gdi->primary->hdc->hwnd->ninvalid = 0;
}

static void
frdp_end_paint (rdpContext *context)
{
  VinagreRdpTab        *rdp_tab = ((frdpContext *) context)->rdp_tab;
  VinagreRdpTabPrivate *priv = rdp_tab->priv;
  rdpGdi               *gdi = context->gdi;
  double                pos_x, pos_y;
  gint                  x, y, w, h;

  if (gdi->primary->hdc->hwnd->invalid->null)
    return;

  x = gdi->primary->hdc->hwnd->invalid->x;
  y = gdi->primary->hdc->hwnd->invalid->y;
  w = gdi->primary->hdc->hwnd->invalid->w;
  h = gdi->primary->hdc->hwnd->invalid->h;

  if (priv->scaling)
    {
      pos_x = priv->offset_x + x * priv->scale;
      pos_y = priv->offset_y + y * priv->scale;

      gtk_widget_queue_draw_area (priv->display,
                                  floor (pos_x),
                                  floor (pos_y),
                                  ceil (pos_x + w * priv->scale) - floor (pos_x),
                                  ceil (pos_y + h * priv->scale) - floor (pos_y));
    }
  else
    {
      gtk_widget_queue_draw_area (priv->display, x, y, w, h);
    }
}

static BOOL
frdp_pre_connect (freerdp *instance)
{
  rdpSettings *settings = instance->settings;

#if HAVE_FREERDP_1_1
  settings->OrderSupport[NEG_DSTBLT_INDEX] = TRUE;
  settings->OrderSupport[NEG_PATBLT_INDEX] = TRUE;
  settings->OrderSupport[NEG_SCRBLT_INDEX] = TRUE;
  settings->OrderSupport[NEG_OPAQUE_RECT_INDEX] = TRUE;
  settings->OrderSupport[NEG_DRAWNINEGRID_INDEX] = FALSE;
  settings->OrderSupport[NEG_MULTIDSTBLT_INDEX] = FALSE;
  settings->OrderSupport[NEG_MULTIPATBLT_INDEX] = FALSE;
  settings->OrderSupport[NEG_MULTISCRBLT_INDEX] = FALSE;
  settings->OrderSupport[NEG_MULTIOPAQUERECT_INDEX] = TRUE;
  settings->OrderSupport[NEG_MULTI_DRAWNINEGRID_INDEX] = FALSE;
  settings->OrderSupport[NEG_LINETO_INDEX] = TRUE;
  settings->OrderSupport[NEG_POLYLINE_INDEX] = TRUE;
  settings->OrderSupport[NEG_MEMBLT_INDEX] = TRUE;
  settings->OrderSupport[NEG_MEM3BLT_INDEX] = FALSE;
  settings->OrderSupport[NEG_MEMBLT_V2_INDEX] = TRUE;
  settings->OrderSupport[NEG_MEM3BLT_V2_INDEX] = FALSE;
  settings->OrderSupport[NEG_SAVEBITMAP_INDEX] = FALSE;
  settings->OrderSupport[NEG_GLYPH_INDEX_INDEX] = TRUE;
  settings->OrderSupport[NEG_FAST_INDEX_INDEX] = TRUE;
  settings->OrderSupport[NEG_FAST_GLYPH_INDEX] = FALSE;
  settings->OrderSupport[NEG_POLYGON_SC_INDEX] = FALSE;
  settings->OrderSupport[NEG_POLYGON_CB_INDEX] = FALSE;
  settings->OrderSupport[NEG_ELLIPSE_SC_INDEX] = FALSE;
  settings->OrderSupport[NEG_ELLIPSE_CB_INDEX] = FALSE;
#else
  settings->order_support[NEG_DSTBLT_INDEX] = true;
  settings->order_support[NEG_PATBLT_INDEX] = true;
  settings->order_support[NEG_SCRBLT_INDEX] = true;
  settings->order_support[NEG_OPAQUE_RECT_INDEX] = true;
  settings->order_support[NEG_DRAWNINEGRID_INDEX] = false;
  settings->order_support[NEG_MULTIDSTBLT_INDEX] = false;
  settings->order_support[NEG_MULTIPATBLT_INDEX] = false;
  settings->order_support[NEG_MULTISCRBLT_INDEX] = false;
  settings->order_support[NEG_MULTIOPAQUERECT_INDEX] = true;
  settings->order_support[NEG_MULTI_DRAWNINEGRID_INDEX] = false;
  settings->order_support[NEG_LINETO_INDEX] = true;
  settings->order_support[NEG_POLYLINE_INDEX] = true;
  settings->order_support[NEG_MEMBLT_INDEX] = true;
  settings->order_support[NEG_MEM3BLT_INDEX] = false;
  settings->order_support[NEG_MEMBLT_V2_INDEX] = true;
  settings->order_support[NEG_MEM3BLT_V2_INDEX] = false;
  settings->order_support[NEG_SAVEBITMAP_INDEX] = false;
  settings->order_support[NEG_GLYPH_INDEX_INDEX] = true;
  settings->order_support[NEG_FAST_INDEX_INDEX] = true;
  settings->order_support[NEG_FAST_GLYPH_INDEX] = false;
  settings->order_support[NEG_POLYGON_SC_INDEX] = false;
  settings->order_support[NEG_POLYGON_CB_INDEX] = false;
  settings->order_support[NEG_ELLIPSE_SC_INDEX] = false;
  settings->order_support[NEG_ELLIPSE_CB_INDEX] = false;
#endif

  return TRUE;
}

static BOOL
frdp_post_connect (freerdp *instance)
{
  VinagreRdpTab        *rdp_tab = ((frdpContext *) instance->context)->rdp_tab;
  VinagreRdpTabPrivate *priv = rdp_tab->priv;
  rdpGdi               *gdi;
  int                   stride;

  gdi_init (instance,
#if defined(FREERDP_VERSION_MAJOR) && defined(FREERDP_VERSION_MINOR) && \
    !(FREERDP_VERSION_MAJOR > 1 || (FREERDP_VERSION_MAJOR == 1 && \
    FREERDP_VERSION_MINOR >= 2))
                    CLRBUF_24BPP,
#else
                    CLRBUF_32BPP,
#endif
                    NULL);
  gdi = instance->context->gdi;

  instance->update->BeginPaint = frdp_begin_paint;
  instance->update->EndPaint = frdp_end_paint;

  stride = cairo_format_stride_for_width (CAIRO_FORMAT_RGB24, gdi->width);
  rdp_tab->priv->surface = cairo_image_surface_create_for_data ((unsigned char*) gdi->primary_buffer,
                                                                CAIRO_FORMAT_RGB24,
                                                                gdi->width,
                                                                gdi->height,
                                                                stride);
  gtk_widget_queue_draw_area (priv->display,
                              0, 0,
                              gdi->width, gdi->height);

  vinagre_tab_save_credentials_in_keyring (VINAGRE_TAB (rdp_tab));
  vinagre_tab_add_recent_used (VINAGRE_TAB (rdp_tab));
  vinagre_tab_set_state (VINAGRE_TAB (rdp_tab), VINAGRE_TAB_STATE_CONNECTED);

  return TRUE;
}

static gboolean
update (gpointer user_data)
{
  VinagreRdpTab        *rdp_tab = (VinagreRdpTab *) user_data;
  VinagreRdpTabPrivate *priv = rdp_tab->priv;
  struct timeval        timeout;
  fd_set                rfds_set;
  fd_set                wfds_set;
  void                 *rfds[32];
  void                 *wfds[32];
  int                   i;
  int                   fds;
  int                   max_fds;
  int                   rcount = 0;
  int                   wcount = 0;
  int                   result;

  memset (rfds, 0, sizeof (rfds));
  memset (wfds, 0, sizeof (wfds));

  if (!freerdp_get_fds (priv->freerdp_session,
                        rfds, &rcount,
                        wfds, &wcount))
    {
      g_warning ("Failed to get FreeRDP file descriptor\n");
      return FALSE;
    }

  max_fds = 0;
  FD_ZERO (&rfds_set);
  FD_ZERO (&wfds_set);

  for (i = 0; i < rcount; i++)
    {
      fds = (int)(long) (rfds[i]);

      if (fds > max_fds)
        max_fds = fds;

      FD_SET (fds, &rfds_set);
    }

  if (max_fds == 0)
    return FALSE;

  timeout.tv_sec = 0;
  timeout.tv_usec = SELECT_TIMEOUT;

  result = select (max_fds + 1, &rfds_set, NULL, NULL, &timeout);
  if (result == -1)
    {
      /* these are not errors */
      if (!((errno == EAGAIN) ||
            (errno == EWOULDBLOCK) ||
            (errno == EINPROGRESS) ||
            (errno == EINTR))) /* signal occurred */
      {
        g_warning ("update: select failed\n");
        return FALSE;
      }
    }

  if (!freerdp_check_fds (priv->freerdp_session))
    {
      g_warning ("Failed to check FreeRDP file descriptor\n");
      return FALSE;
    }

  frdp_process_events (priv->freerdp_session, priv->events);

  if (freerdp_shall_disconnect (priv->freerdp_session))
    {
      g_idle_add ((GSourceFunc) idle_close, rdp_tab);
      return FALSE;
    }

  return TRUE;
}

static gboolean
frdp_key_pressed (GtkWidget   *widget,
                  GdkEventKey *event,
                  gpointer     user_data)
{
  VinagreRdpTab        *rdp_tab = (VinagreRdpTab *) user_data;
  VinagreRdpTabPrivate *priv = rdp_tab->priv;
  frdpEventKey         *frdp_event;
#if HAVE_FREERDP_1_1
  UINT16                scancode;
#endif

  frdp_event = g_new0 (frdpEventKey, 1);
  frdp_event->type = FRDP_EVENT_TYPE_KEY;
  frdp_event->flags = event->type == GDK_KEY_PRESS ? KBD_FLAGS_DOWN : KBD_FLAGS_RELEASE;

#if HAVE_FREERDP_1_1
  scancode = freerdp_keyboard_get_rdp_scancode_from_x11_keycode (event->hardware_keycode);
  frdp_event->code = RDP_SCANCODE_CODE(scancode);
  frdp_event->extended = RDP_SCANCODE_EXTENDED(scancode);
#else
  frdp_event->code = freerdp_kbd_get_scancode_by_keycode (event->hardware_keycode, &frdp_event->extended);
#endif

  if (frdp_event->extended)
    frdp_event->flags |= KBD_FLAGS_EXTENDED;

  g_queue_push_tail (priv->events, frdp_event);

  return TRUE;
}

static gboolean
frdp_button_pressed (GtkWidget      *widget,
                     GdkEventButton *event,
                     gpointer        user_data)
{
  VinagreRdpTab        *rdp_tab = (VinagreRdpTab *) user_data;
  VinagreRdpTabPrivate *priv = rdp_tab->priv;
  frdpEventButton      *frdp_event;

  frdp_event = g_new0 (frdpEventButton, 1);

  frdp_event->type = FRDP_EVENT_TYPE_BUTTON;

  switch (event->button)
    {
      case 1:
        frdp_event->flags = PTR_FLAGS_BUTTON1;
        break;

      case 2:
        frdp_event->flags = PTR_FLAGS_BUTTON3;
        break;

      case 3:
        frdp_event->flags = PTR_FLAGS_BUTTON2;
        break;
    }

  if (frdp_event->flags != 0)
    {
      frdp_event->flags |= event->type == GDK_BUTTON_PRESS ? PTR_FLAGS_DOWN : 0;

      frdp_event->x = event->x < 0.0 ? 0.0 : event->x;
      frdp_event->y = event->y < 0.0 ? 0.0 : event->y;

      g_queue_push_tail (priv->events, frdp_event);
    }
  else
    {
      g_free (frdp_event);
    }

  return TRUE;
}

static gboolean
frdp_scroll (GtkWidget      *widget,
             GdkEventScroll *event,
             gpointer        user_data)
{
  VinagreRdpTab        *rdp_tab = (VinagreRdpTab *) user_data;
  VinagreRdpTabPrivate *priv = rdp_tab->priv;
  frdpEventButton      *frdp_event;
  gdouble               delta_x = 0.0;
  gdouble               delta_y = 0.0;

  frdp_event = g_new0 (frdpEventButton, 1);
  frdp_event->type = FRDP_EVENT_TYPE_BUTTON;

  frdp_event->flags = 0;
  /* http://msdn.microsoft.com/en-us/library/cc240586.aspx (section 2.2.8.1.1.3.1.1.3) */
  switch (event->direction)
    {
      case GDK_SCROLL_UP:
        frdp_event->flags = PTR_FLAGS_WHEEL;
        frdp_event->flags |= 0x0078;
        break;

      case GDK_SCROLL_DOWN:
        frdp_event->flags = PTR_FLAGS_WHEEL;
        frdp_event->flags |= PTR_FLAGS_WHEEL_NEGATIVE;
        frdp_event->flags |= 0x0088;
        break;

      case GDK_SCROLL_SMOOTH:
        if (gdk_event_get_scroll_deltas ((GdkEvent *) event, &delta_x, &delta_y))
          {
            if (delta_y != 0.0)
              {
                frdp_event->flags = PTR_FLAGS_WHEEL;
                if (delta_y < 0.0)
                  {
                    frdp_event->flags |= 0x0078;
                  }
                else
                  {
                    frdp_event->flags |= PTR_FLAGS_WHEEL_NEGATIVE;
                    frdp_event->flags |= 0x0088;
                  }
              }
          }
        break;

      default:
        break;
    }

  if (frdp_event->flags != 0)
    {
      frdp_event->x = event->x < 0.0 ? 0.0 : event->x;
      frdp_event->y = event->y < 0.0 ? 0.0 : event->y;

      g_queue_push_tail (priv->events, frdp_event);
    }
  else
    {
      g_free (frdp_event);
    }

  return TRUE;
}

static gboolean
frdp_mouse_moved (GtkWidget      *widget,
                  GdkEventButton *event,
                  gpointer        user_data)
{
  VinagreRdpTab        *rdp_tab = (VinagreRdpTab *) user_data;
  VinagreRdpTabPrivate *priv = rdp_tab->priv;
  frdpEventButton      *frdp_event;

  frdp_event = g_new0 (frdpEventButton, 1);

  frdp_event->type = FRDP_EVENT_TYPE_BUTTON;
  frdp_event->flags = PTR_FLAGS_MOVE;
  frdp_event->x = event->x < 0.0 ? 0.0 : event->x;
  frdp_event->y = event->y < 0.0 ? 0.0 : event->y;

  g_queue_push_tail (priv->events, frdp_event);

  return TRUE;
}

static gboolean
frdp_authenticate (freerdp  *instance,
                   char    **username,
                   char    **password,
                   char    **domain)
{
  VinagreTab           *tab = VINAGRE_TAB (((frdpContext *) instance->context)->rdp_tab);
  VinagreRdpTab        *rdp_tab = VINAGRE_RDP_TAB (tab);
  VinagreRdpTabPrivate *priv = rdp_tab->priv;
  VinagreConnection    *conn = vinagre_tab_get_conn (tab);
  GtkWindow            *window = GTK_WINDOW (vinagre_tab_get_window (tab));
  gboolean              save_in_keyring = FALSE;
  gchar                *keyring_domain = NULL;
  gchar                *keyring_username = NULL;
  gchar                *keyring_password = NULL;

  priv->authentication_attempts++;

  if (priv->authentication_attempts == 1)
    {
      vinagre_tab_find_credentials_in_keyring (tab, &keyring_domain, &keyring_username, &keyring_password);
      if (keyring_password != NULL && keyring_username != NULL)
        {
          *domain = keyring_domain;
          *username = keyring_username;
          *password = keyring_password;

          return TRUE;
        }
      else
        {
          g_free (keyring_domain);
          g_free (keyring_username);
          g_free (keyring_password);
        }
    }

  if (vinagre_utils_request_credential (window,
                                        "RDP",
                                        vinagre_connection_get_host (conn),
                                        vinagre_connection_get_domain (conn),
                                        vinagre_connection_get_username (conn),
                                        TRUE,
                                        TRUE,
                                        TRUE,
                                        20,
                                        domain,
                                        username,
                                        password,
                                        &save_in_keyring))
    {
      if (*domain && **domain != '\0')
        vinagre_connection_set_domain (conn, *domain);

      if (*username && **username != '\0')
        vinagre_connection_set_username (conn, *username);

      if (*password && **password != '\0')
        vinagre_connection_set_password (conn, *password);

      vinagre_tab_set_save_credentials (tab, save_in_keyring);
    }
  else
    {
      vinagre_tab_remove_from_notebook (tab);

      return FALSE;
    }

  return TRUE;
}

static BOOL
frdp_certificate_verify (freerdp *instance,
                         char    *subject,
                         char    *issuer,
                         char    *fingerprint)
{
  VinagreTab *tab = VINAGRE_TAB (((frdpContext *) instance->context)->rdp_tab);
  GtkBuilder *builder;
  GtkWidget  *dialog;
  GtkWidget  *widget;
  gint        response;

  builder = vinagre_utils_get_builder ();

  dialog = GTK_WIDGET (gtk_builder_get_object (builder, "certificate_dialog"));
  gtk_window_set_transient_for ((GtkWindow *) dialog, GTK_WINDOW (vinagre_tab_get_window (tab)));
  gtk_dialog_add_buttons (GTK_DIALOG (dialog), _("_Cancel"), GTK_RESPONSE_NO,
                          _("Connect"), GTK_RESPONSE_YES, NULL);
  gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_YES);

  widget = GTK_WIDGET (gtk_builder_get_object (builder, "certificate_subject"));
  gtk_label_set_text (GTK_LABEL (widget), subject);

  widget = GTK_WIDGET (gtk_builder_get_object (builder, "certificate_issuer"));
  gtk_label_set_text (GTK_LABEL (widget), issuer);

  widget = GTK_WIDGET (gtk_builder_get_object (builder, "certificate_fingerprint"));
  gtk_label_set_text (GTK_LABEL (widget), fingerprint);


  response = gtk_dialog_run (GTK_DIALOG (dialog));
  gtk_widget_hide (dialog);


  return response == GTK_RESPONSE_YES;
}


#if HAVE_FREERDP_1_1
static BOOL
frdp_changed_certificate_verify (freerdp *instance,
                                 char    *subject,
                                 char    *issuer,
                                 char    *new_fingerprint,
                                 char    *old_fingerprint)
{
  VinagreTab *tab = VINAGRE_TAB (((frdpContext *) instance->context)->rdp_tab);
  GtkBuilder *builder;
  GtkWidget  *dialog;
  GtkWidget  *widget;
  GtkWidget  *label;
  gint        response;

  builder = vinagre_utils_get_builder ();

  dialog = GTK_WIDGET (gtk_builder_get_object (builder, "certificate_changed_dialog"));
  gtk_window_set_transient_for ((GtkWindow *) dialog, GTK_WINDOW (vinagre_tab_get_window (tab)));
  gtk_dialog_add_buttons (GTK_DIALOG (dialog), _("_Cancel"), GTK_RESPONSE_NO,
                          _("Connect"), GTK_RESPONSE_YES, NULL);
  gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_YES);

  widget = GTK_WIDGET (gtk_builder_get_object (builder, "certificate_changed_subject"));
  gtk_label_set_text (GTK_LABEL (widget), subject);

  widget = GTK_WIDGET (gtk_builder_get_object (builder, "certificate_changed_issuer"));
  gtk_label_set_text (GTK_LABEL (widget), issuer);

  widget = GTK_WIDGET (gtk_builder_get_object (builder, "certificate_changed_new_fingerprint"));
  gtk_label_set_text (GTK_LABEL (widget), new_fingerprint);

  widget = GTK_WIDGET (gtk_builder_get_object (builder, "certificate_changed_old_fingerprint"));
  label = GTK_WIDGET (gtk_builder_get_object (builder, "certificate_changed_old_fingerprint_label"));
  if (old_fingerprint != NULL && old_fingerprint[0] != '\0')
    {
      gtk_label_set_text (GTK_LABEL (widget), old_fingerprint);
      gtk_widget_show (widget);
      gtk_widget_show (label);
    }
  else
    {
      gtk_widget_hide (widget);
      gtk_widget_hide (label);
    }


  response = gtk_dialog_run (GTK_DIALOG (dialog));
  gtk_widget_hide (dialog);


  return response == GTK_RESPONSE_YES;
}
#endif

static void
init_freerdp (VinagreRdpTab *rdp_tab)
{
  VinagreRdpTabPrivate *priv = rdp_tab->priv;
  rdpSettings          *settings;
  VinagreTab           *tab = VINAGRE_TAB (rdp_tab);
  VinagreConnection    *conn = vinagre_tab_get_conn (tab);
  gboolean              scaling;
  gchar                *hostname;
  gint                  width, height;
  gint                  port;

  g_object_get (conn,
                "port", &port,
                "host", &hostname,
                "width", &width,
                "height", &height,
                "scaling", &scaling,
                NULL);

  /* Setup FreeRDP session */
  priv->freerdp_session = freerdp_new ();
  priv->freerdp_session->PreConnect = frdp_pre_connect;
  priv->freerdp_session->PostConnect = frdp_post_connect;
  priv->freerdp_session->Authenticate = frdp_authenticate;
  priv->freerdp_session->VerifyCertificate = frdp_certificate_verify;
#if HAVE_FREERDP_1_1
  priv->freerdp_session->VerifyChangedCertificate = frdp_changed_certificate_verify;
#endif

#if HAVE_FREERDP_1_1
  priv->freerdp_session->ContextSize = sizeof (frdpContext);
#else
  priv->freerdp_session->context_size = sizeof (frdpContext);
#endif

  freerdp_context_new (priv->freerdp_session);
  ((frdpContext *) priv->freerdp_session->context)->rdp_tab = rdp_tab;

  /* Set FreeRDP settings */
  settings = priv->freerdp_session->settings;

  /* Security settings */
#if HAVE_FREERDP_1_1
  settings->RdpSecurity = TRUE;
  settings->TlsSecurity = TRUE;
  settings->NlaSecurity = TRUE;
  settings->EncryptionMethods = ENCRYPTION_METHOD_40BIT | ENCRYPTION_METHOD_128BIT | ENCRYPTION_METHOD_FIPS;
  settings->EncryptionLevel = ENCRYPTION_LEVEL_CLIENT_COMPATIBLE;
#else
  settings->rdp_security = true;
  settings->tls_security = true;
  settings->nla_security = true;
  settings->encryption = true;
  settings->encryption_method = ENCRYPTION_METHOD_40BIT | ENCRYPTION_METHOD_128BIT | ENCRYPTION_METHOD_FIPS;
  settings->encryption_level = ENCRYPTION_LEVEL_CLIENT_COMPATIBLE;
#endif
#include <freerdp/version.h>
#if (FREERDP_VERSION_MAJOR == 1 && FREERDP_VERSION_MINOR >= 2 && FREERDP_VERSION_REVISION >= 1) || (FREERDP_VERSION_MAJOR == 2)
  settings->UseRdpSecurityLayer = FALSE;
#else
  settings->DisableEncryption = FALSE;
#endif

  /* Set display size */
#if HAVE_FREERDP_1_1
  settings->DesktopWidth = width;
  settings->DesktopHeight = height;
#else
  settings->width = width;
  settings->height = height;
#endif

  /* Set hostname */
#if HAVE_FREERDP_1_1
  settings->WindowTitle = g_strdup (hostname);
  settings->ServerHostname = g_strdup (hostname);
  settings->ServerPort = port;
#else
  settings->window_title = g_strdup (hostname);
  settings->hostname = g_strdup (hostname);
  settings->port = port;
#endif

  /* Set keyboard layout */
#if HAVE_FREERDP_1_1
  freerdp_keyboard_init (KBD_US);
#else
  freerdp_kbd_init (GDK_DISPLAY_XDISPLAY (gdk_display_get_default ()), KBD_US);
#endif

  /* Allow font smoothing by default */
  settings->AllowFontSmoothing = TRUE;
}

static void
init_display (VinagreRdpTab *rdp_tab)
{
  VinagreRdpTabPrivate *priv = rdp_tab->priv;
  VinagreTab           *tab = VINAGRE_TAB (rdp_tab);
  VinagreConnection    *conn = vinagre_tab_get_conn (tab);
  GtkWindow            *window = GTK_WINDOW (vinagre_tab_get_window (tab));
  gboolean              fullscreen, scaling;
  gint                  width, height;

  g_object_get (conn,
                "width", &width,
                "height", &height,
                "fullscreen", &fullscreen,
                "scaling", &scaling,
                NULL);

  /* Setup display for FreeRDP session */
  priv->display = gtk_drawing_area_new ();
  if (priv->display)
    {
      g_signal_connect (priv->display, "draw",
                        G_CALLBACK (frdp_drawing_area_draw), rdp_tab);

      gtk_widget_add_events (priv->display,
                             GDK_POINTER_MOTION_MASK |
                             GDK_BUTTON_PRESS_MASK |
                             GDK_BUTTON_RELEASE_MASK |
                             GDK_SCROLL_MASK |
                             GDK_SMOOTH_SCROLL_MASK);

      priv->button_press_handler_id = g_signal_connect (priv->display, "button-press-event",
                                                        G_CALLBACK (frdp_button_pressed),
                                                        rdp_tab);

      priv->button_release_handler_id = g_signal_connect (priv->display, "button-release-event",
                                                          G_CALLBACK (frdp_button_pressed),
                                                          rdp_tab);

      priv->button_release_handler_id = g_signal_connect (priv->display, "scroll-event",
                                                          G_CALLBACK (frdp_scroll),
                                                          rdp_tab);

      priv->motion_notify_handler_id = g_signal_connect (priv->display, "motion-notify-event",
                                                         G_CALLBACK (frdp_mouse_moved),
                                                         rdp_tab);

      gtk_widget_show (priv->display);

      vinagre_tab_add_view (VINAGRE_TAB (rdp_tab), priv->display);

      if (fullscreen)
        gtk_window_fullscreen (window);

      vinagre_rdp_tab_set_scaling (rdp_tab, scaling);
    }

  priv->key_press_handler_id = g_signal_connect (GTK_WIDGET (tab), "key-press-event",
                                                 G_CALLBACK (frdp_key_pressed),
                                                 rdp_tab);

  priv->key_release_handler_id = g_signal_connect (GTK_WIDGET (tab), "key-release-event",
                                                   G_CALLBACK (frdp_key_pressed),
                                                   rdp_tab);
}

static void
open_freerdp (VinagreRdpTab *rdp_tab)
{
  VinagreRdpTabPrivate *priv = rdp_tab->priv;
  VinagreTab           *tab = VINAGRE_TAB (rdp_tab);
  GtkWindow            *window = GTK_WINDOW (vinagre_tab_get_window (tab));
  gboolean              success = TRUE;
  gboolean              cancelled = FALSE;
  guint                 authentication_errors = 0;

  priv->events = g_queue_new ();

  init_freerdp (rdp_tab);
  init_display (rdp_tab);

  do
    {
      /* Run FreeRDP session */
      success = freerdp_connect (priv->freerdp_session);
      if (!success)
        {
          authentication_errors += freerdp_get_last_error (priv->freerdp_session->context) == 0x20009 ||
                                   freerdp_get_last_error (priv->freerdp_session->context) == 0x2000c;

          cancelled = freerdp_get_last_error (priv->freerdp_session->context) == 0x2000b;

          freerdp_free (priv->freerdp_session);
          init_freerdp (rdp_tab);
        }
    }
  while (!success && !cancelled && authentication_errors < 3);

  if (!success)
    {
      gtk_window_unfullscreen (window);
      if (!cancelled)
        vinagre_utils_show_error_dialog (_("Error connecting to host."),
                                         NULL,
                                         window);
      g_idle_add ((GSourceFunc) idle_close, rdp_tab);
    }
  else
    {
      priv->authentication_attempts = 0;
      priv->update_id = g_idle_add ((GSourceFunc) update, rdp_tab);
    }
}

static void
tab_realized (GtkWidget     *widget,
              gpointer       user_data)
{
  gtk_widget_grab_focus (widget);
  g_signal_handlers_disconnect_by_func (widget, tab_realized, user_data);
}

static void
vinagre_rdp_tab_init (VinagreRdpTab *rdp_tab)
{
  rdp_tab->priv = VINAGRE_RDP_TAB_GET_PRIVATE (rdp_tab);

  rdp_tab->priv->connected_actions = create_connected_actions (rdp_tab);

  g_signal_connect (rdp_tab, "realize", G_CALLBACK (tab_realized), NULL);
}

GtkWidget *
vinagre_rdp_tab_new (VinagreConnection *conn,
		     VinagreWindow     *window)
{
  return GTK_WIDGET (g_object_new (VINAGRE_TYPE_RDP_TAB,
				   "conn", conn,
				   "window", window,
				   "can-focus", TRUE,
				   NULL));
}

/* vim: set ts=8: */
