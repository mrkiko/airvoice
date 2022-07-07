// SPDX-License-Identifier: GPL-2.0-or-later

/*
 * The code in this file deals with two main aspects:
 * - it connects (and disconnects) GSignals from MMModemVoice objects, to be
 *   notified about MMCall objects additions and removals.
 * - invoked the code responsible for audio calls tracking and management.
 *
 * The code is a little bit more convoluted due to the fact that signal
 * handlers for voice calls objects additions/removals, give us only the object
 * path. Subsequently, each time a MMCall object is added, we search for the
 * corresponding object in ModemManager's calls list, via libmm-glib of course.
 * When a MMCall object is removed instead, we check for the corresponding
 * MMCall object in a GList of MMCall objects we maintain, to unref it.
*/

/* GLib2 headers */
#include <glib.h>

/* AV headers */
#include <av.h>
#include <av_gobjects.h>
#include <av_utils.h>
#include <av_mm_voice.h>
#include <av_storage.h>
#include <av_mm_call.h>
#include <av_thread.h>
#include <av_threadcomm.h>
#include <av_sip.h>
#include <av_config.h>

/*
 * This data structure has been created to solve the problem of going from a
 * MMCall object path, to the object itself. A problem that maybe solvable with
 * a GTask.
*/
struct MMCall_ctx {
	/* This is the AvModem object we are working with... */
	AvModem *avm;

	/* Object path of the MMCall object we are searching. */
	gchar *object_path;

	/* Was the call added (TRUE) or removed (FALSE). */
	gboolean added;
};

/*
 * Allocates a MMCall_ctx structure, given the data needed to do so as input.
 *
 * Parameters:
 * - AvModem object for which we are searching calls (in case of additions) or
 *   our a MMCall object to unregister (in case of removals).
 * - object path of the MMCall object
 * - a gboolean: is the call being added? If not, it's being removed.
*/
static struct MMCall_ctx *MMCall_ctx_alloc(AvModem *m, const gchar *object_path, gboolean added) {
	struct MMCall_ctx *callctx;

	callctx = g_try_malloc0(sizeof *callctx);
	if (!callctx)
		return callctx;

	callctx->avm = m;
	callctx->object_path = g_strdup(object_path);
	callctx->added = added;

	return callctx;
}

/*
 * Frees an allocated MMCall_ctx structure.
 *
 * Parameters:
 * - a MMCall_ctx pointer to an allocated MMCall_ctx structure.
 *
 * This bils down to:
 * - g_freeing a string allocated with g_strdup
 * - g_freeing the structure itself.
*/
static void MMCall_ctx_free(struct MMCall_ctx *ctx) {
	g_free(ctx->object_path);
	g_free(ctx);
	return;
}

/* Forward declaration, as suggested by GLib docs... */
static void av_mm_voice_find_call_list_ready(MMModemVoice *v, GAsyncResult *res, struct MMCall_ctx *callcontext);

/*
* As suggested by its name, this function has the purpose of "finding calls",
 * that is is - finding MMCall objects. both in the ModemManager calls list for
 * a modem (MMCall objects are being added) or on our own MMCalls GList
 * (removals). As you can guess, this is only the first chunk of this function.
 * the second part, follows when async operation is completed.
 *
 * Parameters:
 * - the AvModem object to which the event is related
 * - the MMModemVoice object who generated the event itself
 * - the object path of the MMCall object to which the event relates
*/
static void av_mm_voice_find_call(AvModem *m, MMModemVoice *v, const gchar *object_path, gboolean added) {
	struct MMCall_ctx *callcontext;

	/* <Boris> ... come Durok! </Boris> */
	callcontext = MMCall_ctx_alloc(
		m,
		object_path,
		added);

	if (!callcontext) {
		g_print("Failure when allocating temporary call context structure\n");
		/* This call will not be processed. Not nice. */
		return;
	}

	/* Starts async operation, taking an extra reference to avoid the AvModem object
	   going away, should e.g.: the modem do so. */
	av_utils_async_start(G_OBJECT(m));
	mm_modem_voice_list_calls(v,
		NULL,
		(GAsyncReadyCallback)av_mm_voice_find_call_list_ready,
		callcontext);

	return;
}

/*
 * This function continues what av_mm_voice_find_call() started. the logic here
 * maybe a little bit convoluted. A consideration that may help is:
 * - when a call is added, it is expected to be present in ModemManager's calls
 *   list for a specified modem.
 * - when a call goes away, it is not expected to be there
 * - when last call is removed, no GList will be provided.
 *
 * Or, since NULL is a valid GList, an empty list is provided to caller.
*/
static void av_mm_voice_find_call_list_ready(MMModemVoice *v, GAsyncResult *res, struct MMCall_ctx *callcontext) {
	GList *calls_list;
	GError *e = NULL;
	MMCall *current_call = NULL;

	calls_list = mm_modem_voice_list_calls_finish(v, res, &e);

	/* if given GList is empty, no error will be printed */
	if (!calls_list)
		av_utils_print_gerror(&e);

	/*
	 * When a call is added, then we expect to find it in the calls list.
	 * However, when a call is being removed, this will not happen. So, we avoid
	 * searching at all, and proceed with unregistration without an object.
	*/
	if (callcontext->added) {
		/* if a call was added but we have no list, terminate the program */
		g_assert(calls_list);
		current_call = av_utils_mm_call_search(calls_list, callcontext->object_path);

		if (current_call)
			av_mm_call_register(callcontext->avm, g_object_ref(current_call));

	}
	else {
		/* MMCall object being removed */
		av_mm_call_unregister(callcontext->avm, callcontext->object_path);
	}

	av_utils_async_end(G_OBJECT(callcontext->avm));
	MMCall_ctx_free(callcontext);

	/*
	 * If the call was added, we add an extra reference to it in the process of passing it to av_mm_call_register.
	 * Thus, our object will survive.
	*/
	g_list_free_full(calls_list, g_object_unref);

	return;
}

/* GSignal handlers */

static void av_mm_voice_call_added(MMModemVoice *voice, const gchar *object_path, AvModem *m) {
	g_print("Modem %s got call %s\n",mm_modem_voice_get_path(voice),object_path);

	av_mm_voice_find_call(m, voice, object_path, TRUE);

	return;
}

static void av_mm_voice_call_deleted(MMModemVoice *voice, const gchar *object_path, AvModem *m) {
	g_print("Call %s was removed from %s\n",object_path,mm_modem_voice_get_path(voice));

	av_mm_voice_find_call(m, voice, object_path, FALSE);

	return;
}

/*
 * Connects/disconnects GSignals from the MMModemVoice object corresponding to
 * given AvModem one.
 *
 * Parameters:
 * - a valid AvModem object to work with
 * - a boolean to indicate whether GSignals should be (re)connected
*/
static guint av_mm_voice_gsignals(AvModem *m, gboolean connect) {
	guint n_handlers;
	MMModemVoice *v;
	gulong call_added_signal;
	gulong call_removed_signal;

	v = avmodem_get_mmmodemvoice(m);

	n_handlers = g_signal_handlers_disconnect_by_func(v,
		av_mm_voice_call_added,
		m);

	n_handlers = n_handlers + g_signal_handlers_disconnect_by_func(v,
		av_mm_voice_call_deleted,
		m);

	avmodem_set_mmmodemvoice_signal_call_added(m, 0);
	avmodem_set_mmmodemvoice_signal_call_deleted(m, 0);

	if (connect) {
		call_added_signal = g_signal_connect(v,
			"call-added",
			G_CALLBACK(av_mm_voice_call_added),
			m);
		call_removed_signal = g_signal_connect(v,
			"call-deleted",
			G_CALLBACK(av_mm_voice_call_deleted),
			m);
		avmodem_set_mmmodemvoice_signal_call_added(m, call_added_signal);
		avmodem_set_mmmodemvoice_signal_call_deleted(m, call_removed_signal);
	}

	return n_handlers;
}

static void av_mm_voice_send_sip_config(AvModem *m) {
	struct av_modem_config *mc;
	struct av_thread_cmd *config_data;
	struct av_thread *sipthread;

	mc = av_config_parse(m);
	if (!mc)
		return;

	sipthread = avmodem_get_sipthread(m);

	config_data = av_thread_cmd(SIP_CMD_REGISTER, mc);
	if (config_data) {
		av_thread_txcmd(sipthread, config_data, 0);
		g_clear_pointer(&config_data, g_free);
	}
	else {
		g_printerr("Failure while allocating config data\n");
		av_config_free(&mc);
	}

}

static gboolean av_mm_voice_process_sip_event(AvModem *m) {
	struct av_thread_cmd *cmd;
	struct av_thread *sipthread;

	sipthread = avmodem_get_sipthread(m);
	if ( (cmd = av_thread_rxcmd(sipthread, 0)) ) {
		switch(cmd->msgtype) {
			case SIP_EVENT_READY:
				g_print("Sending SIP config...\n");
				av_mm_voice_send_sip_config(m);
				break;
			case SIP_EVENT_INCOMING_CALL:
				av_mm_call_sipcall(m, cmd->payload);
				g_clear_pointer(&cmd->payload, g_free);
				break;
			default:
				g_print("Unknown event %d received!\n",cmd->msgtype);
		}
	}

	g_clear_pointer(&cmd, g_free);

	return TRUE;
}

static gboolean av_mm_voice_process_sip_event_msg(GIOChannel *channel, GIOCondition condition, gpointer data) {
	AvModem *m = AV_MODEM(data);

	switch(condition) {
		case G_IO_IN:
			return av_mm_voice_process_sip_event(m);
		case G_IO_PRI:
		case G_IO_ERR:
		case G_IO_NVAL:
		case G_IO_HUP:
		default:
			g_printerr("Unexpected or unknown condition in SIP event channel. We'll stop processing events.\n");
			return FALSE;
	}

	return TRUE;
}

static void av_mm_voice_stop_sip_eventchannel(AvModem *m) {
	guint id;
	GIOChannel *c;
	GSource *src;

	id = avmodem_get_sip_GIOchannel_gsource_id(m);
	if (id) {
		src = g_main_context_find_source_by_id(NULL, id);
		if (src)
			g_source_destroy(src);
		else
			g_printerr("Failure while destroying SIP events channel processing source\n");

		avmodem_set_sip_GIOchannel_gsource_id(m, 0);
	}

	c = avmodem_get_sip_GIOchannel(m);
	if (c) {
		g_io_channel_unref(c);
		avmodem_set_sip_GIOchannel(m, NULL);
	}
}

static void av_mm_voice_start_sip_eventchannel(AvModem *m) {
	GIOChannel *c;
	struct av_thread *t;
	GError *e = NULL;
	guint id;

	/*
	 * This function won't fail, and we already know our thread and related FDs are ok if we reach here.
	*/
	t = avmodem_get_sipthread(m);
	c = g_io_channel_unix_new(t->sockets[0]);
	avmodem_set_sip_GIOchannel(m, c);

	/*
	 * The FD will get closed in av_thread_teardown, so make sure it's not closed on channel unref.
	 *
	 * Note: close_on_unref is FALSE by default!
	*/
	g_io_channel_set_close_on_unref(c, FALSE);
	if (g_io_channel_set_encoding(c, NULL, &e) != G_IO_STATUS_NORMAL) {
		av_utils_print_gerror(&e);
		av_mm_voice_stop_sip_eventchannel(m);
		return;
	}

	id = g_io_add_watch(c, G_IO_IN | G_IO_PRI | G_IO_ERR | G_IO_NVAL | G_IO_HUP, av_mm_voice_process_sip_event_msg, m);
	if (!id) {
		g_printerr("Failure adding IO watch\n");
		av_mm_voice_stop_sip_eventchannel(m);
	}

	avmodem_set_sip_GIOchannel_gsource_id(m, id);
}

static void av_mm_voice_startsip(AvModem *m) {
	struct av_thread *sipthread;

	/* Starts up the SIP thread for this modem. */
	sipthread = av_thread_setup("SIPStack", av_sip_init);
	if (!sipthread)
		return;

	avmodem_set_sipthread(m, sipthread);

	av_mm_voice_start_sip_eventchannel(m);
}

static void av_mm_voice_stopsip(AvModem *m) {
	struct av_thread *sipthread;
	struct av_thread_cmd *deinit_cmd;

	av_mm_voice_stop_sip_eventchannel(m);

	sipthread = avmodem_get_sipthread(m);
	if ( sipthread && (deinit_cmd = av_thread_cmd(0, NULL)) ) {
		av_thread_txcmd(sipthread, deinit_cmd, 0);
		av_thread_teardown(sipthread);
		g_clear_pointer(&deinit_cmd, g_free);
	}
}

gint av_mm_voice_init(AvModem *m) {
	MMModemVoice *voice;
	const gchar *dbus_path = mm_object_get_path(avmodem_get_mmobject(m));

	voice = avmodem_get_mmmodemvoice(m);

	if (!voice) {
		g_printerr("Unable to get a MMModemVoice object for %s\n",dbus_path);
		return 1;
	}

	/* Guard against "strange" state transitions: do not initialize if already present */
	if (avmodem_get_mmmodemvoice_signal_call_added(m))
		return 1;

	g_print("Attaching to voice service for %s\n",mm_modem_voice_get_path(voice));

	av_mm_voice_gsignals(m, TRUE);

	/* Starts up the SIP thread. */
	av_mm_voice_startsip(m);

	return 0;
}

gint av_mm_voice_deinit(AvModem *m) {
	MMModemVoice *voice;
	const gchar *dbus_path;

	if (!avmodem_get_mmmodemvoice_signal_call_added(m))
		return 0;

	voice = avmodem_get_mmmodemvoice(m);

	if (voice) {
		dbus_path = mm_modem_voice_get_path(voice);
		g_print("Unregistering MMModemVoice object for %s\n",dbus_path);
		av_mm_call_release_mmcalls(m);
		g_print("Disconnected %" G_GUINT16_FORMAT" signal handlers from MMModemVoice object %s\n",av_mm_voice_gsignals(m, FALSE),dbus_path);
		av_mm_voice_stopsip(m);
	}

	return 0;
}
