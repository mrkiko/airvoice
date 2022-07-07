// SPDX-License-Identifier: GPL-2.0-or-later

/*
 * Tis code deals with the ModemManager objects manager, We connects to
 * GSignals notifying us when a modem object comes, or goes, registering or
 * unregistering objects accordingly.
*/

/* AV headers */
#include <av.h>
#include <av_utils.h>
#include <av_storage.h>
#include <av_mm_modem.h>

/*
 * Given a MMObject, this function:
 * - gets an AvModem object via av_storage_add_mmobject()
 * - registers it via av_mm_modem_register
 *
 * Parameters:
 * - a valid MMObject
 *
 * Returns: 0 on success, 1 when the passed MMObject is already tracked or its
 * registration fails.
 *
 * The first case is to be considered critical.
 *
 * Note: this function is invoked when a new modem object is added (e.g.:
 * modem detected by MM), or at program startup when modems are listed and added.
 * In general, a failure in this function results only in a message being
 * printed to the user.
*/
static gint av_mm_manager_addmodem(MMObject *object) {
	AvModem *m;

	m = av_storage_find_mmobject(object);
	if (m) {
		g_printerr("BUG - a (probably) stale object has been found\n");
		return 1;
	}

	m = av_storage_add_mmobject(object);
	if (av_mm_modem_register(m)) {
		g_printerr("Unable to get MMModem object\n");
		return 1;
	}

	return 0;
}

/*
 * This is the GSignal c_handler invoked when a modem object (MMObject) is
 * added.
 *
 * Parameters:
 * - the object manager from ModemManager
 * - the newly added MMObject
 *
 * Note: MMObject is passed to us with no extra references.
*/
static void av_mm_manager_modem_added(MMManager *manager, MMObject *object, gpointer user_data) {

	/* if MMObject is successfully added / registered, print a message */
	if (!av_mm_manager_addmodem(object))
		g_print("%s added\n",mm_object_get_path(object));

	return;
}

/*
 * This is the GSignal c_handler invoked when a modem object (MMObject) is
 * removed. We call av_storage_remove_avmodem() that drops a reference of the AVModem object for this MMObject.
 *
 * Parameters:
 * - the object manager from ModemManager
 * - the MMObject being removed
 *
 * Note: MMObject is passed to us with no extra references.
*/
static void av_mm_manager_modem_removed(MMManager *manager, MMObject *object, gpointer user_data) {
	AvModem *m;

	m = av_storage_find_mmobject(object);
	if (!m) {
		g_printerr("BUG - can not find object for %s\n",mm_object_get_path(object));
		return;
	}

	g_print("%s is gone\n",mm_object_get_path(object));

	if (av_storage_remove_avmodem(object)) {
		g_printerr("BUG - storage can not remove %s\n",mm_object_get_path(object));
		return;
	}

	return;
}

/*
 * Disconnects, and optionally re-connects, GSignals handlers. GSignals
 * handlers IDs stored in AV state structure are updated accordingly.
 *
 * Parameters:
 * - gboolean value; should be set to TRUE to cause (re)connection of GSignals handlers
 *
 * Returns: the number of disconnected handlers.
*/
static guint av_mm_manager_gsignals(gboolean connect_signals) {
	guint n_handlers;

	/* disconnects handlers */
	n_handlers = g_signal_handlers_disconnect_by_func(
		ll->manager,               /* object manager */
		av_mm_manager_modem_added, /* callback */
		NULL);                       /* user data */
	n_handlers = n_handlers+g_signal_handlers_disconnect_by_func(
		ll->manager,                 /* manager object */
		av_mm_manager_modem_removed, /* callback being disconnected */
		NULL);                         /* user_data */
	ll->modem_added = 0;
	ll->modem_removed = 0;

	/* (re)connect */
	if (connect_signals) {
		ll->modem_added = g_signal_connect(ll->manager, /* instance */
			"object-added",                               /* detailed_signal */
			G_CALLBACK(av_mm_manager_modem_added),        /* callback */
			NULL);                                        /* NULL user_data passed to callbacks */

		ll->modem_removed = g_signal_connect(ll->manager,
			"object-removed",
			G_CALLBACK(av_mm_manager_modem_removed),
			NULL);
	}

	return n_handlers;
}

/*
 * Lists al present modems to get and register MMObjects for them.
*/
static void av_mm_manager_get_modems(void) {
	GList *modems;
	GList *l;

	modems = g_dbus_object_manager_get_objects(G_DBUS_OBJECT_MANAGER(ll->manager));

	if (!modems) {
		g_printerr("No modems\n");
		return;
	}

	for (l = modems; l; l = g_list_next(l)) {
		if (!av_mm_manager_addmodem(MM_OBJECT(l->data)))
			g_print("%s added\n",mm_object_get_path(MM_OBJECT(l->data)));
	}

	g_list_free_full(modems, g_object_unref); /* we should have references for these objects by now */

	return;
}

/* Forward declaration, as suggested by GLib docs... */
static void av_mm_manager_init_finish(GDBusConnection *c, GAsyncResult *res, gpointer user_data);

/*
 * Gets a manager object.
*/
void av_mm_manager_init(void) {
	g_print("Manager init: ");

	/* we start an async operation but we won't take any extra reference to an
	 * object here. We don't have one. */
	av_utils_async_start(NULL);
	mm_manager_new(ll->dbus_connection,
		G_DBUS_OBJECT_MANAGER_CLIENT_FLAGS_DO_NOT_AUTO_START,
		NULL,
		(GAsyncReadyCallback)av_mm_manager_init_finish,
		NULL);

	return;
}

/*
 * Invoked when mm_manager_new() result is ready, be it success or not.
 * This is a GAsyncReadyCallback. The object we get here has a reference for us,
 * so we should g_object_unref() it at some point. An error condition here
 * causes the program to uselessly run themain loop, not interacting with the
 * MM.
 * No further attempts will be made to fix the situation, until MM restarts I
 * guess.
*/
static void av_mm_manager_init_finish(GDBusConnection *c, GAsyncResult *res, gpointer user_data) {
	GError *e = NULL;
	ll->manager = mm_manager_new_finish(res, &e);
	av_utils_async_end(NULL);

	if (!ll->manager) {
		av_utils_print_gerror(&e);
		return;
	}

	g_print("OK\n");

	/* connect GSignals */
	av_mm_manager_gsignals(TRUE);

	/* list all present modems and register them */
	av_mm_manager_get_modems();

	return;
}

/*
 * Deinits manager; e.g.: g_object_unref() the manager object, after
 * (uselessly) disconnecting GSignal handlers. The use of g_clear_object here allows to set the pointer to NULL.
*/
void av_mm_manager_deinit(void) {

	g_print("Manager deinit: ");
	if (ll->manager) {
		g_print("%" G_GUINT16_FORMAT" signal handlers disconnected\n",av_mm_manager_gsignals(FALSE));
		g_clear_object(&ll->manager);
	}
	else
		g_print("not initialized\n");

	return;
}
