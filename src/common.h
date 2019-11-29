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

#ifndef __COMMON_H__
#define __COMMON_H__

#include <glib.h>

GdkPixbuf  *notify_pixbuf_from_image_data (GVariant *image_data);

gchar      *notify_icon_name_from_desktop_id (const gchar *desktop_id);

#endif /* __COMMON_H__ */
