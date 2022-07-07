// SPDX-License-Identifier: GPL-2.0-or-later

/*
 * The code in here mainly monitors for modem state changes (e.g.: disabled
 * -> enabled, enabled -> registered... ), and starts (or stops) code
 * interacting with specific modem services.
*/

/* AV headers */
#include <av.h>
#include <av_gobjects.h>
#include <av_storage.h>
#include <av_mm_voice.h>

/*
 * GSignal c_handler invoked when a modem changes it's state.
 * An AvModem object is passed to user_data, and subsequently to code
 * interacting with specific services.
*/
static void av_mm_modem_statechange(MMModem *modem,
	MMModemState oldstate,
	MMModemState newstate,
	MMModemStateChangeReason reason,
	AvModem *m)
{
	if (newstate < MM_MODEM_STATE_REGISTERED)
		av_mm_voice_deinit(m);
	if ((newstate == MM_MODEM_STATE_REGISTERED) && (oldstate < newstate))
		av_mm_voice_init(m);

	return;
}

/*
 * Disconnects, and optionally re-connects, GSignals handlers. GSignals
 * handlers IDs stored in the passed AvModem object are updated accordingly.
 *
 * Parameters:
 * - a valid AvModem object
 * - gboolean value; should be set to TRUE to cause (re)connection of GSignals handlers
 *
 * Returns: the number of disconnected handlers.
 *
 * If an error occurs, a message is printed. However, nothing else happen. No
 * GSignals will be received for this object (instance). Even in the face of
 * GSignal connection failure, this function returns 0, still printing an
 * error message.
*/
static guint av_mm_modem_gsignals(AvModem *m, gboolean connect_signals) {
	guint n_handlers;
	MMModem *modem;
	gulong statechange_gsignal;

	/* If we reach here, then a MMModem for this MMObject should exist; otherwise
	 * av_mm_modem_register would not have called us. */
	modem = avmodem_get_mmmodem(m);

	n_handlers = g_signal_handlers_disconnect_by_func(modem,
		av_mm_modem_statechange,
		m);
	avmodem_set_mmmodem_signal_statechange(m, 0);

	if (connect_signals) {
		statechange_gsignal = g_signal_connect(modem, /* instance */
			"state-changed",                            /* detailed_signal */
			G_CALLBACK(av_mm_modem_statechange),        /* c_handler */
			m);                                         /* AvModem object passed to callbacks */
		if (!statechange_gsignal)
			g_printerr("Unable to connect state-changed MMModem signals to %s\n",mm_modem_get_path(modem));
		else
			avmodem_set_mmmodem_signal_statechange(m, statechange_gsignal);

	}

	return n_handlers;
}

/*
 * Registers a modem. Given an AvModem object, this function gets the
 * corresponding MMModem one if it exists, and connects GSignals to it.
 * If the modem device is registered to the network, services are initialized.
 *
 * Parameters:
 * - a valid AvModem object
 *
 * Returns: 0 on success, 1 on failure (no MMModem object).
 * Failure to connect GSignals is not checked here: we consider it a very
 * unlikely event. Hope we are right in doing so.
*/
gint av_mm_modem_register(AvModem *m) {
	MMModem *modem;

	/* get a MMModem object for this AvModem object (not guaranteed to exist) */
	modem = avmodem_get_mmmodem(m);
	if (!modem) {
		g_printerr("Unable to obtain MMModem object for %s\n",mm_object_get_path(avmodem_get_mmobject(m)));
		return 1;
	}

	g_print("Attaching to modem %s\n",mm_modem_get_path(modem));

	av_mm_modem_gsignals(m, TRUE);

	if (mm_modem_get_state(modem) == MM_MODEM_STATE_REGISTERED)
		av_mm_voice_init(m);

	return 0;
}

/*
 * Disconnects GSignals handlers from a given AvModem's MMModem object, if existing.
 *
 * Parameter:
 * - a valid AvModem object
 *
 * Returns: 0 anyway.
*/
gint av_mm_modem_unregister(AvModem *m) {
	MMModem *modem;
	const gchar *dbus_path;

	modem = avmodem_get_mmmodem(m);
	if (modem) {
		dbus_path = mm_modem_get_path(modem);
		g_print("Disconnected %" G_GUINT16_FORMAT" signal handlers from MMModem object %s\n",av_mm_modem_gsignals(m, FALSE),dbus_path);
	}

	return 0;
}
