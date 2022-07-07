// SPDX-License-Identifier: GPL-2.0-or-later

/*
 * Most of the code in here, deals with MM watching.
 * When MM connects and disconnects from the bus, we act accordingly, mainly
 * getting rid of AvModem objects we still track.
*/

/* GLib2 headers */
#include <glib.h>

/* AV headers */
#include <av.h>
#include <av_mm.h>
#include <av_mm_manager.h>
#include <av_mm_modem.h>
#include <av_mm_voice.h>
#include <av_utils.h>

/*
 * Used to free all AvModem objects we still track.
 *
 * Parameters:
 * - an allocated AV state structure
*/
static gint av_mm_unref_modems(void) {
	g_list_free_full(ll->av_modems, g_object_unref);
	ll->av_modems = NULL;

	return 0;
}

/*
 * So MM appeared! And you're looking at a GBusNameAppearedCallback function.
 * As such, we hope it follows GLib2 semantics. Basically, this function calls
 * av_mm_manager_init().
 *
 * Parameters:
 * - a working D-Bus connection
 * - name being watched
 * - unique name of the owner of the name being watched
 * - (user_data is unused)
*/
static void av_mm_on(GDBusConnection *connection,
	const gchar *name,
	const gchar *name_owner,
	gpointer user_data) {

	g_print("MM is connected!\n");
	av_mm_manager_init();
	return;
}

/*
 * Invoked when we are exiting, or MM is gone. Unreferences objects we still
 * track via av_mm_unref_modems and the manager object via av_mm_manager_deinit.
*/
static void av_mm_mm_is_gone_common(void) {
	if (ll->av_modems)
		av_mm_unref_modems();

	av_mm_manager_deinit();
	return;

}

/*
 * So MM disappeared. And you're looking at a GBusNameVanishedCallback function this time.
 * Basically, this function unreferences all AvModem objects we are still
 * tracking, and invokes av_mm_manager_deinit() via av_mm_mm_is_gone_common..
 *
 * Parameters:
 * - a D-Bus connection
 * - name being watched
 * - (user_data is unused)
*/
static void av_mm_off(GDBusConnection *connection,
	const gchar *name,
	gpointer user_data) {

	g_print("MM disconnected from bus :(\n");
	av_mm_mm_is_gone_common();
	return;
}

/*
 * Deinitializes MM interaction. That is, we stop watching for it.
*/
gint av_mm_deinit(void) {
	av_mm_mm_is_gone_common();
	if (ll->mm_watch) {
		g_bus_unwatch_name(ll->mm_watch);
		ll->mm_watch = 0;
	}

	if (ll->dbus_connection) {
		g_object_unref(ll->dbus_connection);
		ll->dbus_connection = NULL;
	}

	g_print("No longer watching for MM...\n");

	return 0;
}

/*
 * Initializes MM interaction. In particular, we prepare to watch for MM
 * appearing and disappearing from the bus, acting accordingly.
*/
gint av_mm_init(void) {
	GError *dbus_connection_error = NULL;

	/* Sanity check: AV modems list should be empty at this point. */
	if (ll->av_modems) {
		g_printerr("BUG: AV modems list is not empty!\n");
		return 1;
	}

	/* connects to D-Bus, system bus */
	ll->dbus_connection = g_bus_get_sync(G_BUS_TYPE_SYSTEM,
		NULL,                    /* GCancellable is not used (at least yet) */
		&dbus_connection_error); /* GError to report errors */
	if (!ll->dbus_connection) {
		av_utils_print_gerror(&dbus_connection_error);
		return 1;              /* main loop is never started and program exits */
	}

	ll->mm_watch = g_bus_watch_name_on_connection(ll->dbus_connection,
		"org.freedesktop.ModemManager1", /* watch for ModemManager well-known service name */
		G_BUS_NAME_WATCHER_FLAGS_NONE,   /* no flags */
		av_mm_on,                        /* what to do when MM connects */
		av_mm_off,                      /* what to do when MM disconnects */
		NULL,                           /* no callbacks user_data */
		NULL);                          /* no GDestroyNotify ?? */
	if (!ll->mm_watch) {
		g_printerr("Failure while starting to watch for MM in the system bus\n");
		av_mm_deinit();
		return 1;              /* main loop is never started and program exits */
	}

	g_print("Watching for MM...\n");

	return 0;
}
