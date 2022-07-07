// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef __av_h__
#define __av_h__

/* ModemManager's libmm-glib headers */
#include <libmm-glib.h>

/* AV lifecycle data. */
struct av_ll {
	/* async operations counter for a "clean exit" */
	gint async_counter;

	/* GLib main event loop */
	GMainLoop *loop;

	/* GSources */
	guint unix_signals_src_tag;
	guint exit_timeout_src_tag;

	/* "Of modems and men": MM related stuff */

	/* D-Bus connection */
	GDBusConnection *dbus_connection;

	/* MM watch ID */
	guint mm_watch;

	/* manager object */
	MMManager *manager;

	/* "modem added" and "modem removed" GSignals IDs */
	gulong modem_added;
	gulong modem_removed;

	/* list of managed modems */
	GList *av_modems;
};

extern struct av_ll *ll;

#endif
