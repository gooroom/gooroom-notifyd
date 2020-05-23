/*
 *  Copyright (c) 2008-2009 Brian Tarricone <bjt23@cornell.edu>
 *  Copyright (c) 2009 Jérôme Guelfucci <jeromeg@xfce.org>
 *  Copyright (c) 2015 Ali Abdallah    <ali@xfce.org>
 *  Copyright (c) 2019 Gooroom <gooroom@gooroom.kr>
 *
 *  The workarea per monitor code is taken from
 *  http://trac.galago-project.org/attachment/ticket/5/10-nd-improve-multihead-support.patch
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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifdef HAVE_STRING_H
#include <string.h>
#endif

#include <glib/gi18n.h>
#include <gdk/gdkx.h>
#include <gio/gio.h>

#include <X11/Xatom.h>

#include "common.h"
#include "gooroom-notify-gbus.h"
#include "gooroom-notify-daemon.h"
#include "gooroom-notify-window.h"
#include "gooroom-notify-marshal.h"

#define SPACE 0
#define XND_N_MONITORS gooroom_notify_daemon_get_n_monitors_quark()

struct _GooroomNotifyDaemon
{
    GooroomNotifyGBusSkeleton parent;

    GooroomNotifyKrGooroomNotifyd *gooroom_iface_skeleton;
    gint expire_timeout;
    guint bus_name_id;
    gdouble initial_opacity;
    GtkCornerType notify_location;
    gboolean do_fadeout;
    gboolean do_slideout;
    gboolean do_not_disturb;
    gint primary_monitor;

    GSettings *settings;

    GTree *active_notifications;
    GList **reserved_rectangles;
    GdkRectangle *monitors_workarea;

    guint32 last_notification_id;
};

typedef struct
{
    GooroomNotifyGBusSkeletonClass  parent;

} GooroomNotifyDaemonClass;


enum
{
    URGENCY_LOW = 0,
    URGENCY_NORMAL,
    URGENCY_CRITICAL,
};

static void gooroom_notify_daemon_screen_changed(GdkScreen *screen,
                                              gpointer user_data);
static void gooroom_notify_daemon_update_reserved_rectangles(gpointer key,
                                                          gpointer value,
                                                          gpointer data);
static void gooroom_notify_daemon_finalize(GObject *obj);
static void  gooroom_notify_daemon_constructed(GObject *obj);

static GQuark gooroom_notify_daemon_get_n_monitors_quark(void);

static GdkFilterReturn gooroom_notify_rootwin_watch_workarea(GdkXEvent *gxevent,
                                                          GdkEvent *event,
                                                          gpointer user_data);

static void gooroom_gdk_rectangle_largest_box(GdkRectangle *src1,
                                           GdkRectangle *src2,
                                           GdkRectangle *dest);
static void gooroom_notify_daemon_get_workarea(GdkScreen *screen,
                                            guint monitor,
                                            GdkRectangle *rect);
static void daemon_quit (GooroomNotifyDaemon *xndaemon);

/* DBus method callbacks  forward declarations */
static gboolean notify_get_capabilities (GooroomNotifyGBus *skeleton,
                                		 GDBusMethodInvocation   *invocation,
                                         GooroomNotifyDaemon *xndaemon);

static gboolean notify_notify (GooroomNotifyGBus *skeleton,
                               GDBusMethodInvocation   *invocation,
                               const gchar *app_name,
                               guint replaces_id,
                               const gchar *app_icon,
                               const gchar *summary,
                               const gchar *body,
                               const gchar **actions,
                               GVariant *hints,
                               gint expire_timeout,
                               GooroomNotifyDaemon *xndaemon);


static gboolean notify_close_notification (GooroomNotifyGBus *skeleton,
                                           GDBusMethodInvocation   *invocation,
                                           guint id,
                                           GooroomNotifyDaemon *xndaemon);


static gboolean notify_get_server_information (GooroomNotifyGBus *skeleton,
                                               GDBusMethodInvocation *invocation,
                                               GooroomNotifyDaemon *xndaemon);


static gboolean notify_quit (GooroomNotifyKrGooroomNotifyd *skeleton,
                             GDBusMethodInvocation   *invocation,
                             GooroomNotifyDaemon *xndaemon);


G_DEFINE_TYPE(GooroomNotifyDaemon, gooroom_notify_daemon, GOOROOM_NOTIFY_TYPE_GBUS_SKELETON)


static void
gooroom_notify_daemon_class_init(GooroomNotifyDaemonClass *klass)
{
    GObjectClass *gobject_class = (GObjectClass *)klass;

    gobject_class->finalize = gooroom_notify_daemon_finalize;
    gobject_class->constructed = gooroom_notify_daemon_constructed;
}


static gint
gooroom_direct_compare(gconstpointer a,
                    gconstpointer b,
                    gpointer user_data)
{
    return (gint)((gchar *)a - (gchar *)b);
}


static GQuark
gooroom_notify_daemon_get_n_monitors_quark(void)
{
    static GQuark quark = 0;

    if(!quark)
        quark = g_quark_from_static_string("xnd-n-monitors");

    return quark;
}

#if GTK_CHECK_VERSION (3, 22, 0)
static gint
gooroom_notify_daemon_get_monitor_index (GdkDisplay *display,
                                      GdkMonitor *monitor)
{
    gint i, nmonitors;

    nmonitors = gdk_display_get_n_monitors (display);

    for (i = 0; i < nmonitors; i++) {
        if (monitor == gdk_display_get_monitor (display, i))
            return i;
    }

    return 0;
}
#endif

static gint
gooroom_notify_daemon_get_primary_monitor (GdkScreen *screen)
{
#if GTK_CHECK_VERSION (3, 22, 0)
    GdkDisplay *display = gdk_screen_get_display (screen);
    GdkMonitor *monitor =gdk_display_get_primary_monitor (display);

    return gooroom_notify_daemon_get_monitor_index (display, monitor);
#else
    return gdk_screen_get_primary_monitor (screen);
#endif
}

static gint
gooroom_notify_daemon_get_monitor_at_point (GdkScreen *screen,
                                         gint x,
                                         gint y)
{
#if GTK_CHECK_VERSION (3, 22, 0)
    GdkDisplay *display = gdk_screen_get_display (screen);
    GdkMonitor *monitor = gdk_display_get_monitor_at_point (display, x, y);

    return gooroom_notify_daemon_get_monitor_index (display, monitor);
#else
    return gdk_screen_get_monitor_at_point (screen, x, y);
#endif
}

static inline gint
gooroom_notify_daemon_get_n_monitors (GdkScreen *screen)
{
#if GTK_CHECK_VERSION (3, 22, 0)
    return gdk_display_get_n_monitors (gdk_screen_get_display (screen));
#else
    return gdk_screen_get_n_monitors (screen);
#endif
}

static gboolean
gooroom_notify_daemon_screen_changed_idle (gpointer user_data)
{
    GooroomNotifyDaemon *xndaemon = GOOROOM_NOTIFY_DAEMON(user_data);
    gint j;
    gint new_nmonitor;
    gint old_nmonitor;
    GdkScreen *screen;

    screen = gdk_screen_get_default();

    if(!xndaemon->monitors_workarea || !xndaemon->reserved_rectangles)
        /* Placement data not initialized, don't update it */
        return FALSE;

    new_nmonitor = gooroom_notify_daemon_get_n_monitors (screen);
    old_nmonitor = GPOINTER_TO_INT(g_object_get_qdata(G_OBJECT(screen), XND_N_MONITORS));

    /* Set the new number of monitors */
    g_object_set_qdata(G_OBJECT(screen), XND_N_MONITORS, GINT_TO_POINTER(new_nmonitor));

    /* Free the current reserved rectangles on screen */
    for(j = 0; j < old_nmonitor; j++)
        g_list_free(xndaemon->reserved_rectangles[j]);

    g_free(xndaemon->reserved_rectangles);
    g_free(xndaemon->monitors_workarea);

    xndaemon->monitors_workarea = g_new0(GdkRectangle, new_nmonitor);
    for(j = 0; j < new_nmonitor; j++) {
        gooroom_notify_daemon_get_workarea(screen, j,
                                        &(xndaemon->monitors_workarea[j]));
    }

    /* Initialize a new reserved rectangles array for screen */
    xndaemon->reserved_rectangles = g_new0(GList *, new_nmonitor);

    /* Traverse the active notifications tree to fill the new reserved rectangles array for screen */
    g_tree_foreach(xndaemon->active_notifications,
                   (GTraverseFunc)gooroom_notify_daemon_update_reserved_rectangles,
                   xndaemon);

    return FALSE;
}

static GdkFilterReturn
gooroom_notify_rootwin_watch_workarea(GdkXEvent *gxevent,
                                   GdkEvent *event,
                                   gpointer user_data)
{
    GooroomNotifyDaemon *xndaemon = GOOROOM_NOTIFY_DAEMON(user_data);
    XPropertyEvent *xevt = (XPropertyEvent *)gxevent;

    if(xevt->type == PropertyNotify
       && XInternAtom(xevt->display, "_NET_WORKAREA", False) == xevt->atom
       && xndaemon->monitors_workarea)
    {
        g_idle_add (gooroom_notify_daemon_screen_changed_idle, user_data);
    }

    return GDK_FILTER_CONTINUE;
}

static void
gooroom_notify_daemon_screen_changed(GdkScreen *screen,
                                  gpointer user_data)
{
    g_idle_add (gooroom_notify_daemon_screen_changed_idle, user_data);
}

static void
gooroom_notify_daemon_init_placement_data(GooroomNotifyDaemon *xndaemon)
{
    GdkScreen *screen = gdk_screen_get_default();
    gint nmonitor = gooroom_notify_daemon_get_n_monitors (screen);
    GdkWindow *groot;
    int j;

    g_object_set_qdata(G_OBJECT(screen), XND_N_MONITORS, GINT_TO_POINTER(nmonitor));

    g_signal_connect(G_OBJECT(screen), "monitors-changed",
                     G_CALLBACK(gooroom_notify_daemon_screen_changed), xndaemon);

    xndaemon->reserved_rectangles = g_new0(GList *, nmonitor);
    xndaemon->monitors_workarea = g_new0(GdkRectangle, nmonitor);

    for(j = 0; j < nmonitor; j++)
        gooroom_notify_daemon_get_workarea(screen, j,
                                        &(xndaemon->monitors_workarea[j]));

    /* Monitor root window changes */
    groot = gdk_screen_get_root_window(screen);
    gdk_window_add_filter(groot, gooroom_notify_rootwin_watch_workarea, xndaemon);
    gdk_window_set_events(groot, gdk_window_get_events(groot) | GDK_PROPERTY_CHANGE_MASK);
}


static void
gooroom_notify_bus_name_acquired_cb (GDBusConnection *connection,
                                  const gchar *name,
                                  gpointer user_data)
{
    GooroomNotifyDaemon *xndaemon;
    GError *error = NULL;
    gboolean exported;


    xndaemon = GOOROOM_NOTIFY_DAEMON(user_data);

    exported =  g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON (xndaemon),
                                                  connection,
                                                  "/org/freedesktop/Notifications",
                                                  &error);
    if (exported)
    {
        /* Connect dbus signals callbacks */
        g_signal_connect (xndaemon, "handle-notify",
                          G_CALLBACK(notify_notify), xndaemon);

        g_signal_connect (xndaemon, "handle-get-capabilities",
                          G_CALLBACK(notify_get_capabilities), xndaemon);

        g_signal_connect (xndaemon, "handle-get-server-information",
                          G_CALLBACK(notify_get_server_information), xndaemon);

        g_signal_connect (xndaemon, "handle-close-notification",
                          G_CALLBACK(notify_close_notification), xndaemon);
    }
    else
    {
        g_warning ("Failed to export interface: %s", error->message);
        g_error_free (error);
        gtk_main_quit ();
    }

    xndaemon->gooroom_iface_skeleton  = gooroom_notify_kr_gooroom_notifyd_skeleton_new();
    exported =  g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON(xndaemon->gooroom_iface_skeleton),
                                                  connection,
                                                  "/org/freedesktop/Notifications",
                                                  &error);
    if (exported)
        g_signal_connect (xndaemon->gooroom_iface_skeleton, "handle-quit",
                          G_CALLBACK(notify_quit), xndaemon);
    else
    {
        g_warning ("Failed to export interface: %s", error->message);
        g_error_free (error);
        gtk_main_quit ();
    }
}

static void
gooroom_notify_bus_name_lost_cb (GDBusConnection *connection,
                              const gchar     *name,
                              gpointer         user_data)
{
    daemon_quit(GOOROOM_NOTIFY_DAEMON(user_data));
}

static void gooroom_notify_daemon_constructed (GObject *obj)
{
    GooroomNotifyDaemon *self;

    self  = GOOROOM_NOTIFY_DAEMON (obj);

    self->bus_name_id = g_bus_own_name (G_BUS_TYPE_SESSION,
                                        "org.freedesktop.Notifications",
                                        G_BUS_NAME_OWNER_FLAGS_REPLACE,
                                        gooroom_notify_bus_name_acquired_cb,
                                        NULL,
                                        gooroom_notify_bus_name_lost_cb,
                                        self,
                                        NULL);
}

static void
gooroom_notify_daemon_init(GooroomNotifyDaemon *xndaemon)
{
    xndaemon->active_notifications = g_tree_new_full(gooroom_direct_compare,
                                                     NULL, NULL,
                                                     (GDestroyNotify)gtk_widget_destroy);

    xndaemon->last_notification_id = 1;
    xndaemon->reserved_rectangles = NULL;
    xndaemon->monitors_workarea = NULL;
}

static void
gooroom_notify_daemon_finalize(GObject *obj)
{
    GooroomNotifyDaemon *xndaemon = GOOROOM_NOTIFY_DAEMON(obj);
    GDBusConnection *connection;

    connection = g_dbus_interface_skeleton_get_connection(G_DBUS_INTERFACE_SKELETON(xndaemon));


    if ( g_dbus_interface_skeleton_has_connection(G_DBUS_INTERFACE_SKELETON(xndaemon),
                                                  connection))
    {
        g_dbus_interface_skeleton_unexport (G_DBUS_INTERFACE_SKELETON(xndaemon));
    }

    if (xndaemon->gooroom_iface_skeleton &&
        g_dbus_interface_skeleton_has_connection(G_DBUS_INTERFACE_SKELETON(xndaemon->gooroom_iface_skeleton),
                                                 connection))
    {
    g_dbus_interface_skeleton_unexport (G_DBUS_INTERFACE_SKELETON(xndaemon->gooroom_iface_skeleton));
    }


    if(xndaemon->reserved_rectangles && xndaemon->monitors_workarea) {
      gint i;

      GdkScreen *screen = gdk_screen_get_default ();
      GdkWindow *groot = gdk_screen_get_root_window(screen);
      gint nmonitor = gooroom_notify_daemon_get_n_monitors (screen);

      gdk_window_remove_filter(groot, gooroom_notify_rootwin_watch_workarea, xndaemon);

      for(i = 0; i < nmonitor; i++) {
          if (xndaemon->reserved_rectangles[i])
              g_list_free(xndaemon->reserved_rectangles[i]);
      }

      g_free(xndaemon->reserved_rectangles);
      g_free(xndaemon->monitors_workarea);
    }

    g_tree_destroy(xndaemon->active_notifications);

    if(xndaemon->settings)
        g_object_unref(xndaemon->settings);

    G_OBJECT_CLASS(gooroom_notify_daemon_parent_class)->finalize(obj);
}



static guint32
gooroom_notify_daemon_generate_id(GooroomNotifyDaemon *xndaemon)
{
    if(G_UNLIKELY(xndaemon->last_notification_id == 0))
        xndaemon->last_notification_id = 1;

    return xndaemon->last_notification_id++;
}

static void
gooroom_notify_daemon_window_action_invoked(GooroomNotifyWindow *window,
                                         const gchar *action,
                                         gpointer user_data)
{
    GooroomNotifyDaemon *xndaemon = user_data;
    guint id = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(window),
                                                  "--notify-id"));

    gooroom_notify_gbus_emit_action_invoked (GOOROOM_NOTIFY_GBUS(xndaemon),
                                          id,
                                          action);
}

static void
gooroom_notify_daemon_window_closed(GooroomNotifyWindow *window,
                                 GooroomNotifyCloseReason reason,
                                 gpointer user_data)
{
    GooroomNotifyDaemon *xndaemon = user_data;
    gpointer id_p = g_object_get_data(G_OBJECT(window), "--notify-id");
    GList *list;
    gint monitor = gooroom_notify_window_get_last_monitor(window);

    /* Remove the reserved rectangle from the list */
    list = xndaemon->reserved_rectangles[monitor];
    list = g_list_remove(list, gooroom_notify_window_get_geometry(window));
    xndaemon->reserved_rectangles[monitor] = list;

    g_tree_remove(xndaemon->active_notifications, id_p);

    gooroom_notify_gbus_emit_notification_closed (GOOROOM_NOTIFY_GBUS(xndaemon),
                                               GPOINTER_TO_UINT(id_p),
                                               (guint)reason);
}

/* Gets the largest rectangle in src1 which does not contain src2.
 * src2 is totally included in src1. */
/*
 *                    1
 *          ____________________
 *          |            ^      |
 *          |           d1      |
 *      4   |         ___|_     | 2
 *          | < d4  >|_____|<d2>|
 *          |            ^      |
 *          |___________d3______|
 *
 *                    3
 */
static void
gooroom_gdk_rectangle_largest_box(GdkRectangle *src1,
                               GdkRectangle *src2,
                               GdkRectangle *dest)
{
    gint d1, d2, d3, d4; /* distance to the different sides of src1, see drawing above */
    gint max;

    d1 = src2->y - src1->y;
    d4 = src2->x - src1->x;
    d2 = src1->width - d4 - src2->width;
    d3 = src1->height - d1 - src2->height;

    /* Get the max of rectangles implied by d1, d2, d3 and d4 */
    max = MAX (d1 * src1->width, d2 * src1->height);
    max = MAX (max, d3 * src1->width);
    max = MAX (max, d4 * src1->height);

    if (max == d1 * src1->width) {
        dest->x = src1->x;
        dest->y = src1->y;
        dest->height = d1;
        dest->width = src1->width;
    }
    else if (max == d2 * src1->height) {
        dest->x = src2->x + src2->width;
        dest->y = src1->y;
        dest->width = d2;
        dest->height = src1->height;
    }
    else if (max == d3 * src1->width) {
        dest->x = src1->x;
        dest->y = src2->y + src2->height;
        dest->width = src1->width;
        dest->height = d3;
    }
    else {
        /* max == d4 * src1->height */
        dest->x = src1->x;
        dest->y = src1->y;
        dest->height = src1->height;
        dest->width = d4;
    }
}

static inline void
translate_origin(GdkRectangle *src1,
                 gint xoffset,
                 gint yoffset)
{
    src1->x += xoffset;
    src1->y += yoffset;
}

static gboolean
get_work_area (GdkWindow *gdk_window, GdkScreen *screen, int scale, GdkRectangle *rect)
{
        Atom            xatom;
        Atom            type;
        Window          win;
        int             format;
        gulong          num;
        gulong          leftovers;
        gulong          max_len = 4 * 32;
        guchar         *ret_workarea;
        long           *workareas;
        int             result;
        int             disp_screen;
        int             desktop;
        Display        *display;
        int             i;

        display = GDK_WINDOW_XDISPLAY (gdk_window);
        win  = GDK_WINDOW_XID (gdk_window);

        xatom = XInternAtom (display, "_NET_WM_STRUT_PARTIAL", True);

        disp_screen = GDK_SCREEN_XNUMBER (screen);

        /* Defaults in case of error */
        rect->x = 0;
        rect->y = 0;
        rect->width = gdk_screen_get_width (screen) * scale;
        rect->height = gdk_screen_get_height (screen) * scale;

        if (xatom == None)
                return FALSE;

        result = XGetWindowProperty (display,
                                     win,
                                     xatom,
                                     0,
                                     max_len,
                                     False,
                                     AnyPropertyType,
                                     &type,
                                     &format,
                                     &num,
                                     &leftovers,
                                     &ret_workarea);

        if (result != Success
            || type == None
            || format == 0
            || leftovers
            || num % 12) {
                return FALSE;
        }

        workareas = (long *) ret_workarea;

        for (i = 0; i < 4; i++) {
            int thickness, strut_begin, strut_end;

            thickness = workareas[i];

            if (thickness == 0)
                continue;

            strut_begin = workareas[4+(i*2)];
            strut_end   = workareas[4+(i*2)+1];

            switch (i)
            {
                case 0: // left
                    rect->y      = strut_begin;
                    rect->width  = thickness;
                    rect->height = strut_end - strut_begin + 1;
                break;
                case 1: // right
                    rect->x      = rect->x + rect->width - thickness;
                    rect->y      = strut_begin;
                    rect->width  = thickness;
                    rect->height = strut_end - strut_begin + 1;
                break;
                case 2: // top
                    rect->x      = strut_begin;
                    rect->width  = strut_end - strut_begin + 1;
                    rect->height = thickness;
                break;
                case 3: // bottom
                    rect->x      = strut_begin;
                    rect->y      = rect->y + rect->height - thickness;
                    rect->width = strut_end - strut_begin + 1;
                    rect->height = thickness;
                break;
                default:
                    continue;
            }
        }

        rect->x /= scale;
        rect->y /= scale;
        rect->width /= scale;
        rect->height /= scale;

        XFree (ret_workarea);

        if (rect->x == 0 && rect->y == 0 &&
            rect->width == gdk_screen_get_width (screen) &&
            rect->height == gdk_screen_get_height (screen)) {
            return FALSE;
        }

        return TRUE;
}

/* Returns the workarea (largest non-panel/dock occupied rectangle) for a given
   monitor. */
static void
gooroom_notify_daemon_get_workarea(GdkScreen *screen,
                                guint monitor_num,
                                GdkRectangle *workarea)
{
    GdkDisplay *display;
    GList *windows_list, *l;
    gint monitor_xoff, monitor_yoff;
    GdkRectangle monitor_rect;
    gint scale = 1;

    display = gdk_screen_get_display(screen);

    /* Defaults */
#if GTK_CHECK_VERSION (3, 22, 0)
    gdk_monitor_get_geometry (gdk_display_get_monitor (display, monitor_num), workarea);
    scale = gdk_monitor_get_scale_factor (gdk_display_get_monitor (display, monitor_num));
#else
    gdk_screen_get_monitor_geometry(screen, monitor_num, workarea);
#endif

    monitor_rect.x = workarea->x;
    monitor_rect.y = workarea->y;
    monitor_rect.width = workarea->width;
    monitor_rect.height = workarea->height;

    monitor_xoff = workarea->x;
    monitor_yoff = workarea->y;

    /* Sync the display */
    gdk_display_sync(display);
G_GNUC_BEGIN_IGNORE_DEPRECATIONS
    gdk_window_process_all_updates();
G_GNUC_END_IGNORE_DEPRECATIONS

    windows_list = gdk_screen_get_window_stack(screen);

    for(l = g_list_first(windows_list); l != NULL; l = g_list_next(l)) {
        GdkWindow *window = l->data;
        GdkWindowTypeHint type_hint;

        gdk_error_trap_push();
        type_hint = gdk_window_get_type_hint(window);
        gdk_flush();

        if (gdk_error_trap_pop()) {
            continue;
        }

        if(type_hint == GDK_WINDOW_TYPE_HINT_DOCK) {
            GdkRectangle strut_rect, window_geom, intersection;

            if (!get_work_area(window, screen, scale, &strut_rect))
                continue;

            if(gdk_rectangle_intersect(&monitor_rect, &strut_rect, &window_geom)){
                if(gdk_rectangle_intersect(workarea, &window_geom, &intersection)){
                    translate_origin(workarea, -monitor_xoff, -monitor_yoff);
                    translate_origin(&intersection, -monitor_xoff, -monitor_yoff);

                    gooroom_gdk_rectangle_largest_box(workarea, &intersection, workarea);

                    translate_origin(workarea, monitor_xoff, monitor_yoff);
                    translate_origin(&intersection, monitor_xoff, monitor_yoff);
                }
            }
        }

        g_object_unref(window);
    }

    g_list_free(windows_list);
}

static void
gooroom_notify_daemon_window_size_allocate(GtkWidget *widget,
                                        GtkAllocation *allocation,
                                        gpointer user_data)
{
    GooroomNotifyDaemon *xndaemon = user_data;
    GooroomNotifyWindow *window = GOOROOM_NOTIFY_WINDOW(widget);
    GdkScreen *p_screen = NULL;
    GdkDevice *pointer;
    GdkScreen *widget_screen;
#if GTK_CHECK_VERSION (3, 20, 0)
    GdkSeat *seat;
#else
    GdkDisplay *display;
    GdkDeviceManager *device_manager;
#endif
    gint x, y, monitor, max_width;
    GdkRectangle *geom_tmp, geom, initial, widget_geom;
    GList *list;
    gboolean found = FALSE;
    static gboolean placement_data_initialized = FALSE;

    if (placement_data_initialized == FALSE) {
        /* First time we place a notification, initialize the arrays needed for
         * that (workarea, notification lists...). */
        gooroom_notify_daemon_init_placement_data(xndaemon);
        placement_data_initialized = TRUE;
    }

    geom_tmp = gooroom_notify_window_get_geometry(window);
    if(geom_tmp->width != 0 && geom_tmp->height != 0) {
        /* Notification has already been placed previously. Not sure if that
         * can happen. */
        GList *old_list;

        monitor = gooroom_notify_window_get_last_monitor(window);
        old_list = xndaemon->reserved_rectangles[monitor];

        old_list = g_list_remove(old_list, gooroom_notify_window_get_geometry(window));
        xndaemon->reserved_rectangles[monitor] = old_list;
    }

    widget_screen = gtk_widget_get_screen (widget);
#if GTK_CHECK_VERSION (3, 20, 0)
    seat = gdk_display_get_default_seat (gdk_display_get_default());
    pointer = gdk_seat_get_pointer (seat);
#else
    display = gdk_screen_get_display (widget_screen);
    device_manager = gdk_display_get_device_manager (display);
    pointer = gdk_device_manager_get_client_pointer (device_manager);
#endif

    gdk_device_get_position (pointer, &p_screen, &x, &y);

    if (xndaemon->primary_monitor == 1)
        monitor = gooroom_notify_daemon_get_primary_monitor (widget_screen);
    else
        monitor = gooroom_notify_daemon_get_monitor_at_point (p_screen, x, y);

    geom = xndaemon->monitors_workarea[monitor];

    gtk_window_set_screen(GTK_WINDOW(widget), p_screen);

    /* Set initial geometry */
    initial.width = allocation->width;
    initial.height = allocation->height;

    switch(xndaemon->notify_location) {
        case GTK_CORNER_TOP_LEFT:
            initial.x = geom.x + SPACE;
            initial.y = geom.y + SPACE;
            break;
        case GTK_CORNER_BOTTOM_LEFT:
            initial.x = geom.x + SPACE;
            initial.y = geom.y + geom.height - allocation->height - SPACE;
            break;
        case GTK_CORNER_TOP_RIGHT:
            initial.x = geom.x + geom.width - allocation->width - SPACE;
            initial.y = geom.y + SPACE;
            break;
        case GTK_CORNER_BOTTOM_RIGHT:
            initial.x = geom.x + geom.width - allocation->width - SPACE;
            initial.y = geom.y + geom.height - allocation->height - SPACE;
            break;
        default:
            g_warning("Invalid notify location: %d", xndaemon->notify_location);
            return;
    }

    widget_geom.x = initial.x;
    widget_geom.y = initial.y;
    widget_geom.width = initial.width;
    widget_geom.height = initial.height;
    max_width = 0;

    /* Get the list of reserved places */
    list = xndaemon->reserved_rectangles[monitor];

    if(!list) {
        /* If the list is empty, there are no displayed notifications */
        gooroom_notify_window_set_geometry(GOOROOM_NOTIFY_WINDOW(widget), widget_geom);
        gooroom_notify_window_set_last_monitor(GOOROOM_NOTIFY_WINDOW(widget), monitor);

        list = g_list_prepend(list, gooroom_notify_window_get_geometry(GOOROOM_NOTIFY_WINDOW(widget)));
        xndaemon->reserved_rectangles[monitor] = list;

        gtk_window_move(GTK_WINDOW(widget), widget_geom.x, widget_geom.y);
        return;
    } else {
        /* Else, we try to find the appropriate position on the monitor */
        while(!found) {
            gboolean overlaps = FALSE;
            GList *l = NULL;
            gint notification_y, notification_height;

            for(l = g_list_first(list); l; l = l->next) {
                GdkRectangle *rectangle = l->data;

                overlaps =  overlaps || gdk_rectangle_intersect(rectangle, &widget_geom, NULL);

                if(overlaps) {

                    if(rectangle->width > max_width)
                        max_width = rectangle->width;

                    notification_y = rectangle->y;
                    notification_height = rectangle->height;

                    break;
                }
            }

            if(!overlaps) {
                found = TRUE;
            } else {
                switch(xndaemon->notify_location) {
                    case GTK_CORNER_TOP_LEFT:
                        widget_geom.y = notification_y + notification_height + SPACE;

                        if(widget_geom.y + widget_geom.height > geom.height + geom.y) {
                            widget_geom.y = geom.y + SPACE;
                            widget_geom.x = widget_geom.x + max_width + SPACE;
                            max_width = 0;

                            if(widget_geom.x + widget_geom.width > geom.width + geom.x) {
                                widget_geom.x = initial.x;
                                widget_geom.y = initial.y;
                                found = TRUE;
                            }
                        }
                        break;
                    case GTK_CORNER_BOTTOM_LEFT:
                        widget_geom.y = notification_y - widget_geom.height - SPACE;

                        if(widget_geom.y < geom.y) {
                            widget_geom.y = geom.y + geom.height - widget_geom.height - SPACE;
                            widget_geom.x = widget_geom.x + max_width + SPACE;
                            max_width = 0;

                            if(widget_geom.x + widget_geom.width > geom.width + geom.x) {
                                widget_geom.x = initial.x;
                                widget_geom.y = initial.y;
                                found = TRUE;
                            }
                        }
                        break;
                    case GTK_CORNER_TOP_RIGHT:
                        widget_geom.y = notification_y + notification_height + SPACE;

                        if(widget_geom.y + widget_geom.height > geom.height + geom.y) {
                            widget_geom.y = geom.y + SPACE;
                            widget_geom.x = widget_geom.x - max_width - SPACE;
                            max_width = 0;

                            if(widget_geom.x < geom.x) {
                                widget_geom.x = initial.x;
                                widget_geom.y = initial.y;
                                found = TRUE;
                            }
                        }
                        break;
                    case GTK_CORNER_BOTTOM_RIGHT:
                        widget_geom.y = notification_y - widget_geom.height - SPACE;

                        if(widget_geom.y < geom.y) {
                            widget_geom.y = geom.y + geom.height - widget_geom.height - SPACE;
                            widget_geom.x = widget_geom.x - max_width - SPACE;
                            max_width = 0;

                            if(widget_geom.x < geom.x) {
                                widget_geom.x = initial.x;
                                widget_geom.y = initial.y;
                                found = TRUE;
                            }
                        }
                        break;

                    default:
                        g_warning("Invalid notify location: %d", xndaemon->notify_location);
                        return;
                }
            }
        }
    }

    gooroom_notify_window_set_geometry(GOOROOM_NOTIFY_WINDOW(widget), widget_geom);
    gooroom_notify_window_set_last_monitor(GOOROOM_NOTIFY_WINDOW(widget), monitor);

    list = g_list_prepend(list, gooroom_notify_window_get_geometry(GOOROOM_NOTIFY_WINDOW(widget)));
    xndaemon->reserved_rectangles[monitor] = list;

    gtk_window_move(GTK_WINDOW(widget), widget_geom.x, widget_geom.y);
}


static void
gooroom_notify_daemon_update_reserved_rectangles(gpointer key,
                                              gpointer value,
                                              gpointer data)
{
    GooroomNotifyDaemon *xndaemon = GOOROOM_NOTIFY_DAEMON(data);
    GooroomNotifyWindow *window = GOOROOM_NOTIFY_WINDOW(value);
    gint width, height;
    GtkAllocation allocation;

    /* Get the size of the notification */
    gtk_window_get_size(GTK_WINDOW(window), &width, &height);

    allocation.x = 0;
    allocation.y = 0;
    allocation.width = width;
    allocation.height = height;

    gooroom_notify_daemon_window_size_allocate(GTK_WIDGET(window), &allocation, xndaemon);
}

static gboolean notify_get_capabilities (GooroomNotifyGBus *skeleton,
                                         GDBusMethodInvocation   *invocation,
                                         GooroomNotifyDaemon *xndaemon)
{
    const gchar *const capabilities[] =
    {
        "actions", "body", "body-hyperlinks", "body-markup", "icon-static",
        "x-canonical-private-icon-only", NULL
    };

    gooroom_notify_gbus_complete_get_capabilities(skeleton, invocation, capabilities);

    return TRUE;
}

static gboolean
notify_show_window (gpointer window)
{
    gtk_widget_show(GTK_WIDGET(window));
    return FALSE;
}

static gboolean
notify_notify (GooroomNotifyGBus *skeleton,
               GDBusMethodInvocation   *invocation,
               const gchar *app_name,
               guint replaces_id,
               const gchar *app_icon,
               const gchar *summary,
               const gchar *body,
               const gchar **actions,
               GVariant *hints,
               gint expire_timeout,
               GooroomNotifyDaemon *xndaemon)
{
    GooroomNotifyWindow *window;
    GdkPixbuf *pix = NULL;
    GVariant *image_data = NULL;
    GVariant *icon_data = NULL;
    const gchar *image_path = NULL;
    gchar *desktop_id = NULL;
    gchar *new_app_name;
    gint value_hint = 0;
    gboolean value_hint_set = FALSE;
    gboolean x_canonical = FALSE;
    gboolean transient = FALSE;
    GVariant *item;
    GVariantIter iter;
    guint OUT_id = gooroom_notify_daemon_generate_id(xndaemon);

    g_variant_iter_init (&iter, hints);

    while ((item = g_variant_iter_next_value (&iter)))
    {
        gchar *key;
        GVariant   *value;

        g_variant_get (item,
                       "{sv}",
                       &key,
                   	   &value);

        if (g_strcmp0 (key, "urgency") == 0)
        {
            if (g_variant_is_of_type (value, G_VARIANT_TYPE_BYTE) &&
                (g_variant_get_byte(value) == URGENCY_CRITICAL))
            {
                /* don't expire urgent notifications */
                expire_timeout = 0;
            }
            g_variant_unref(value);
        }
        else if ((g_strcmp0 (key, "image-data") == 0) ||
                 (g_strcmp0 (key, "image_data") == 0))
        {
            if (image_data) {
                g_variant_unref(image_data);
            }
            image_data = value;
        }
        else if ((g_strcmp0 (key, "icon-data") == 0) ||
                 (g_strcmp0 (key, "icon_data") == 0))
        {
            if (icon_data) {
                g_variant_unref(icon_data);
            }
            icon_data = value;
        }
        else if ((g_strcmp0 (key, "image-path") == 0) ||
                 (g_strcmp0 (key, "image_path") == 0))
        {
            image_path = g_variant_get_string (value, NULL);
            g_variant_unref(value);
        }
        else if ((g_strcmp0 (key, "desktop_entry") == 0) ||
                 (g_strcmp0 (key, "desktop-entry") == 0))
        {
            if (g_variant_is_of_type (value, G_VARIANT_TYPE_STRING))
                desktop_id = g_variant_dup_string (value, NULL);

            g_variant_unref(value);
        }
        else if (g_strcmp0 (key, "value") == 0)
        {
            if (g_variant_is_of_type (value, G_VARIANT_TYPE_INT32))
            {
                value_hint = g_variant_get_int32 (value);
                value_hint_set = TRUE;
            }
            g_variant_unref(value);
        }
        else if (g_strcmp0 (key, "transient") == 0)
        {
            transient = TRUE;
            g_variant_unref(value);
        }
        else if (g_strcmp0 (key, "x-canonical-private-icon-only") == 0)
        {
            x_canonical = TRUE;
            g_variant_unref(value);
        }
        else
        {
            g_variant_unref(value);
        }

        g_free(key);
        g_variant_unref (item);
    }

    if (desktop_id)
        new_app_name = g_strdup (desktop_id);
    else
        new_app_name = g_strdup (app_name);

    if(expire_timeout == -1)
        expire_timeout = xndaemon->expire_timeout;

    /* Don't show notification bubbles in the "Do not disturb" mode or if the
       application has been muted by the user. Exceptions are "urgent"
       notifications which do not expire. */
    if (expire_timeout != 0)
    {
        if (xndaemon->do_not_disturb == TRUE)
        {
            gooroom_notify_gbus_complete_notify (skeleton, invocation, OUT_id);
            if (image_data)
                g_variant_unref (image_data);
            if (desktop_id)
                g_free (desktop_id);
            return TRUE;
        }
    }

    if(replaces_id
       && (window = g_tree_lookup(xndaemon->active_notifications,
                                  GUINT_TO_POINTER(replaces_id))))
    {
        gooroom_notify_window_set_summary(window, summary);
        gooroom_notify_window_set_body(window, body);
        gooroom_notify_window_set_actions(window, actions);
        gooroom_notify_window_set_expire_timeout(window, expire_timeout);
        gooroom_notify_window_set_opacity(window, xndaemon->initial_opacity);

        OUT_id = replaces_id;
    } else {
        window = GOOROOM_NOTIFY_WINDOW(gooroom_notify_window_new_with_actions(summary, body,
                                                                        app_icon,
                                                                        expire_timeout,
                                                                        actions));
        gooroom_notify_window_set_opacity(window, xndaemon->initial_opacity);

        g_object_set_data(G_OBJECT(window), "--notify-id",
                          GUINT_TO_POINTER(OUT_id));

        g_tree_insert(xndaemon->active_notifications,
                      GUINT_TO_POINTER(OUT_id), window);

        g_signal_connect(G_OBJECT(window), "action-invoked",
                         G_CALLBACK(gooroom_notify_daemon_window_action_invoked),
                         xndaemon);
        g_signal_connect(G_OBJECT(window), "closed",
                         G_CALLBACK(gooroom_notify_daemon_window_closed),
                         xndaemon);
        g_signal_connect(G_OBJECT(window), "size-allocate",
                         G_CALLBACK(gooroom_notify_daemon_window_size_allocate),
                         xndaemon);

        gtk_widget_realize(GTK_WIDGET(window));

        g_idle_add(notify_show_window, window);
    }

    if (image_data) {
        pix = notify_pixbuf_from_image_data(image_data);
        if (pix) {
            gooroom_notify_window_set_icon_pixbuf(window, pix);
            g_object_unref(G_OBJECT(pix));
        }
    }
    else if (image_path) {
        gooroom_notify_window_set_icon_name (window, image_path);
    }
    else if (app_icon && (g_strcmp0 (app_icon, "") != 0)) {
        gooroom_notify_window_set_icon_name (window, app_icon);
    }
    else if (icon_data) {
        pix = notify_pixbuf_from_image_data(icon_data);
        if (pix) {
            gooroom_notify_window_set_icon_pixbuf(window, pix);
            g_object_unref(G_OBJECT(pix));
        }
    }
    else if (desktop_id) {
        gchar *icon = notify_icon_name_from_desktop_id (desktop_id);
        gooroom_notify_window_set_icon_name (window, icon);
        g_free (icon);
    }

    gooroom_notify_window_set_icon_only(window, x_canonical);

    gooroom_notify_window_set_do_fadeout(window, xndaemon->do_fadeout, xndaemon->do_slideout);
    gooroom_notify_window_set_notify_location(window, xndaemon->notify_location);

    if (value_hint_set)
        gooroom_notify_window_set_gauge_value(window, value_hint);
    else
        gooroom_notify_window_unset_gauge_value(window);

    gtk_widget_realize(GTK_WIDGET(window));

    gooroom_notify_gbus_complete_notify(skeleton, invocation, OUT_id);

    if (image_data)
      g_variant_unref (image_data);
    if (icon_data)
      g_variant_unref (icon_data);
    if (desktop_id)
        g_free (desktop_id);
    return TRUE;
}


static gboolean notify_close_notification (GooroomNotifyGBus *skeleton,
                                           GDBusMethodInvocation   *invocation,
                                           guint id,
                                           GooroomNotifyDaemon *xndaemon)
{
    GooroomNotifyWindow *window = g_tree_lookup(xndaemon->active_notifications,
                                                GUINT_TO_POINTER(id));

    if(window)
        gooroom_notify_window_closed(window, GOOROOM_NOTIFY_CLOSE_REASON_CLIENT);

    gooroom_notify_gbus_complete_close_notification(skeleton, invocation);

    return TRUE;
}

static gboolean notify_get_server_information (GooroomNotifyGBus *skeleton,
                                               GDBusMethodInvocation   *invocation,
                                               GooroomNotifyDaemon *xndaemon)
{
    gooroom_notify_gbus_complete_get_server_information(skeleton,
                                                        invocation,
                                                        "Gooroom Notify Daemon",
                                                        "Gooroom",
                                                        VERSION,
                                                        NOTIFICATIONS_SPEC_VERSION);

    return TRUE;
}


static void daemon_quit (GooroomNotifyDaemon *xndaemon)
{
    gint i, main_level = gtk_main_level();
    for(i = 0; i < main_level; ++i)
        gtk_main_quit();
}


static gboolean notify_quit (GooroomNotifyKrGooroomNotifyd *skeleton,
                             GDBusMethodInvocation   *invocation,
                             GooroomNotifyDaemon *xndaemon)
{
    gooroom_notify_kr_gooroom_notifyd_complete_quit (skeleton, invocation);
    daemon_quit(xndaemon);
    return TRUE;
}

static void
gooroom_notify_daemon_settings_changed(GSettings *settings,
                                    const gchar *key,
                                    gpointer data)
{
    GooroomNotifyDaemon *xndaemon = GOOROOM_NOTIFY_DAEMON (data);

    if(g_str_equal (key, "expire-timeout")) {
        xndaemon->expire_timeout = g_settings_get_int(settings, key);
        if(xndaemon->expire_timeout != -1)
            xndaemon->expire_timeout *= 1000;
    } else if(g_str_equal (key, "initial-opacity")) {
        xndaemon->initial_opacity = g_settings_get_double(settings, key);
    } else if(g_str_equal (key, "notify-location")) {
        xndaemon->notify_location = g_settings_get_uint(settings, key);
    } else if(g_str_equal (key, "do-fadeout")) {
        xndaemon->do_fadeout = g_settings_get_boolean(settings, key);
    } else if(g_str_equal (key, "do-slideout")) {
        xndaemon->do_slideout = g_settings_get_boolean(settings, key);
    } else if(g_str_equal (key, "primary-monitor")) {
        xndaemon->primary_monitor = g_settings_get_uint(settings, key);
    } else if(g_str_equal (key, "do-not-disturb")) {
        xndaemon->do_not_disturb = g_settings_get_boolean(settings, key);
    }
}

static gboolean
gooroom_notify_daemon_load_config (GooroomNotifyDaemon *xndaemon,
                                   GError **error)
{
    GSettingsSchema *schema = NULL;

    schema = g_settings_schema_source_lookup (g_settings_schema_source_get_default (), "apps.gooroom-notifyd", TRUE);

    if (schema) {
        xndaemon->settings = g_settings_new_full (schema, NULL, NULL);
    }

    xndaemon->expire_timeout = 10 * 1000;
    xndaemon->initial_opacity = 0.9;
    xndaemon->notify_location = 3;
    xndaemon->do_fadeout = TRUE;
    xndaemon->do_slideout = FALSE;
    xndaemon->primary_monitor = 0;
    xndaemon->do_not_disturb = FALSE;

	if (xndaemon->settings) {
        xndaemon->expire_timeout = g_settings_get_int (xndaemon->settings, "expire-timeout");
        if(xndaemon->expire_timeout != -1)
            xndaemon->expire_timeout *= 1000;
    
        xndaemon->initial_opacity = g_settings_get_double (xndaemon->settings, "initial-opacity");
    
        xndaemon->notify_location = g_settings_get_uint(xndaemon->settings, "notify-location");
        xndaemon->do_fadeout = g_settings_get_boolean(xndaemon->settings, "do-fadeout");
        xndaemon->do_slideout = g_settings_get_boolean(xndaemon->settings, "do-slideout");
        xndaemon->primary_monitor = g_settings_get_uint(xndaemon->settings, "primary-monitor");
        xndaemon->do_not_disturb = g_settings_get_boolean(xndaemon->settings, "do-not-disturb");
    
        g_signal_connect(G_OBJECT(xndaemon->settings), "changed::",
                         G_CALLBACK(gooroom_notify_daemon_settings_changed),
                         xndaemon);
    }

    return TRUE;
}

GooroomNotifyDaemon *
gooroom_notify_daemon_new_unique (GError **error)
{
    GooroomNotifyDaemon *xndaemon = g_object_new (GOOROOM_TYPE_NOTIFY_DAEMON, NULL);

    if(!gooroom_notify_daemon_load_config (xndaemon, error))
    {
        g_object_unref (G_OBJECT (xndaemon));
        return NULL;
    }

    return xndaemon;
}
