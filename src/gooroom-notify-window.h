/*
 *  Copyright (c) 2008-2009 Brian Tarricone <bjt23@cornell.edu>
 *  Copyright (c) 2009 Jérôme Guelfucci <jeromeg@xfce.org>
 *  Copyright (c) 2019-2021 Gooroom <gooroom@gooroom.kr>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License ONLY.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#ifndef __GOOROOM_NOTIFY_WINDOW_H__
#define __GOOROOM_NOTIFY_WINDOW_H__

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define GOOROOM_TYPE_NOTIFY_WINDOW            (gooroom_notify_window_get_type ())
#define GOOROOM_NOTIFY_WINDOW(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), GOOROOM_TYPE_NOTIFY_WINDOW, GooroomNotifyWindow))
#define GOOROOM_NOTIFY_WINDOW_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  GOOROOM_TYPE_NOTIFY_WINDOW, GooroomNotifyWindowClass))
#define GOOROOM_IS_NOTIFY_WINDOW(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GOOROOM_TYPE_NOTIFY_WINDOW))
#define GOOROOM_IS_NOTIFY_WINDOW_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  GOOROOM_TYPE_NOTIFY_WINDOW))
#define GOOROOM_NOTIFY_WINDOW_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  GOOROOM_TYPE_NOTIFY_WINDOW, GooroomNotifyWindowClass))

typedef struct _GooroomNotifyWindow  GooroomNotifyWindow;
typedef struct _GooroomNotifyWindowClass  GooroomNotifyWindowClass;
typedef struct _GooroomNotifyWindowPrivate GooroomNotifyWindowPrivate;


typedef enum
{
    GOOROOM_NOTIFY_CLOSE_REASON_EXPIRED = 1,
    GOOROOM_NOTIFY_CLOSE_REASON_DISMISSED,
    GOOROOM_NOTIFY_CLOSE_REASON_CLIENT,
    GOOROOM_NOTIFY_CLOSE_REASON_UNKNOWN,
} GooroomNotifyCloseReason;


struct _GooroomNotifyWindow {
	GtkWindow __parent__;

	GooroomNotifyWindowPrivate *priv;
};

struct _GooroomNotifyWindowClass {
	GtkWindowClass __parent_class__;

    /*< signals >*/
    void (*closed)(GooroomNotifyWindow *window,
                   GooroomNotifyCloseReason reason);

    void (*action_invoked)(GooroomNotifyWindow *window,
                           const gchar *action_id);
};


GType gooroom_notify_window_get_type (void) G_GNUC_CONST;

GtkWidget *gooroom_notify_window_new (void);

GtkWidget *gooroom_notify_window_new_full (const gchar *summary,
                                           const gchar *body,
                                           const gchar *icon_name,
                                           gint expire_timeout);

GtkWidget *gooroom_notify_window_new_with_actions (const gchar *summary,
                                                   const gchar *body,
                                                   const gchar *icon_name,
                                                   gint expire_timeout,
                                                   const gchar **actions);

void gooroom_notify_window_set_summary (GooroomNotifyWindow *window,
                                        const gchar *summary);
void gooroom_notify_window_set_body (GooroomNotifyWindow *window,
                                     const gchar *body);

void gooroom_notify_window_set_geometry (GooroomNotifyWindow *window,
                                         GdkRectangle rectangle);
GdkRectangle *gooroom_notify_window_get_geometry (GooroomNotifyWindow *window);

void gooroom_notify_window_set_last_monitor (GooroomNotifyWindow *window,
                                             gint monitor);
gint gooroom_notify_window_get_last_monitor (GooroomNotifyWindow *window);

void gooroom_notify_window_set_icon_name (GooroomNotifyWindow *window,
                                          const gchar *icon_name);
void gooroom_notify_window_set_icon_pixbuf (GooroomNotifyWindow *window,
                                            GdkPixbuf *pixbuf);

void gooroom_notify_window_set_expire_timeout (GooroomNotifyWindow *window,
                                               gint expire_timeout);

void gooroom_notify_window_set_actions (GooroomNotifyWindow *window,
                                        const gchar **actions);

void gooroom_notify_window_set_fade_transparent (GooroomNotifyWindow *window,
                                                 gboolean fade_transparent);
gboolean gooroom_notify_window_get_fade_transparent (GooroomNotifyWindow *window);

void gooroom_notify_window_set_opacity (GooroomNotifyWindow *window,
                                        gdouble opacity);
gdouble gooroom_notify_window_get_opacity (GooroomNotifyWindow *window);

void gooroom_notify_window_set_icon_only (GooroomNotifyWindow *window,
                                          gboolean icon_only);

void gooroom_notify_window_set_gauge_value (GooroomNotifyWindow *window,
                                            gint value);
void gooroom_notify_window_unset_gauge_value (GooroomNotifyWindow *window);

void gooroom_notify_window_set_do_fadeout (GooroomNotifyWindow *window,
                                           gboolean do_fadeout,
                                           gboolean do_slideout);

void gooroom_notify_window_set_notify_location (GooroomNotifyWindow *window,
                                                GtkCornerType notify_location);
/* signal trigger */
void gooroom_notify_window_closed (GooroomNotifyWindow *window,
                                   GooroomNotifyCloseReason reason);

G_END_DECLS

#endif  /* __GOOROOM_NOTIFY_WINDOW_H__ */
