/*
 *  Copyright (c) 2008-2009 Brian Tarricone <bjt23@cornell.edu>
 *  Copyright (c) 2009 Jérôme Guelfucci <jeromeg@xfce.org>
 *  Copyright (c) 2015 Ali Abdallah    <ali@xfce.org>
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

#include <math.h>

#include "gooroom-notify-window.h"
#include "gooroom-notify-enum-types.h"

#define DEFAULT_EXPIRE_TIMEOUT 10000
#define DEFAULT_NORMAL_OPACITY 0.85
#define DEFAULT_DO_FADEOUT     TRUE
#define DEFAULT_DO_SLIDEOUT    FALSE
#define FADE_TIME              800
#define FADE_CHANGE_TIMEOUT    50
#define DEFAULT_RADIUS         10
#define DEFAULT_PADDING        10.0

struct _GooroomNotifyWindow
{
    GtkWindow parent;

    GdkRectangle geometry;
    gint last_monitor;

    guint expire_timeout;

    gboolean mouse_hover;

    gdouble normal_opacity;
    gint original_x, original_y;

    guint32 icon_only:1,
            has_summary_text:1,
            has_body_text:1,
            has_actions;

    GtkWidget *icon_box;
    GtkWidget *icon;
    GtkWidget *content_box;
    GtkWidget *gauge;
    GtkWidget *summary;
    GtkWidget *body;
    GtkWidget *button_box;

    guint64 expire_start_timestamp;
    guint expire_id;
    guint fade_id;
    guint op_change_steps;
    gdouble op_change_delta;
    gboolean do_fadeout;
    gboolean do_slideout;
    GtkCornerType notify_location;
};

typedef struct
{
    GtkWindowClass parent;

    /*< signals >*/
    void (*closed)(GooroomNotifyWindow *window,
                   GooroomNotifyCloseReason reason);
    void (*action_invoked)(GooroomNotifyWindow *window,
                           const gchar *action_id);
} GooroomNotifyWindowClass;

enum
{
    SIG_CLOSED = 0,
    SIG_ACTION_INVOKED,
    N_SIGS,
};

static void gooroom_notify_window_finalize(GObject *object);

static void gooroom_notify_window_realize(GtkWidget *widget);
static void gooroom_notify_window_unrealize(GtkWidget *widget);
//static gboolean gooroom_notify_window_draw (GtkWidget *widget,
//                                            cairo_t *cr);
static gboolean gooroom_notify_window_enter_leave(GtkWidget *widget,
                                                  GdkEventCrossing *evt);
static gboolean gooroom_notify_window_button_release(GtkWidget *widget,
                                                     GdkEventButton *evt);
static gboolean gooroom_notify_window_configure_event(GtkWidget *widget,
                                                      GdkEventConfigure *evt);
static gboolean gooroom_notify_window_expire_timeout(gpointer data);
static gboolean gooroom_notify_window_fade_timeout(gpointer data);

static void gooroom_notify_window_button_clicked(GtkWidget *widget,
                                                 gpointer user_data);

static guint signals[N_SIGS] = { 0, };


G_DEFINE_TYPE(GooroomNotifyWindow, gooroom_notify_window, GTK_TYPE_WINDOW)


static void
gooroom_notify_window_class_init(GooroomNotifyWindowClass *klass)
{
    GObjectClass *gobject_class = (GObjectClass *)klass;
    GtkWidgetClass *widget_class = (GtkWidgetClass *)klass;

    gobject_class->finalize = gooroom_notify_window_finalize;

    widget_class->realize = gooroom_notify_window_realize;
    widget_class->unrealize = gooroom_notify_window_unrealize;

//    widget_class->draw = gooroom_notify_window_draw;
    widget_class->enter_notify_event = gooroom_notify_window_enter_leave;
    widget_class->leave_notify_event = gooroom_notify_window_enter_leave;
    widget_class->button_release_event = gooroom_notify_window_button_release;
    widget_class->configure_event = gooroom_notify_window_configure_event;

    signals[SIG_CLOSED] = g_signal_new("closed",
                                       GOOROOM_TYPE_NOTIFY_WINDOW,
                                       G_SIGNAL_RUN_LAST,
                                       G_STRUCT_OFFSET(GooroomNotifyWindowClass,
                                                       closed),
                                       NULL, NULL,
                                       g_cclosure_marshal_VOID__ENUM,
                                       G_TYPE_NONE, 1,
                                       GOOROOM_TYPE_NOTIFY_CLOSE_REASON);

    signals[SIG_ACTION_INVOKED] = g_signal_new("action-invoked",
                                               GOOROOM_TYPE_NOTIFY_WINDOW,
                                               G_SIGNAL_RUN_LAST,
                                               G_STRUCT_OFFSET(GooroomNotifyWindowClass,
                                                               action_invoked),
                                               NULL, NULL,
                                               g_cclosure_marshal_VOID__STRING,
                                               G_TYPE_NONE,
                                               1, G_TYPE_STRING);
}

static void
gooroom_notify_window_init(GooroomNotifyWindow *window)
{
    GdkScreen *screen;
    GtkWidget *topvbox, *tophbox, *vbox;
    gint screen_width;
    gdouble padding = DEFAULT_PADDING;
#if GTK_CHECK_VERSION (3, 22, 0)
    GdkMonitor *monitor;
    GdkRectangle geometry;
#endif

    window->expire_timeout = DEFAULT_EXPIRE_TIMEOUT;
    window->normal_opacity = DEFAULT_NORMAL_OPACITY;
    window->do_fadeout = DEFAULT_DO_FADEOUT;
    window->do_slideout = DEFAULT_DO_SLIDEOUT;

    gtk_widget_set_name (GTK_WIDGET(window), "GooroomNotifyWindow");
    gtk_window_set_keep_above(GTK_WINDOW(window), TRUE);
    gtk_window_stick(GTK_WINDOW(window));
    gtk_window_set_decorated(GTK_WINDOW(window), FALSE);
    gtk_window_set_accept_focus(GTK_WINDOW(window), FALSE);
    gtk_window_set_skip_taskbar_hint(GTK_WINDOW(window), TRUE);
    gtk_window_set_skip_pager_hint(GTK_WINDOW(window), TRUE);
    gtk_window_set_resizable(GTK_WINDOW(window), FALSE);
    gtk_window_set_type_hint(GTK_WINDOW(window),
                             GDK_WINDOW_TYPE_HINT_NOTIFICATION);
    gtk_container_set_border_width(GTK_CONTAINER(window), 0);
    gtk_widget_set_app_paintable(GTK_WIDGET(window), TRUE);

    gtk_widget_add_events(GTK_WIDGET(window),
                          GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK
                          | GDK_POINTER_MOTION_MASK);

    screen = gtk_widget_get_screen(GTK_WIDGET(window));

    if(gdk_screen_is_composited(screen)) {
        GdkVisual *visual = gdk_screen_get_rgba_visual (screen);
        if (visual == NULL)
            visual = gdk_screen_get_system_visual (screen);

        gtk_widget_set_visual (GTK_WIDGET(window), visual);
    }

    /* Use the screen width to get a maximum width for the notification bubble.
       This assumes that a character is 10px wide and we want a third of the
       screen as maximum width. */
#if GTK_CHECK_VERSION (3, 22, 0)
    monitor = gdk_display_get_monitor_at_window (gtk_widget_get_display (GTK_WIDGET (window)),
                                                 gdk_screen_get_root_window (screen));
    gdk_monitor_get_geometry (monitor, &geometry);
    screen_width = geometry.width / 30;
#else
    screen_width = gdk_screen_get_width (screen) / 30;
#endif

    /* Get the style context to get style properties */
//    GtkStyleContext *context;
//	context = gtk_widget_get_style_context (GTK_WIDGET (window));
//    gtk_style_context_get (context,
//                           GTK_STATE_FLAG_NORMAL,
//                           "padding", &padding,
//                           NULL);

    topvbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_name (topvbox, "topvbox");
    gtk_box_set_homogeneous (GTK_BOX (topvbox), FALSE);
    gtk_container_set_border_width (GTK_CONTAINER(topvbox), 10);
    gtk_widget_show (topvbox);
    gtk_container_add (GTK_CONTAINER(window), topvbox);
    tophbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_box_set_homogeneous (GTK_BOX (tophbox), FALSE);
    gtk_widget_show (tophbox);
    gtk_container_add (GTK_CONTAINER(topvbox), tophbox);

    window->icon_box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_set_border_width(GTK_CONTAINER(window->icon_box), 0);
    gtk_widget_set_margin_end (GTK_WIDGET (window->icon_box), padding);

    gtk_box_pack_start(GTK_BOX(tophbox), window->icon_box, FALSE, TRUE, 0);

    window->icon = gtk_image_new();
    gtk_widget_show(window->icon);
    gtk_container_add(GTK_CONTAINER(window->icon_box), window->icon);

    window->content_box = vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
    gtk_box_set_homogeneous(GTK_BOX (vbox), FALSE);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 0);
    gtk_widget_show(vbox);
    gtk_box_pack_start(GTK_BOX(topvbox), vbox, TRUE, TRUE, 0);

    window->summary = gtk_label_new(NULL);
    gtk_widget_set_name (window->summary, "summary");
    gtk_label_set_max_width_chars (GTK_LABEL(window->summary), screen_width);
    gtk_label_set_ellipsize (GTK_LABEL (window->summary), PANGO_ELLIPSIZE_END);
    gtk_label_set_line_wrap (GTK_LABEL(window->summary), TRUE);
    gtk_label_set_line_wrap_mode (GTK_LABEL (window->summary), PANGO_WRAP_WORD_CHAR);
    gtk_label_set_lines (GTK_LABEL (window->summary), 1);
    gtk_widget_set_halign (window->summary, GTK_ALIGN_FILL);
#if GTK_CHECK_VERSION (3, 16, 0)
    gtk_label_set_xalign (GTK_LABEL(window->summary), 0);
#endif
    gtk_widget_set_valign (window->summary, GTK_ALIGN_BASELINE);
    gtk_box_pack_start(GTK_BOX(tophbox), window->summary, TRUE, TRUE, 0);

    window->body = gtk_label_new(NULL);
    gtk_widget_set_name (window->body, "body");
    gtk_label_set_max_width_chars (GTK_LABEL(window->body), screen_width);
    gtk_label_set_ellipsize (GTK_LABEL (window->body), PANGO_ELLIPSIZE_END);
    gtk_label_set_line_wrap (GTK_LABEL(window->body), TRUE);
    gtk_label_set_line_wrap_mode (GTK_LABEL (window->body), PANGO_WRAP_WORD_CHAR);
    gtk_label_set_lines (GTK_LABEL (window->body), 6);
    gtk_widget_set_halign (window->body, GTK_ALIGN_FILL);
#if GTK_CHECK_VERSION (3, 16, 0)
    gtk_label_set_xalign (GTK_LABEL(window->body), 0);
#endif
    gtk_widget_set_valign (window->body, GTK_ALIGN_BASELINE);
    gtk_box_pack_start(GTK_BOX(vbox), window->body, TRUE, TRUE, 0);

    window->button_box = gtk_button_box_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_button_box_set_layout(GTK_BUTTON_BOX(window->button_box),
                              GTK_BUTTONBOX_END);
    gtk_box_set_spacing (GTK_BOX(window->button_box), padding / 2);
    gtk_box_set_homogeneous (GTK_BOX(window->button_box), FALSE);
    gtk_box_pack_end (GTK_BOX(topvbox), window->button_box, FALSE, FALSE, 0);
}

static void
gooroom_notify_window_start_expiration(GooroomNotifyWindow *window)
{
    if(window->expire_timeout) {
        GTimeVal ct;
        guint timeout;
        gboolean fade_transparent;

        g_get_current_time(&ct);

        fade_transparent =
            gdk_screen_is_composited(gtk_window_get_screen(GTK_WINDOW (window)));

        if(!fade_transparent)
            timeout = window->expire_timeout;
        else if(window->expire_timeout > FADE_TIME)
            timeout = window->expire_timeout - FADE_TIME;
        else
            timeout = FADE_TIME;

        window->expire_start_timestamp = ct.tv_sec * 1000 + ct.tv_usec / 1000;
        window->expire_id = g_timeout_add(timeout,
                                          gooroom_notify_window_expire_timeout,
                                          window);
    }
    gtk_widget_set_opacity(GTK_WIDGET(window), window->normal_opacity);
}

static void
gooroom_notify_window_finalize(GObject *object)
{
    G_OBJECT_CLASS(gooroom_notify_window_parent_class)->finalize(object);
}

static void
gooroom_notify_window_realize(GtkWidget *widget)
{
    GooroomNotifyWindow *window = GOOROOM_NOTIFY_WINDOW(widget);

    GTK_WIDGET_CLASS(gooroom_notify_window_parent_class)->realize(widget);

    gdk_window_set_type_hint(gtk_widget_get_window(widget),
                             GDK_WINDOW_TYPE_HINT_NOTIFICATION);
    gdk_window_set_override_redirect(gtk_widget_get_window(widget), TRUE);
    gooroom_notify_window_start_expiration(window);
}

static void
gooroom_notify_window_unrealize(GtkWidget *widget)
{
    GooroomNotifyWindow *window = GOOROOM_NOTIFY_WINDOW(widget);

    if(window->fade_id) {
        g_source_remove(window->fade_id);
        window->fade_id = 0;
    }

    if(window->expire_id) {
        g_source_remove(window->expire_id);
        window->expire_id = 0;
    }

    GTK_WIDGET_CLASS(gooroom_notify_window_parent_class)->unrealize(widget);

}

static inline int
get_max_border_width (GtkStyleContext *context,
                      GtkStateFlags state)
{
    GtkBorder border_width;
    gint border_width_max;

    gtk_style_context_save (context);
    gtk_style_context_get_border (context,
                                  state,
                                  &border_width);
    gtk_style_context_restore (context);

    border_width_max = MAX(border_width.left,
                           MAX(border_width.top,
                               MAX(border_width.bottom, border_width.right)));
    return border_width_max;
}


static void
gooroom_notify_window_draw_rectangle (GooroomNotifyWindow *window,
                                     cairo_t *cr)
{
    GtkWidget *widget = GTK_WIDGET(window);
    gint radius = DEFAULT_RADIUS;
    GtkStateFlags state = GTK_STATE_FLAG_NORMAL;
    gint border_width;
    GtkAllocation widget_allocation ;
    GtkStyleContext *context;

    /* this secifies the border_padding from the edges in order to make
     * sure the border completely fits into the drawing area */
    gdouble border_padding = 0.0;

    gtk_widget_get_allocation (widget, &widget_allocation);

    /* Load the css style information for hover aka prelight */
    if (window->mouse_hover)
        state = GTK_STATE_FLAG_PRELIGHT;

    context = gtk_widget_get_style_context (widget);
    /* This is something completely counterintuitive,
     * but in Gtk >= 3.18 calling gtk_style_context_get
     * with a state that is different from the current widget state, causes
     * the widget to redraw itself. Resulting in a storm of draw callbacks.
     * See : https://bugzilla.gnome.org/show_bug.cgi?id=756524 */
    gtk_style_context_save (context);
    gtk_style_context_get (context,
                           state,
                           "border-radius", &radius,
                           NULL);
    gtk_style_context_restore (context);

    border_width = get_max_border_width (context, state);
    border_padding = border_width / 2.0;

    /* Always use a small rounded corners. This should not be necessary in
     * theory, as Adwaita defined border-radius: 0 0 6px 6px; for the
     * app-notification and osd css classes. The problem is that Gtk for some
     * reason gets the border-radius as gint and not as GtkBorder. Getting
     * this way the first value only, which is 0. */
    if ( radius == 0 ) {
        radius = 6;
    }

    cairo_move_to(cr, border_padding, radius + border_padding);
    cairo_arc(cr, radius + border_padding,
              radius + border_padding, radius,
              M_PI, 3.0*M_PI/2.0);
    cairo_line_to(cr,
                  widget_allocation.width - radius - border_padding,
                  border_padding);
    cairo_arc(cr,
              widget_allocation.width - radius - border_padding,
              radius + border_padding, radius,
              3.0*M_PI/2.0, 0.0);
    cairo_line_to(cr, widget_allocation.width - border_padding,
                  widget_allocation.height - radius - border_padding);
    cairo_arc(cr, widget_allocation.width - radius - border_padding,
              widget_allocation.height - radius - border_padding,
              radius, 0.0, M_PI/2.0);
    cairo_line_to(cr, radius + border_padding,
                  widget_allocation.height - border_padding);
    cairo_arc(cr, radius + border_padding,
              widget_allocation.height - radius - border_padding,
              radius, M_PI/2.0, M_PI);
    cairo_close_path(cr);
}

//static gboolean gooroom_notify_window_draw (GtkWidget *widget,
//                                            cairo_t *cr)
//{
//    GtkStyleContext *context;
//    GdkRGBA         *border_color, *bg_color;
//    gint  border_width;
//    GtkStateFlags state;
//    cairo_t         *cr2;
//    cairo_surface_t *surface;
//    cairo_region_t  *region;
//    GtkAllocation    allocation;
//
//    GooroomNotifyWindow *window = GOOROOM_NOTIFY_WINDOW(widget);
//
//    gtk_widget_get_allocation (widget, &allocation);
//
//    /* Create a similar surface as of cr */
//    surface = cairo_surface_create_similar (cairo_get_target (cr),
//                                            CAIRO_CONTENT_COLOR_ALPHA,
//                                            allocation.width,
//                                            allocation.height);
//    cr2 = cairo_create (surface);
//
//    /* Fill first with a transparent background */
//    cairo_rectangle (cr2, 0, 0, allocation.width, allocation.height);
//    cairo_set_source_rgba (cr2, 0.5, 0.5, 0.5, 0.0);
//    cairo_fill (cr2);
//
//    /* Draw a rounded rectangle */
//    gooroom_notify_window_draw_rectangle (window, cr2);
//
//    state = GTK_STATE_FLAG_NORMAL;
//    /* Load the css style information for hover aka prelight */
//    if (window->mouse_hover)
//        state = GTK_STATE_FLAG_PRELIGHT;
//
//    /* Get the style context to get style properties */
//    context = gtk_widget_get_style_context (widget);
//    gtk_style_context_save (context);
//    gtk_style_context_get (context,
//                           state,
//                           "border-color", &border_color,
//                           "background-color", &bg_color,
//                           NULL);
//    gtk_style_context_restore (context);
//
//    /* Draw the background, getting its color from the style context*/
//    cairo_set_source_rgba (cr2,
//                           bg_color->red, bg_color->green, bg_color->blue,
//                           1.0);
//    cairo_fill_preserve (cr2);
//    gdk_rgba_free (bg_color);
//
//    /* Now draw the border */
//    border_width = get_max_border_width (context, state);
//    cairo_set_source_rgba (cr2,
//                           border_color->red, border_color->green, border_color->blue,
//                           1.0);
//    cairo_set_line_width (cr2, border_width);
//    cairo_stroke (cr2);
//    gdk_rgba_free (border_color);
//
//    /* Enough, everything we need has been written on the surface */
//    cairo_destroy (cr2);
//
//    /* Set the surface drawn by cr2, to cr */
//    cairo_save (cr);
//    cairo_set_operator (cr, CAIRO_OPERATOR_SOURCE);
//    cairo_set_source_surface (cr, surface, 0, 0);
//    cairo_paint (cr);
//    cairo_restore (cr);
//
//    region = gdk_cairo_region_create_from_surface (surface);
//    if(!gdk_screen_is_composited(gtk_widget_get_screen(widget)))
//        gtk_widget_shape_combine_region(widget, region);
//
//    /* however, of course always set the input shape; it doesn't matter
//     * if this is a pixel or two off here and there */
//    gtk_widget_input_shape_combine_region(widget, region);
//
//    cairo_surface_destroy (surface);
//    cairo_region_destroy (region);
//
//    GTK_WIDGET_CLASS (gooroom_notify_window_parent_class)->draw (widget, cr);
//
//    return FALSE;
//}

static gboolean
gooroom_notify_window_enter_leave(GtkWidget *widget,
                                  GdkEventCrossing *evt)
{
    GooroomNotifyWindow *window = GOOROOM_NOTIFY_WINDOW(widget);

    if(evt->type == GDK_ENTER_NOTIFY) {
        if(window->expire_timeout) {
            if(window->expire_id) {
                g_source_remove(window->expire_id);
                window->expire_id = 0;
            }
            if(window->fade_id) {
                g_source_remove(window->fade_id);
                window->fade_id = 0;
                /* reset the sliding-out window to its original position */
                if (window->do_slideout)
                    gtk_window_move (GTK_WINDOW (window), window->original_x, window->original_y);
            }
        }
        gtk_widget_set_opacity(GTK_WIDGET(widget), 1.0);
        window->mouse_hover = TRUE;
        gtk_widget_queue_draw(widget);
    } else if(evt->type == GDK_LEAVE_NOTIFY
              && evt->detail != GDK_NOTIFY_INFERIOR)
    {
        gooroom_notify_window_start_expiration(window);
        window->mouse_hover = FALSE;
        gtk_widget_queue_draw(widget);
    }

    return FALSE;
}

static gboolean
gooroom_notify_window_button_release(GtkWidget *widget,
                                     GdkEventButton *evt)
{
    g_signal_emit(G_OBJECT(widget), signals[SIG_CLOSED], 0,
                  GOOROOM_NOTIFY_CLOSE_REASON_DISMISSED);

    return FALSE;
}

static gboolean
gooroom_notify_window_configure_event(GtkWidget *widget,
                                      GdkEventConfigure *evt)
{
    gboolean ret;

    ret = GTK_WIDGET_CLASS(gooroom_notify_window_parent_class)->configure_event(widget,
                                                                             evt);

    gtk_widget_queue_draw(widget);

    return ret;
}



static gboolean
gooroom_notify_window_expire_timeout(gpointer data)
{
    GooroomNotifyWindow *window = data;
    gboolean          fade_transparent;
    gint              animation_timeout;

    g_return_val_if_fail(GOOROOM_IS_NOTIFY_WINDOW(data), FALSE);

    window->expire_id = 0;

    fade_transparent =
        gdk_screen_is_composited(gtk_window_get_screen(GTK_WINDOW(window)));

    if(fade_transparent && window->do_fadeout) {
        /* remember the original position of the window before we slide it out */
        if (window->do_slideout) {
            gtk_window_get_position (GTK_WINDOW (window), &window->original_x, &window->original_y);
            animation_timeout = FADE_CHANGE_TIMEOUT / 2;
        }
        else
            animation_timeout = FADE_CHANGE_TIMEOUT;
        window->fade_id = g_timeout_add(animation_timeout,
                                        gooroom_notify_window_fade_timeout,
                                        window);
    } else {
        /* it might be 800ms early, but that's ok */
        g_signal_emit(G_OBJECT(window), signals[SIG_CLOSED], 0,
                      GOOROOM_NOTIFY_CLOSE_REASON_EXPIRED);
    }

    return FALSE;
}

static gboolean
gooroom_notify_window_fade_timeout(gpointer data)
{
    GooroomNotifyWindow *window = data;
    gdouble op;
    gint x, y;

    g_return_val_if_fail(GOOROOM_IS_NOTIFY_WINDOW(data), FALSE);

    /* slide out animation */
    if (window->do_slideout) {
        gtk_window_get_position (GTK_WINDOW (window), &x, &y);
        if (window->notify_location == GTK_CORNER_TOP_RIGHT ||
            window->notify_location == GTK_CORNER_BOTTOM_RIGHT)
            x = x + 10;
        else if (window->notify_location == GTK_CORNER_TOP_LEFT ||
            window->notify_location == GTK_CORNER_BOTTOM_LEFT)
            x = x - 10;
        else
            g_warning("Invalid notify location: %d", window->notify_location);
        gtk_window_move (GTK_WINDOW (window), x, y);
    }

    /* fade-out animation */
    op = gtk_widget_get_opacity(GTK_WIDGET(window));
    op -= window->op_change_delta;
    if(op < 0.0)
        op = 0.0;

    gtk_widget_set_opacity(GTK_WIDGET(window), op);

    if(op <= 0.0001) {
        window->fade_id = 0;
        g_signal_emit(G_OBJECT(window), signals[SIG_CLOSED], 0,
                      GOOROOM_NOTIFY_CLOSE_REASON_EXPIRED);
        return FALSE;
    }

    return TRUE;
}

static void
gooroom_notify_window_button_clicked(GtkWidget *widget,
                                     gpointer user_data)
{
    GooroomNotifyWindow *window;
    gchar *action_id;

    g_return_if_fail(GOOROOM_IS_NOTIFY_WINDOW(user_data));

    window = GOOROOM_NOTIFY_WINDOW(user_data);

    action_id = g_object_get_data(G_OBJECT(widget), "--action-id");
    g_assert(action_id);

    g_signal_emit(G_OBJECT(window), signals[SIG_ACTION_INVOKED], 0,
                  action_id);
    g_signal_emit(G_OBJECT(window), signals[SIG_CLOSED], 0,
                  GOOROOM_NOTIFY_CLOSE_REASON_DISMISSED);
}

GtkWidget *
gooroom_notify_window_new(void)
{
    return gooroom_notify_window_new_with_actions(NULL, NULL, NULL, -1, NULL/*, NULL*/);
}

GtkWidget *
gooroom_notify_window_new_full(const gchar *summary,
                               const gchar *body,
                               const gchar *icon_name,
                               gint expire_timeout)
{
    return gooroom_notify_window_new_with_actions(summary, body,
                                                  icon_name,
                                                  expire_timeout,
                                                  NULL/*,
                                                  NULL*/);
}

GtkWidget *
gooroom_notify_window_new_with_actions(const gchar *summary,
                                       const gchar *body,
                                       const gchar *icon_name,
                                       gint expire_timeout,
                                       const gchar **actions)
{
    GooroomNotifyWindow *window;

    window = g_object_new(GOOROOM_TYPE_NOTIFY_WINDOW,
                          "type", GTK_WINDOW_TOPLEVEL, NULL);

    gooroom_notify_window_set_summary(window, summary);
    gooroom_notify_window_set_body(window, body);
    gooroom_notify_window_set_icon_name(window, icon_name);
    gooroom_notify_window_set_expire_timeout(window, expire_timeout);
    gooroom_notify_window_set_actions(window, actions/*, css_provider*/);

    return GTK_WIDGET(window);
}

void
gooroom_notify_window_set_summary(GooroomNotifyWindow *window,
                                  const gchar *summary)
{
    g_return_if_fail(GOOROOM_IS_NOTIFY_WINDOW(window));

    gtk_label_set_text(GTK_LABEL(window->summary), summary);
    if(summary && *summary) {
        gtk_widget_show(window->summary);
        window->has_summary_text = TRUE;
    } else {
        gtk_widget_hide(window->summary);
        window->has_summary_text = FALSE;
    }

    if(gtk_widget_get_realized(GTK_WIDGET(window)))
        gtk_widget_queue_draw(GTK_WIDGET(window));
}

void
gooroom_notify_window_set_body(GooroomNotifyWindow *window,
                               const gchar *body)
{
    g_return_if_fail(GOOROOM_IS_NOTIFY_WINDOW(window));

    if(body && *body) {
        /* Try to set the body with markup and in case this fails (empty label)
           fall back to escaping the whole string and showing it plainly.
           This equals pango_parse_markup extended by checking for valid hyperlinks
           (which is not supported by pango). */
        gtk_label_set_markup (GTK_LABEL (window->body), body);
        if (g_strcmp0 (gtk_label_get_text(GTK_LABEL (window->body)), "") == 0 ) {
            gchar *tmp;
            tmp = g_markup_escape_text (body, -1);
            gtk_label_set_text (GTK_LABEL (window->body), body);
            g_free (tmp);
        }
        gtk_widget_show(window->body);
        window->has_body_text = TRUE;
    } else {
        gtk_label_set_markup(GTK_LABEL(window->body), "");
        gtk_widget_hide(window->body);
        window->has_body_text = FALSE;
    }

    if(gtk_widget_get_realized(GTK_WIDGET(window)))
        gtk_widget_queue_draw(GTK_WIDGET(window));
}

void
gooroom_notify_window_set_geometry(GooroomNotifyWindow *window,
                                   GdkRectangle rectangle)
{
    window->geometry = rectangle;
}

GdkRectangle *
gooroom_notify_window_get_geometry (GooroomNotifyWindow *window)
{
   return &window->geometry;
}

void
gooroom_notify_window_set_last_monitor(GooroomNotifyWindow *window,
                                       gint monitor)
{
    window->last_monitor = monitor;
}

gint
gooroom_notify_window_get_last_monitor(GooroomNotifyWindow *window)
{
   return window->last_monitor;
}

void
gooroom_notify_window_set_icon_name (GooroomNotifyWindow *window,
                                     const gchar *icon_name)
{
    gboolean icon_set = FALSE;
    gchar *filename;

    g_return_if_fail (GOOROOM_IS_NOTIFY_WINDOW (window));

    if (icon_name && *icon_name) {
        gint w, h;
        GdkPixbuf *pix = NULL;
        GIcon *icon;

        gtk_icon_size_lookup (GTK_ICON_SIZE_DIALOG, &w, &h);

        if (g_path_is_absolute (icon_name)) {
            pix = gdk_pixbuf_new_from_file_at_size (icon_name, w, h, NULL);
        }
        else if (g_str_has_prefix (icon_name, "file://")) {
            filename = g_filename_from_uri (icon_name, NULL, NULL);
            if (filename)
              pix = gdk_pixbuf_new_from_file_at_size (filename, w, h, NULL);
            g_free (filename);
        }
        else {
            icon = g_themed_icon_new_with_default_fallbacks (icon_name);
            gtk_image_set_from_gicon (GTK_IMAGE (window->icon), icon, GTK_ICON_SIZE_DIALOG);
            icon_set = TRUE;
        }

        if (pix) {
            gtk_image_set_from_pixbuf (GTK_IMAGE (window->icon), pix);
            g_object_unref (G_OBJECT (pix));
            icon_set = TRUE;
        }
    }

    if (icon_set)
        gtk_widget_show (window->icon_box);
    else {
        gtk_image_clear (GTK_IMAGE (window->icon));
        gtk_widget_hide (window->icon_box);
    }

    if (gtk_widget_get_realized (GTK_WIDGET (window)))
        gtk_widget_queue_draw (GTK_WIDGET (window));
}

void
gooroom_notify_window_set_icon_pixbuf(GooroomNotifyWindow *window,
                                      GdkPixbuf *pixbuf)
{
    GdkPixbuf *p_free = NULL;

    g_return_if_fail(GOOROOM_IS_NOTIFY_WINDOW(window)
                     && (!pixbuf || GDK_IS_PIXBUF(pixbuf)));

    if(pixbuf) {
        gint w, h, pw, ph;

        gtk_icon_size_lookup(GTK_ICON_SIZE_DIALOG, &w, &h);
        pw = gdk_pixbuf_get_width(pixbuf);
        ph = gdk_pixbuf_get_height(pixbuf);

        if(w > h)
            w = h;
        if(pw > w || ph > w) {
            gint nw, nh;

            if(pw > ph) {
                nw = w;
                nh = w * ((gdouble)ph/pw);
            } else {
                nw = w * ((gdouble)pw/ph);
                nh = w;
            }

            pixbuf = p_free = gdk_pixbuf_scale_simple(pixbuf, nw, nh,
                                                      GDK_INTERP_BILINEAR);
        }
    }

    gtk_image_set_from_pixbuf(GTK_IMAGE(window->icon), pixbuf);

    if(pixbuf)
        gtk_widget_show(window->icon_box);
    else
        gtk_widget_hide(window->icon_box);

    if(gtk_widget_get_realized(GTK_WIDGET(window)))
        gtk_widget_queue_draw(GTK_WIDGET(window));

    if(p_free)
        g_object_unref(G_OBJECT(p_free));
}

void
gooroom_notify_window_set_expire_timeout(GooroomNotifyWindow *window,
                                         gint expire_timeout)
{
    g_return_if_fail(GOOROOM_IS_NOTIFY_WINDOW(window));

    if(expire_timeout >= 0)
        window->expire_timeout = expire_timeout;
    else
        window->expire_timeout = DEFAULT_EXPIRE_TIMEOUT;

    if(gtk_widget_get_realized(GTK_WIDGET(window))) {
        if(window->expire_id) {
            g_source_remove(window->expire_id);
            window->expire_id = 0;
        }
        if(window->fade_id) {
            g_source_remove(window->fade_id);
            window->fade_id = 0;
        }
        gtk_widget_set_opacity(GTK_WIDGET(window), window->normal_opacity);

        gooroom_notify_window_start_expiration (window);
    }
}

void
gooroom_notify_window_set_actions(GooroomNotifyWindow *window,
                                  const gchar **actions/*,
                                  GtkCssProvider *css_provider*/)
{
    gint i;
    GList *children, *l;

    g_return_if_fail(GOOROOM_IS_NOTIFY_WINDOW(window));

    children = gtk_container_get_children(GTK_CONTAINER(window->button_box));
    for(l = children; l; l = l->next)
        gtk_widget_destroy(GTK_WIDGET(l->data));
    g_list_free(children);

    if(!actions) {
        gtk_widget_hide(window->button_box);
        window->has_actions = FALSE;
    } else {
        gtk_widget_show(window->button_box);
        window->has_actions = TRUE;
    }

    for(i = 0; actions && actions[i]; i += 2) {
        const gchar *cur_action_id = actions[i];
        const gchar *cur_button_text = actions[i+1];
        GtkWidget *btn, *lbl;
        gchar *cur_button_text_escaped;

        if(!cur_button_text || !cur_action_id || !*cur_action_id)
            break;
        /* Gnome applications seem to send a "default" action which often has no
           label or text, because it is intended to be executed when clicking
           the notification window.
           See https://developer.gnome.org/notification-spec/
           As we do not support this for the moment we hide buttons without labels. */
        if (g_strcmp0 (cur_button_text, "") == 0)
            continue;

        gdouble padding;
//        gtk_widget_style_get(GTK_WIDGET(window),
//                             "padding", &padding,
//                             NULL);

        btn = gtk_button_new();
        g_object_set_data_full(G_OBJECT(btn), "--action-id",
                               g_strdup(cur_action_id),
                               (GDestroyNotify)g_free);
        gtk_widget_show(btn);
        gtk_widget_set_margin_top (btn, padding / 2);
        gtk_container_add(GTK_CONTAINER(window->button_box), btn);
        g_signal_connect(G_OBJECT(btn), "clicked",
                         G_CALLBACK(gooroom_notify_window_button_clicked),
                         window);

        cur_button_text_escaped = g_markup_printf_escaped("<span size='small'>%s</span>",
                                                          cur_button_text);

        lbl = gtk_label_new(NULL);
        gtk_label_set_markup(GTK_LABEL(lbl), cur_button_text_escaped);
        gtk_label_set_use_markup(GTK_LABEL(lbl), TRUE);
        gtk_widget_show(lbl);
        gtk_container_add(GTK_CONTAINER(btn), lbl);

        g_free(cur_button_text_escaped);
    }

    if(gtk_widget_get_realized(GTK_WIDGET(window)))
        gtk_widget_queue_draw(GTK_WIDGET(window));
}

void
gooroom_notify_window_set_opacity(GooroomNotifyWindow *window,
                                  gdouble opacity)
{
    g_return_if_fail(GOOROOM_IS_NOTIFY_WINDOW(window));

    if(opacity > 1.0)
        opacity = 1.0;
    else if(opacity < 0.0)
        opacity = 0.0;

    window->normal_opacity = opacity;
    window->op_change_steps = FADE_TIME / FADE_CHANGE_TIMEOUT;
    window->op_change_delta = opacity / window->op_change_steps;

    if(gtk_widget_get_realized(GTK_WIDGET(window)) && window->expire_id && !window->fade_id)
        gtk_widget_set_opacity(GTK_WIDGET(window), window->normal_opacity);
}

gdouble
gooroom_notify_window_get_opacity(GooroomNotifyWindow *window)
{
    g_return_val_if_fail(GOOROOM_IS_NOTIFY_WINDOW(window), 0.0);
    return window->normal_opacity;
}

void
gooroom_notify_window_set_icon_only(GooroomNotifyWindow *window,
                                    gboolean icon_only)
{
    g_return_if_fail(GOOROOM_IS_NOTIFY_WINDOW(window));

    if(icon_only == window->icon_only)
        return;

    window->icon_only = !!icon_only;

    if(icon_only) {
        GtkRequisition req;

        if(!gtk_widget_get_visible(window->icon_box)) {
            g_warning("Attempt to set icon-only mode with no icon");
            return;
        }

        gtk_widget_hide(window->content_box);

        /* set a wider size on the icon box so it takes up more space */
        gtk_widget_realize(window->icon);
        gtk_widget_get_preferred_size (window->icon, NULL, &req);
        gtk_widget_set_size_request(window->icon_box, req.width * 4, -1);
        /* and center it */
        g_object_set (window->icon_box,
                      "halign", GTK_ALIGN_CENTER,
                      NULL);
    } else {
        g_object_set (window->icon_box,
                      "halign", GTK_ALIGN_START,
                      NULL);
        gtk_widget_set_size_request(window->icon_box, -1, -1);
        gtk_widget_show(window->content_box);
    }
}

void
gooroom_notify_window_set_gauge_value(GooroomNotifyWindow *window,
                                      gint value)
{
    g_return_if_fail(GOOROOM_IS_NOTIFY_WINDOW(window));

    /* maybe want to do some kind of effect if the value is out of bounds */
    if(value > 100)
        value = 100;
    else if(value < 0)
        value = 0;

    gtk_widget_hide(window->summary);
    gtk_widget_hide(window->body);
    gtk_widget_hide(window->button_box);

    if(!window->gauge) {
        GtkWidget *box;
        gint width;

        if(gtk_widget_get_visible(window->icon)) {
            /* size the pbar in relation to the icon */
            GtkRequisition req;

            gtk_widget_realize(window->icon);
            gtk_widget_get_preferred_size(window->icon, NULL, &req);
            width = req.width * 4;
        } else {
            /* FIXME: do something less arbitrary */
            width = 120;
        }

        box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
        gtk_widget_show(box);

        g_object_set(box, "valign", GTK_ALIGN_CENTER, NULL);
        gtk_box_pack_start(GTK_BOX(window->content_box), box, TRUE, TRUE, 0);

        window->gauge = gtk_progress_bar_new();
        gtk_widget_set_size_request(window->gauge, width, -1);
        gtk_widget_show(window->gauge);
        gtk_container_add(GTK_CONTAINER(box), window->gauge);
    }

    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(window->gauge),
                                  value / 100.0);
}

void
gooroom_notify_window_unset_gauge_value(GooroomNotifyWindow *window)
{
    g_return_if_fail(GOOROOM_IS_NOTIFY_WINDOW(window));

    if(window->gauge) {
        GtkWidget *align = gtk_widget_get_parent(window->gauge);

        g_assert(align);

        gtk_widget_destroy(align);
        window->gauge = NULL;

        if(window->has_summary_text)
            gtk_widget_show(window->summary);
        if(window->has_body_text)
            gtk_widget_show(window->body);
        if(window->has_actions)
            gtk_widget_show(window->button_box);
    }
}

void
gooroom_notify_window_set_do_fadeout(GooroomNotifyWindow *window,
                                     gboolean do_fadeout,
                                     gboolean do_slideout)
{
    g_return_if_fail(GOOROOM_IS_NOTIFY_WINDOW(window));

    window->do_fadeout = do_fadeout;
    window->do_slideout = do_slideout;
}

void gooroom_notify_window_set_notify_location(GooroomNotifyWindow *window,
                                            GtkCornerType notify_location)
{
    g_return_if_fail(GOOROOM_IS_NOTIFY_WINDOW(window));

    window->notify_location = notify_location;
}

void
gooroom_notify_window_closed(GooroomNotifyWindow *window,
                             GooroomNotifyCloseReason reason)
{
    g_return_if_fail(GOOROOM_IS_NOTIFY_WINDOW(window)
                     && reason >= GOOROOM_NOTIFY_CLOSE_REASON_EXPIRED
                     && reason <= GOOROOM_NOTIFY_CLOSE_REASON_UNKNOWN);

    g_signal_emit(G_OBJECT(window), signals[SIG_CLOSED], 0, reason);
}
