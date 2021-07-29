/*
 *  Copyright (c) 2008-2009 Brian Tarricone <bjt23@cornell.edu>
 *  Copyright (c) 2009 Jérôme Guelfucci <jeromeg@xfce.org>
 *  Copyright (c) 2015 Ali Abdallah    <ali@xfce.org>
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

struct _GooroomNotifyWindowPrivate
{
	GdkRectangle geometry;
	gint last_monitor;

	guint expire_timeout;

	gdouble normal_opacity;
	gint original_x, original_y;

	guint32 icon_only:1,
            has_summary_text:1,
            has_body_text:1,
            has_actions;

	GtkWidget *main_box;
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

enum
{
    SIG_CLOSED = 0,
    SIG_ACTION_INVOKED,
    N_SIGS,
};

static guint signals[N_SIGS] = { 0, };


static void gooroom_notify_window_finalize(GObject *object);
static void gooroom_notify_window_realize(GtkWidget *widget);
static void gooroom_notify_window_unrealize(GtkWidget *widget);
static gboolean gooroom_notify_window_enter_leave(GtkWidget *widget, GdkEventCrossing *evt);
static gboolean gooroom_notify_window_button_release(GtkWidget *widget, GdkEventButton *evt);
static gboolean gooroom_notify_window_configure_event(GtkWidget *widget, GdkEventConfigure *evt);
static gboolean gooroom_notify_window_expire_timeout(gpointer data);
static gboolean gooroom_notify_window_fade_timeout(gpointer data);
static void gooroom_notify_window_button_clicked(GtkWidget *widget, gpointer user_data);



G_DEFINE_TYPE_WITH_PRIVATE (GooroomNotifyWindow, gooroom_notify_window, GTK_TYPE_WINDOW)



static void
gooroom_notify_window_start_expiration (GooroomNotifyWindow *window)
{
	GooroomNotifyWindowPrivate *priv = window->priv;

	if(priv->expire_timeout) {
		gint64 ct;
		guint timeout;
		gboolean fade_transparent;

		ct = g_get_real_time ();

		fade_transparent = gdk_screen_is_composited (gtk_window_get_screen (GTK_WINDOW (window)));

		if (!fade_transparent)
			timeout = priv->expire_timeout;
		else if (priv->expire_timeout > FADE_TIME)
			timeout = priv->expire_timeout - FADE_TIME;
		else
			timeout = FADE_TIME;

		priv->expire_start_timestamp = ct / 1000;
		priv->expire_id = g_timeout_add (timeout, gooroom_notify_window_expire_timeout, window);
	}

	gtk_widget_set_opacity(GTK_WIDGET(window), priv->normal_opacity);
}

static void
gooroom_notify_window_finalize (GObject *object)
{
	G_OBJECT_CLASS (gooroom_notify_window_parent_class)->finalize (object);
}

static void
gooroom_notify_window_realize (GtkWidget *widget)
{
	GooroomNotifyWindow *window = GOOROOM_NOTIFY_WINDOW (widget);

	GTK_WIDGET_CLASS (gooroom_notify_window_parent_class)->realize (widget);

	gdk_window_set_type_hint (gtk_widget_get_window(widget),
                              GDK_WINDOW_TYPE_HINT_NOTIFICATION);
	gdk_window_set_override_redirect (gtk_widget_get_window (widget), TRUE);

	gooroom_notify_window_start_expiration (window);
}

static void
gooroom_notify_window_unrealize (GtkWidget *widget)
{
	GooroomNotifyWindow *window = GOOROOM_NOTIFY_WINDOW (widget);
	GooroomNotifyWindowPrivate *priv = window->priv;

	if (priv->fade_id) {
		g_source_remove (priv->fade_id);
		priv->fade_id = 0;
	}

	if (priv->expire_id) {
		g_source_remove (priv->expire_id);
		priv->expire_id = 0;
	}

	GTK_WIDGET_CLASS (gooroom_notify_window_parent_class)->unrealize(widget);
}

static inline int
get_max_border_width (GtkStyleContext *context,
                      GtkStateFlags state)
{
	GtkBorder border_width;
	gint border_width_max;

	gtk_style_context_save (context);
	gtk_style_context_get_border (context, state, &border_width);
	gtk_style_context_restore (context);

	border_width_max = MAX (border_width.left,
                       MAX (border_width.top,
                       MAX (border_width.bottom, border_width.right)));
	return border_width_max;
}

static gboolean
gooroom_notify_window_enter_leave (GtkWidget *widget,
                                   GdkEventCrossing *evt)
{
	GtkStateFlags state_flags;
	GtkStyleContext *style_context;

	GooroomNotifyWindow *window = GOOROOM_NOTIFY_WINDOW (widget);
	GooroomNotifyWindowPrivate *priv = window->priv;

	style_context = gtk_widget_get_style_context (priv->main_box);
	state_flags = gtk_style_context_get_state (style_context);

	if (evt->type == GDK_ENTER_NOTIFY) {
		gtk_style_context_set_state (style_context, state_flags | GTK_STATE_FLAG_PRELIGHT);

		if (priv->expire_timeout) {
			if (priv->expire_id) {
				g_source_remove (priv->expire_id);
				priv->expire_id = 0;
			}
			if (priv->fade_id) {
				g_source_remove (priv->fade_id);
				priv->fade_id = 0;
				/* reset the sliding-out window to its original position */
				if (priv->do_slideout)
					gtk_window_move (GTK_WINDOW (window), priv->original_x, priv->original_y);
			}
		}
	} else if (evt->type == GDK_LEAVE_NOTIFY && evt->detail != GDK_NOTIFY_INFERIOR) {
		gtk_style_context_set_state (style_context, state_flags & ~GTK_STATE_FLAG_PRELIGHT);

		gooroom_notify_window_start_expiration (window);
	}

	return FALSE;
}

static gboolean
gooroom_notify_window_button_press (GtkWidget *widget, GdkEventButton *evt)
{
	GtkStateFlags flags;
	GtkStyleContext *context;

	GooroomNotifyWindow *window = GOOROOM_NOTIFY_WINDOW (widget);
	GooroomNotifyWindowPrivate *priv = window->priv;

	context = gtk_widget_get_style_context (priv->main_box);
	flags = gtk_style_context_get_state (context);

	gtk_style_context_set_state (context, flags | GTK_STATE_FLAG_ACTIVE);

    return FALSE;
}

static gboolean
gooroom_notify_window_button_release (GtkWidget *widget, GdkEventButton *evt)
{
	GtkStateFlags flags;
	GtkStyleContext *context;

	GooroomNotifyWindow *window = GOOROOM_NOTIFY_WINDOW (widget);
	GooroomNotifyWindowPrivate *priv = window->priv;

	context = gtk_widget_get_style_context (priv->main_box);
	flags = gtk_style_context_get_state (context);

	gtk_style_context_set_state (context, flags & ~GTK_STATE_FLAG_ACTIVE);

	g_signal_emit (G_OBJECT(widget), signals[SIG_CLOSED], 0,
                   GOOROOM_NOTIFY_CLOSE_REASON_DISMISSED);

    return FALSE;
}

static gboolean
gooroom_notify_window_configure_event (GtkWidget *widget,
                                       GdkEventConfigure *evt)
{
	gboolean ret;

	ret = GTK_WIDGET_CLASS (gooroom_notify_window_parent_class)->configure_event (widget, evt);

	gtk_widget_queue_draw (widget);

	return ret;
}

static gboolean
gooroom_notify_window_expire_timeout (gpointer user_data)
{
	gboolean          fade_transparent;
	gint              animation_timeout;
	GooroomNotifyWindow *window = GOOROOM_NOTIFY_WINDOW (user_data);
	GooroomNotifyWindowPrivate *priv = window->priv;

	priv->expire_id = 0;

	fade_transparent = gdk_screen_is_composited (gtk_window_get_screen (GTK_WINDOW (window)));

	if(fade_transparent && priv->do_fadeout) {
		/* remember the original position of the window before we slide it out */
		if (priv->do_slideout) {
			gtk_window_get_position (GTK_WINDOW (window), &priv->original_x, &priv->original_y);
			animation_timeout = FADE_CHANGE_TIMEOUT / 2;
		}
		else
			animation_timeout = FADE_CHANGE_TIMEOUT;
		priv->fade_id = g_timeout_add (animation_timeout, gooroom_notify_window_fade_timeout, window);
	} else {
		/* it might be 800ms early, but that's ok */
		g_signal_emit (G_OBJECT(window), signals[SIG_CLOSED], 0,
                       GOOROOM_NOTIFY_CLOSE_REASON_EXPIRED);
    }

    return FALSE;
}

static gboolean
gooroom_notify_window_fade_timeout (gpointer user_data)
{
	gdouble op;
	gint x, y;
	GooroomNotifyWindow *window = GOOROOM_NOTIFY_WINDOW (user_data);
	GooroomNotifyWindowPrivate *priv = window->priv;

    /* slide out animation */
	if (priv->do_slideout) {
		gtk_window_get_position (GTK_WINDOW (window), &x, &y);
		if (priv->notify_location == GTK_CORNER_TOP_RIGHT ||
            priv->notify_location == GTK_CORNER_BOTTOM_RIGHT)
            x = x + 10;
        else if (priv->notify_location == GTK_CORNER_TOP_LEFT ||
                 priv->notify_location == GTK_CORNER_BOTTOM_LEFT)
			x = x - 10;
		else
			g_warning ("Invalid notify location: %d", priv->notify_location);

		gtk_window_move (GTK_WINDOW (window), x, y);
	}

	/* fade-out animation */
	op = gtk_widget_get_opacity (GTK_WIDGET (window));
	op -= priv->op_change_delta;
	if(op < 0.0)
		op = 0.0;

	gtk_widget_set_opacity (GTK_WIDGET (window), op);

	if (op <= 0.0001) {
		priv->fade_id = 0;
		g_signal_emit (G_OBJECT (window), signals[SIG_CLOSED], 0,
				GOOROOM_NOTIFY_CLOSE_REASON_EXPIRED);
		return FALSE;
	}

	return TRUE;
}

static void
gooroom_notify_window_button_clicked (GtkWidget *widget,
                                      gpointer user_data)
{
	GooroomNotifyWindow *window;
	gchar *action_id;

	window = GOOROOM_NOTIFY_WINDOW (user_data);

	action_id = g_object_get_data (G_OBJECT(widget), "--action-id");
	g_assert (action_id);

	g_signal_emit (G_OBJECT (window), signals[SIG_ACTION_INVOKED], 0, action_id);
	g_signal_emit (G_OBJECT (window), signals[SIG_CLOSED], 0,
                   GOOROOM_NOTIFY_CLOSE_REASON_DISMISSED);
}

static void
gooroom_notify_window_init (GooroomNotifyWindow *window)
{
	gint max_chars;
	GdkScreen *screen;
	GdkMonitor *monitor;
	GdkRectangle geometry;
	GtkCssProvider *provider;
	GooroomNotifyWindowPrivate *priv;

	priv = window->priv = gooroom_notify_window_get_instance_private (window);

	priv->expire_timeout = DEFAULT_EXPIRE_TIMEOUT;
	priv->normal_opacity = DEFAULT_NORMAL_OPACITY;
	priv->do_fadeout = DEFAULT_DO_FADEOUT;
	priv->do_slideout = DEFAULT_DO_SLIDEOUT;

	gtk_widget_init_template (GTK_WIDGET (window));

	gtk_window_set_keep_above (GTK_WINDOW (window), TRUE);
	gtk_window_stick (GTK_WINDOW (window));
	gtk_window_set_decorated (GTK_WINDOW (window), FALSE);
	gtk_window_set_accept_focus (GTK_WINDOW (window), FALSE);
	gtk_window_set_skip_taskbar_hint (GTK_WINDOW (window), TRUE);
	gtk_window_set_skip_pager_hint (GTK_WINDOW (window), TRUE);
	gtk_window_set_resizable (GTK_WINDOW (window), FALSE);
	gtk_window_set_type_hint (GTK_WINDOW (window), GDK_WINDOW_TYPE_HINT_NOTIFICATION);
	gtk_widget_set_app_paintable (GTK_WIDGET (window), TRUE);

	screen = gtk_widget_get_screen (GTK_WIDGET (window));
	if (gdk_screen_is_composited (screen)) {
		GdkVisual *visual = gdk_screen_get_rgba_visual (screen);
		if (visual == NULL)
			visual = gdk_screen_get_system_visual (screen);

		gtk_widget_set_visual (GTK_WIDGET (window), visual);
	}

	monitor = gdk_display_get_monitor_at_window (gtk_widget_get_display (GTK_WIDGET (window)),
                                                 gdk_screen_get_root_window (screen));
	gdk_monitor_get_geometry (monitor, &geometry);
	max_chars = geometry.width / 40;

	gtk_label_set_max_width_chars (GTK_LABEL (priv->summary), max_chars);
	gtk_label_set_max_width_chars (GTK_LABEL (priv->body), max_chars);

	provider = gtk_css_provider_new ();
	gtk_css_provider_load_from_resource (provider, "/kr/gooroom/notifyd/theme.css");
	gtk_style_context_add_provider_for_screen (screen,
                                               GTK_STYLE_PROVIDER (provider),
                                               GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
	g_clear_object (&provider);
}

static void
gooroom_notify_window_class_init (GooroomNotifyWindowClass *klass)
{
	GObjectClass *gobject_class = (GObjectClass *)klass;
	GtkWidgetClass *widget_class = (GtkWidgetClass *)klass;

	gobject_class->finalize = gooroom_notify_window_finalize;

	widget_class->realize = gooroom_notify_window_realize;
	widget_class->unrealize = gooroom_notify_window_unrealize;

	widget_class->enter_notify_event = gooroom_notify_window_enter_leave;
	widget_class->leave_notify_event = gooroom_notify_window_enter_leave;
	widget_class->button_press_event = gooroom_notify_window_button_press;
	widget_class->button_release_event = gooroom_notify_window_button_release;
	widget_class->configure_event = gooroom_notify_window_configure_event;

	gtk_widget_class_set_template_from_resource (widget_class,
			"/kr/gooroom/notifyd/gooroom-notify-window.ui");

	gtk_widget_class_bind_template_child_private (widget_class, GooroomNotifyWindow, main_box);
	gtk_widget_class_bind_template_child_private (widget_class, GooroomNotifyWindow, icon_box);
    gtk_widget_class_bind_template_child_private (widget_class, GooroomNotifyWindow, icon);
    gtk_widget_class_bind_template_child_private (widget_class, GooroomNotifyWindow, content_box);
    gtk_widget_class_bind_template_child_private (widget_class, GooroomNotifyWindow, summary);
    gtk_widget_class_bind_template_child_private (widget_class, GooroomNotifyWindow, body);
    gtk_widget_class_bind_template_child_private (widget_class, GooroomNotifyWindow, button_box);

	signals[SIG_CLOSED] = g_signal_new ("closed",
                                        GOOROOM_TYPE_NOTIFY_WINDOW,
                                        G_SIGNAL_RUN_LAST,
                                        G_STRUCT_OFFSET (GooroomNotifyWindowClass, closed),
                                        NULL, NULL,
                                        g_cclosure_marshal_VOID__ENUM,
                                        G_TYPE_NONE, 1,
                                        GOOROOM_TYPE_NOTIFY_CLOSE_REASON);

	signals[SIG_ACTION_INVOKED] = g_signal_new ("action-invoked",
                                                GOOROOM_TYPE_NOTIFY_WINDOW,
                                                G_SIGNAL_RUN_LAST,
                                                G_STRUCT_OFFSET (GooroomNotifyWindowClass,
                                                                 action_invoked),
                                                NULL, NULL,
                                                g_cclosure_marshal_VOID__STRING,
                                                G_TYPE_NONE,
                                                1, G_TYPE_STRING);
}

GtkWidget *
gooroom_notify_window_new (void)
{
	return gooroom_notify_window_new_with_actions (NULL, NULL, NULL, -1, NULL);
}

GtkWidget *
gooroom_notify_window_new_full (const gchar *summary,
                                const gchar *body,
                                const gchar *icon_name,
                                gint expire_timeout)
{
	return gooroom_notify_window_new_with_actions (summary, body,
                                                   icon_name,
                                                   expire_timeout,
                                                   NULL);
}

GtkWidget *
gooroom_notify_window_new_with_actions (const gchar *summary,
                                        const gchar *body,
                                        const gchar *icon_name,
                                        gint expire_timeout,
                                        const gchar **actions)
{
    GooroomNotifyWindow *window;

    window = g_object_new (GOOROOM_TYPE_NOTIFY_WINDOW,
                          "type", GTK_WINDOW_TOPLEVEL, NULL);

    gooroom_notify_window_set_summary (window, summary);
    gooroom_notify_window_set_body (window, body);
    gooroom_notify_window_set_icon_name (window, icon_name);
    gooroom_notify_window_set_expire_timeout (window, expire_timeout);
    gooroom_notify_window_set_actions (window, actions);

    return GTK_WIDGET (window);
}

void
gooroom_notify_window_set_summary (GooroomNotifyWindow *window,
                                   const gchar *summary)
{
	GooroomNotifyWindowPrivate *priv = window->priv;

	gtk_label_set_text (GTK_LABEL (priv->summary), summary);
	if (summary && *summary) {
		gtk_widget_show (priv->summary);
		priv->has_summary_text = TRUE;
	} else {
		gtk_widget_hide (priv->summary);
		priv->has_summary_text = FALSE;
	}

	if (gtk_widget_get_realized (GTK_WIDGET(window)))
		gtk_widget_queue_draw (GTK_WIDGET (window));
}

void
gooroom_notify_window_set_body (GooroomNotifyWindow *window,
                                const gchar *body)
{
	GooroomNotifyWindowPrivate *priv = window->priv;

	if (body && *body) {
        /* Try to set the body with markup and in case this fails (empty label)
           fall back to escaping the whole string and showing it plainly.
           This equals pango_parse_markup extended by checking for valid hyperlinks
           (which is not supported by pango). */
		gtk_label_set_markup (GTK_LABEL (priv->body), body);
		if (g_strcmp0 (gtk_label_get_text(GTK_LABEL (priv->body)), "") == 0 ) {
			gchar *tmp;
			tmp = g_markup_escape_text (body, -1);
			gtk_label_set_text (GTK_LABEL (priv->body), body);
			g_free (tmp);
		}
		gtk_widget_show (priv->body);
		priv->has_body_text = TRUE;
	} else {
		gtk_label_set_markup (GTK_LABEL (priv->body), "");
		gtk_widget_hide (priv->body);
		priv->has_body_text = FALSE;
	}

	if (gtk_widget_get_realized (GTK_WIDGET (window)))
		gtk_widget_queue_draw (GTK_WIDGET (window));
}

void
gooroom_notify_window_set_geometry(GooroomNotifyWindow *window,
                                   GdkRectangle rectangle)
{
	window->priv->geometry = rectangle;
}

GdkRectangle *
gooroom_notify_window_get_geometry (GooroomNotifyWindow *window)
{
	return &window->priv->geometry;
}

void
gooroom_notify_window_set_last_monitor (GooroomNotifyWindow *window,
                                        gint monitor)
{
	window->priv->last_monitor = monitor;
}

gint
gooroom_notify_window_get_last_monitor (GooroomNotifyWindow *window)
{
	return window->priv->last_monitor;
}

void
gooroom_notify_window_set_icon_name (GooroomNotifyWindow *window,
                                     const gchar         *icon_name)
{
	gchar *filename;
	gboolean icon_set = FALSE;
	GooroomNotifyWindowPrivate *priv = window->priv;

	if (icon_name && *icon_name) {
		gint w, h;
		GdkPixbuf *pix = NULL;
		GIcon *icon;

		gtk_icon_size_lookup (GTK_ICON_SIZE_DND, &w, &h);

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
			gtk_image_set_from_gicon (GTK_IMAGE (priv->icon), icon, GTK_ICON_SIZE_DND);
			icon_set = TRUE;
		}

		if (pix) {
			gtk_image_set_from_pixbuf (GTK_IMAGE (priv->icon), pix);
			g_object_unref (G_OBJECT (pix));
			icon_set = TRUE;
		}
	}

	if (icon_set)
		gtk_widget_show (priv->icon_box);
	else {
		gtk_image_clear (GTK_IMAGE (priv->icon));
		gtk_widget_hide (priv->icon_box);
	}

	if (gtk_widget_get_realized (GTK_WIDGET (window)))
		gtk_widget_queue_draw (GTK_WIDGET (window));
}

void
gooroom_notify_window_set_icon_pixbuf (GooroomNotifyWindow *window,
                                       GdkPixbuf           *pixbuf)
{
	GdkPixbuf *p_free = NULL;
	GooroomNotifyWindowPrivate *priv = window->priv;

	g_return_if_fail (GOOROOM_IS_NOTIFY_WINDOW (window)
                      && (!pixbuf || GDK_IS_PIXBUF (pixbuf)));

	if (pixbuf) {
		gint w, h, pw, ph;

		gtk_icon_size_lookup (GTK_ICON_SIZE_DND, &w, &h);
		pw = gdk_pixbuf_get_width (pixbuf);
		ph = gdk_pixbuf_get_height (pixbuf);

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

			pixbuf = p_free = gdk_pixbuf_scale_simple(pixbuf, nw, nh, GDK_INTERP_BILINEAR);
		}
	}

	gtk_image_set_from_pixbuf (GTK_IMAGE (priv->icon), pixbuf);

	if (pixbuf)
		gtk_widget_show (priv->icon_box);
	else
		gtk_widget_hide (priv->icon_box);

	if (gtk_widget_get_realized (GTK_WIDGET (window)))
		gtk_widget_queue_draw (GTK_WIDGET (window));

	if (p_free)
		g_object_unref (G_OBJECT (p_free));
}

void
gooroom_notify_window_set_expire_timeout (GooroomNotifyWindow *window,
                                          gint                 expire_timeout)
{
	g_return_if_fail (GOOROOM_IS_NOTIFY_WINDOW (window));

	GooroomNotifyWindowPrivate *priv = window->priv;

	if (expire_timeout >= 0)
		priv->expire_timeout = expire_timeout;
	else
		priv->expire_timeout = DEFAULT_EXPIRE_TIMEOUT;

	if (gtk_widget_get_realized (GTK_WIDGET (window))) {
		if (priv->expire_id) {
			g_source_remove (priv->expire_id);
			priv->expire_id = 0;
		}
		if (priv->fade_id) {
			g_source_remove (priv->fade_id);
			priv->fade_id = 0;
		}
		gtk_widget_set_opacity (GTK_WIDGET (window), priv->normal_opacity);

		gooroom_notify_window_start_expiration (window);
	}
}

void
gooroom_notify_window_set_actions (GooroomNotifyWindow *window,
                                   const gchar         **actions)
{
	gint i;
	GList *children, *l;
	GooroomNotifyWindowPrivate *priv = window->priv;

	g_return_if_fail (GOOROOM_IS_NOTIFY_WINDOW (window));

	children = gtk_container_get_children (GTK_CONTAINER (priv->button_box));
	for(l = children; l; l = l->next)
		gtk_widget_destroy (GTK_WIDGET (l->data));
	g_list_free(children);

	if(!actions) {
		gtk_widget_hide (priv->button_box);
		priv->has_actions = FALSE;
	} else {
		gtk_widget_show (priv->button_box);
		priv->has_actions = TRUE;
	}

	for (i = 0; actions && actions[i]; i += 2) {
		const gchar *cur_action_id = actions[i];
		const gchar *cur_button_text = actions[i+1];
		GtkWidget *btn, *lbl;
		gchar *cur_button_text_escaped;

		if (!cur_button_text || !cur_action_id || !*cur_action_id)
			break;
        /* Gnome applications seem to send a "default" action which often has no
           label or text, because it is intended to be executed when clicking
           the notification window.
           See https://developer.gnome.org/notification-spec/
           As we do not support this for the moment we hide buttons without labels. */
        if (g_strcmp0 (cur_button_text, "") == 0)
            continue;

        btn = gtk_button_new ();
        gtk_button_set_relief (GTK_BUTTON (btn), GTK_RELIEF_NONE);
        g_object_set_data_full (G_OBJECT (btn), "--action-id",
                                g_strdup (cur_action_id),
                                (GDestroyNotify)g_free);
		gtk_widget_show (btn);
		gtk_container_add (GTK_CONTAINER (priv->button_box), btn);
		g_signal_connect (G_OBJECT (btn), "clicked",
                          G_CALLBACK (gooroom_notify_window_button_clicked),
                          window);

		cur_button_text_escaped = g_markup_printf_escaped("<span size='small'>%s</span>",
                                                          cur_button_text);

		lbl = gtk_label_new (NULL);
		gtk_label_set_markup (GTK_LABEL (lbl), cur_button_text_escaped);
		gtk_label_set_use_markup (GTK_LABEL (lbl), TRUE);
		gtk_widget_show (lbl);
		gtk_container_add (GTK_CONTAINER (btn), lbl);

		g_free (cur_button_text_escaped);
	}

	if (gtk_widget_get_realized (GTK_WIDGET (window)))
		gtk_widget_queue_draw (GTK_WIDGET (window));
}

void
gooroom_notify_window_set_opacity (GooroomNotifyWindow *window, gdouble opacity)
{
	g_return_if_fail (GOOROOM_IS_NOTIFY_WINDOW (window));

	GooroomNotifyWindowPrivate *priv = window->priv;

	if (opacity > 1.0)
		opacity = 1.0;
	else if (opacity < 0.0)
		opacity = 0.0;

	priv->normal_opacity = opacity;
	priv->op_change_steps = FADE_TIME / FADE_CHANGE_TIMEOUT;
	priv->op_change_delta = opacity / priv->op_change_steps;

	if(gtk_widget_get_realized (GTK_WIDGET (window)) && priv->expire_id && !priv->fade_id)
		gtk_widget_set_opacity (GTK_WIDGET (window), priv->normal_opacity);
}

gdouble
gooroom_notify_window_get_opacity (GooroomNotifyWindow *window)
{
	g_return_val_if_fail (GOOROOM_IS_NOTIFY_WINDOW (window), 0.0);

	return window->priv->normal_opacity;
}

void
gooroom_notify_window_set_icon_only (GooroomNotifyWindow *window,
                                     gboolean icon_only)
{
	g_return_if_fail (GOOROOM_IS_NOTIFY_WINDOW (window));

	GooroomNotifyWindowPrivate *priv = window->priv;

	if (icon_only == priv->icon_only)
		return;

	priv->icon_only = !!icon_only;

	if (icon_only) {
		GtkRequisition req;

		if (!gtk_widget_get_visible(priv->icon_box)) {
			g_warning("Attempt to set icon-only mode with no icon");
			return;
		}

		gtk_widget_hide (priv->content_box);

		/* set a wider size on the icon box so it takes up more space */
		gtk_widget_realize (priv->icon);
		gtk_widget_get_preferred_size (priv->icon, NULL, &req);
		gtk_widget_set_size_request (priv->icon_box, req.width * 4, -1);
		/* and center it */
		g_object_set (priv->icon_box, "halign", GTK_ALIGN_CENTER, NULL);
	} else {
		g_object_set (priv->icon_box, "halign", GTK_ALIGN_START, NULL);
		gtk_widget_set_size_request (priv->icon_box, -1, -1);
		gtk_widget_show (priv->content_box);
	}
}

void
gooroom_notify_window_set_gauge_value (GooroomNotifyWindow *window, gint value)
{
	g_return_if_fail (GOOROOM_IS_NOTIFY_WINDOW (window));

	GooroomNotifyWindowPrivate *priv = window->priv;

	/* maybe want to do some kind of effect if the value is out of bounds */
	if (value > 100)
		value = 100;
	else if (value < 0)
		value = 0;

	gtk_widget_hide (priv->summary);
	gtk_widget_hide (priv->body);
	gtk_widget_hide (priv->button_box);

	if (!priv->gauge) {
		GtkWidget *box;
		gint width;

		if (gtk_widget_get_visible(priv->icon)) {
			/* size the pbar in relation to the icon */
			GtkRequisition req;

			gtk_widget_realize (priv->icon);
			gtk_widget_get_preferred_size (priv->icon, NULL, &req);
			width = req.width * 4;
		} else {
			/* FIXME: do something less arbitrary */
			width = 120;
		}

		box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
		gtk_widget_show (box);

		g_object_set (box, "valign", GTK_ALIGN_CENTER, NULL);
		gtk_box_pack_start (GTK_BOX (priv->content_box), box, TRUE, TRUE, 0);

		priv->gauge = gtk_progress_bar_new ();
		gtk_widget_set_size_request (priv->gauge, width, -1);
		gtk_widget_show (priv->gauge);
		gtk_container_add (GTK_CONTAINER (box), priv->gauge);
	}

	gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(priv->gauge), value / 100.0);
}

void
gooroom_notify_window_unset_gauge_value (GooroomNotifyWindow *window)
{
	g_return_if_fail (GOOROOM_IS_NOTIFY_WINDOW (window));

	GooroomNotifyWindowPrivate *priv = window->priv;

	if (priv->gauge) {
		GtkWidget *align = gtk_widget_get_parent (priv->gauge);

		g_assert (align);

		gtk_widget_destroy (align);
		priv->gauge = NULL;

		if(priv->has_summary_text)
			gtk_widget_show (priv->summary);
		if(priv->has_body_text)
			gtk_widget_show (priv->body);
		if(priv->has_actions)
			gtk_widget_show (priv->button_box);
	}
}

void
gooroom_notify_window_set_do_fadeout (GooroomNotifyWindow *window,
                                      gboolean do_fadeout,
                                      gboolean do_slideout)
{
	g_return_if_fail (GOOROOM_IS_NOTIFY_WINDOW (window));

	GooroomNotifyWindowPrivate *priv = window->priv;

	priv->do_fadeout = do_fadeout;
	priv->do_slideout = do_slideout;
}

void
gooroom_notify_window_set_notify_location (GooroomNotifyWindow *window,
                                           GtkCornerType notify_location)
{
	g_return_if_fail (GOOROOM_IS_NOTIFY_WINDOW (window));

	window->priv->notify_location = notify_location;
}

void
gooroom_notify_window_closed (GooroomNotifyWindow *window,
                              GooroomNotifyCloseReason reason)
{
	g_return_if_fail (GOOROOM_IS_NOTIFY_WINDOW (window)
                      && reason >= GOOROOM_NOTIFY_CLOSE_REASON_EXPIRED
                      && reason <= GOOROOM_NOTIFY_CLOSE_REASON_UNKNOWN);

	g_signal_emit (G_OBJECT(window), signals[SIG_CLOSED], 0, reason);
}
