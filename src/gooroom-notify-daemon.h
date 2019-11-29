/*
 *  gooroom-notifyd
 *
 *  Copyright (c) 2008 Brian Tarricone <bjt23@cornell.edu>
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

#ifndef __GOOROOM_NOTIFY_DAEMON_H__
#define __GOOROOM_NOTIFY_DAEMON_H__

#include <glib-object.h>

#define GOOROOM_TYPE_NOTIFY_DAEMON     (gooroom_notify_daemon_get_type())
#define GOOROOM_NOTIFY_DAEMON(obj)     (G_TYPE_CHECK_INSTANCE_CAST((obj), GOOROOM_TYPE_NOTIFY_DAEMON, GooroomNotifyDaemon))
#define GOOROOM_IS_NOTIFY_DAEMON(obj)  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GOOROOM_TYPE_NOTIFY_DAEMON))

G_BEGIN_DECLS

typedef struct _GooroomNotifyDaemon  GooroomNotifyDaemon;

GType gooroom_notify_daemon_get_type (void) G_GNUC_CONST;

GooroomNotifyDaemon *gooroom_notify_daemon_new_unique (GError **error);

G_END_DECLS

#endif  /* __GOOROOM_NOTIFY_DAEMON_H__ */
