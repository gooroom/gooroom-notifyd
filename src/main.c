/*
 *  gooroom-notifyd
 *
 *  Copyright (c) 2008 Brian Tarricone <bjt23@cornell.edu>
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

#include <locale.h>
#include <libintl.h>

#include <glib.h>
#include <gtk/gtk.h>

#include "gooroom-notify-daemon.h"

int
main(int argc,
     char **argv)
{
    GooroomNotifyDaemon *xndaemon;
    GError *error = NULL;

	setlocale (LC_ALL, "");
	bindtextdomain (GETTEXT_PACKAGE, GNOMELOCALEDIR);
	textdomain (GETTEXT_PACKAGE);

    gtk_init(&argc, &argv);

    xndaemon = gooroom_notify_daemon_new_unique(&error);
    if(!xndaemon) {
        fprintf (stderr, "Unable to start notification daemon: %s\n", error->message);
        g_error_free(error);
        return 1;
    }

    gtk_main();

    g_object_unref(G_OBJECT(xndaemon));

    return 0;
}
