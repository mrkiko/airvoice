// SPDX-License-Identifier: GPL-2.0-or-later

/* AV headers */
#include <av.h>
#include <av_utils.h>
#include <av_gobjects.h>
#include <av_threadcomm.h>
#include <av_sip.h>

static void av_mm_call_unregister_mmcall(AvModem *m, MMCall *c);

static void av_mm_call_state_eval(MMCall *c,
	MMCallState oldstate,
	MMCallState newstate,
	AvModem *m) {
	GList *mmcalls;

	gint n_calls = avmodem_get_active_calls_counter(m);

	if (
		(newstate == MM_CALL_STATE_RINGING_IN) ||
		(newstate == MM_CALL_STATE_RINGING_OUT) ||
		(newstate == MM_CALL_STATE_WAITING)
	) {
		if (!n_calls) {
			n_calls++;
			g_print("Activating audio IO...\n");
		}
	}
	if (newstate == MM_CALL_STATE_TERMINATED) {
		n_calls--;
		if (!n_calls) {
			g_print("Deactivating audio IO...\n");
		}

		mmcalls = avmodem_get_mmmodemvoice_calls_list(m);
		mmcalls = g_list_remove(mmcalls, c);
		av_mm_call_unregister_mmcall(m, c);
		avmodem_update_mmmodemvoice_calls_list(m, mmcalls);
	}

	avmodem_set_active_calls_counter(m, n_calls);

}

static void av_mm_call_statechange(MMCall *c,
	MMCallState oldstate,
	MMCallState newstate,
	MMCallStateReason reason,
	AvModem *m) {
	av_mm_call_state_eval(c, oldstate, newstate, m);
}

static guint av_mm_call_gsignals(AvModem *m, MMCall *c, gboolean connect) {
	guint n_handlers;
	gulong call_statechange_signal;

	n_handlers = g_signal_handlers_disconnect_by_func(c,
		av_mm_call_statechange,
		m);

	if (connect) {
		call_statechange_signal = g_signal_connect(c,
			"state-changed",
			G_CALLBACK(av_mm_call_statechange),
			m);

		if (!call_statechange_signal)
			g_printerr("Failure connecting call state change signal for %s\n",mm_call_get_path(c));
	}

	return n_handlers;
}

void av_mm_call_register(AvModem *m, MMCall *call) {
	GList *mmcalls;

	mmcalls = avmodem_get_mmmodemvoice_calls_list(m);

	g_print("Registering %s\n",mm_call_get_path(call));

	mmcalls = g_list_append(mmcalls, call);
	avmodem_update_mmmodemvoice_calls_list(m, mmcalls);

	av_mm_call_gsignals(m, call, TRUE);

	av_mm_call_state_eval(call, MM_CALL_STATE_UNKNOWN, mm_call_get_state(call), m);

	return;
}

static void av_mm_call_unregister_mmcall(AvModem *m, MMCall *c) {
	g_print("Unregistering %s\n",mm_call_get_path(c));

	g_print("Disconnected %" G_GUINT16_FORMAT" signal handlers from MMCall object %s\n",av_mm_call_gsignals(m, c, FALSE),mm_call_get_path(c));
	g_object_unref(c);

	return;
}

void av_mm_call_unregister(AvModem *m, const gchar *call_path) {
	MMCall *current_call;
	GList *mmcalls;

	mmcalls = avmodem_get_mmmodemvoice_calls_list(m);

	current_call = av_utils_mm_call_search(mmcalls, call_path);

	if (current_call) {
		mmcalls = g_list_remove(mmcalls, current_call);
		av_mm_call_unregister_mmcall(m, current_call);
		avmodem_update_mmmodemvoice_calls_list(m, mmcalls);
	}

	return;
}

void av_mm_call_release_mmcalls(AvModem *m) {
	MMCall *voicecall;
	GList *mmcalls;

	mmcalls = avmodem_get_mmmodemvoice_calls_list(m);

	while(g_list_length(mmcalls)) {
		voicecall = MM_CALL(mmcalls->data);
		mmcalls = g_list_remove(mmcalls, mmcalls->data);
		av_mm_call_unregister_mmcall(m, voicecall);
	}

	g_list_free(mmcalls);

	avmodem_update_mmmodemvoice_calls_list(m, NULL);

	return;
}

static void av_mm_call_sipcall_start_call(MMCall *c, GAsyncResult *res, gpointer user_data) {
	GError *e = NULL;
	struct av_thread_cmd *call_started_cmd;
	AvModem *m = user_data;

	if (!mm_call_start_finish(c, res, &e))
		av_utils_print_gerror(&e);
	else {
		call_started_cmd = av_thread_cmd(SIP_CMD_CALL_IN_PROGRESS, mm_call_dup_path(c));
		if (call_started_cmd) {
			av_thread_txcmd(avmodem_get_sipthread(m), call_started_cmd, 0);
			g_clear_pointer(&call_started_cmd, g_free);
		}
	}

	av_utils_async_end(G_OBJECT(c));

	return;
}

static void av_mm_call_sipcall_with_call(MMModemVoice *v, GAsyncResult *res, gpointer user_data) {
	AvModem *m = user_data;
	MMCall *c;
	GError *e = NULL;;

	c = mm_modem_voice_create_call_finish(v, res, &e);
	if (!c) {
		g_printerr("Unable to create MM call...\n");
		av_utils_print_gerror(&e);
		av_utils_async_end(NULL);
		return;
	}

	mm_call_start(c, NULL, (GAsyncReadyCallback)av_mm_call_sipcall_start_call, m);

	return;
}

/*
 * DO WE NEED TO SANITIZE dest_number HERE?? HOW?
*/
void av_mm_call_sipcall(AvModem *m, const char *dest_number) {
	MMCallProperties *cprops;
	gchar *normalized_number;

	normalized_number = g_str_to_ascii(dest_number, "C");
	if (!normalized_number || !strlen(normalized_number))
		return;

	cprops = mm_call_properties_new();

	mm_call_properties_set_number(cprops, normalized_number);

	av_utils_async_start(NULL);
	mm_modem_voice_create_call(avmodem_get_mmmodemvoice(m), cprops, NULL, (GAsyncReadyCallback)av_mm_call_sipcall_with_call, m);

	g_clear_pointer(&cprops, g_object_unref);
	g_clear_pointer(&normalized_number, g_free);

	return;
}
