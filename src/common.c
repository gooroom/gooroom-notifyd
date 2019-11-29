/*
 *  Copyright (c) 2016 Simon Steinbeiß <ochosi@xfce.org>
 *  Copyright (c) 2019 Gooroom <gooroom@gooroom.kr>
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <glib.h>
#include <gtk/gtk.h>
#include <gdk/gdkx.h>
#include <gio/gdesktopappinfo.h>

#include "common.h"

GdkPixbuf *
notify_pixbuf_from_image_data (GVariant *image_data)
{
    GdkPixbuf *pix = NULL;
    gint32 width, height, rowstride, bits_per_sample, channels;
    gboolean has_alpha;
    GVariant *pixel_data;
    gsize correct_len;
    guchar *data;

    if (!g_variant_is_of_type (image_data, G_VARIANT_TYPE ("(iiibiiay)")))
    {
        g_warning ("Image data is not the correct type");
        return NULL;
    }

    g_variant_get (image_data,
                   "(iiibii@ay)",
                   &width,
                   &height,
                   &rowstride,
                   &has_alpha,
                   &bits_per_sample,
                   &channels,
                   &pixel_data);

    correct_len = (height - 1) * rowstride + width
                  * ((channels * bits_per_sample + 7) / 8);
    if(correct_len != g_variant_get_size (pixel_data)) {
        g_message ("Pixel data length (%lu) did not match expected value (%u)",
                   g_variant_get_size (pixel_data), (guint)correct_len);
        return NULL;
    }

    data = (guchar *) g_memdup (g_variant_get_data (pixel_data),
                                g_variant_get_size (pixel_data));
    g_variant_unref(pixel_data);

    pix = gdk_pixbuf_new_from_data(data,
                                   GDK_COLORSPACE_RGB, has_alpha,
                                   bits_per_sample, width, height,
                                   rowstride,
                                   (GdkPixbufDestroyNotify)g_free, NULL);
    return pix;
}

gchar *
notify_icon_name_from_desktop_id (const gchar *desktop_id)
{
    gchar *icon = NULL;
    GDesktopAppInfo *dt_info = NULL;

    dt_info = g_desktop_app_info_new (desktop_id);
    if (dt_info) {
        icon = g_desktop_app_info_get_string (dt_info, G_KEY_FILE_DESKTOP_KEY_ICON); 
        g_object_unref (dt_info);
    }

    return icon;
}
