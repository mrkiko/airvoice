// SPDX-License-Identifier: GPL-2.0-or-later

/* AV headers */
#include <av.h>
#include <av_mm_modem.h>
#include <av_mm_voice.h>
#include <av_gobjects.h>

struct _AvModem {
	GObject parent;

	/* MMObject is guaranteed to exist when a modem exists */
	MMObject *object;

	/* services (D-Bus interfaces), and the signals we are interested in */

	/* MMModem object */
	MMModem *modem;

	/* "state-changed" GSignals for MMModem object */
	gulong modem_signal_statechanged;

	/* MMModemVoice object */
	MMModemVoice *voice;

	/* GSignals for keeping track of voice calls */
	gulong voice_signal_call_added;
	gulong voice_signal_call_deleted;

	/* MMCall objects list for this modem */
	GList *mmcalls;

	/* To keep track of active calls. */
	gint active_calls_counter;

	/* Thread handling SIP communications for this MMModemVoice object. */
	struct av_thread *sipthread;

	/* Communications with SIP stack thread. */
	GIOChannel *voice_sip_giochannel;
	guint voice_sip_giochannel_watch_id;
};

G_DEFINE_TYPE(AvModem, av_modem, G_TYPE_OBJECT)

static void av_modem_init(AvModem *self) {
	g_print("%s invoked\n",__FUNCTION__);
}

static void av_modem_dispose(GObject *gobject) {
	AvModem *m = AV_MODEM(gobject);
	g_print("%s invoked\n",__FUNCTION__);
	av_mm_voice_deinit(m);
	g_clear_object(&m->voice);

	av_mm_modem_unregister(m);
	g_clear_object(&m->modem);

	g_clear_object(&m->object);

	G_OBJECT_CLASS (av_modem_parent_class)->dispose (gobject);
}

static void av_modem_finalize(GObject *gobject) {
	g_print("%s invoked\n",__FUNCTION__);
	/* e.g.: g_free for filename */
	G_OBJECT_CLASS (av_modem_parent_class)->finalize (gobject);
}

static void av_modem_class_init(AvModemClass *klass) {
	g_print("%s invoked\n",__FUNCTION__);
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->dispose = av_modem_dispose;
	object_class->finalize = av_modem_finalize;
}

AvModem *av_modem_new(MMObject *mmobject) {
	AvModem *m = g_object_new(AV_TYPE_MODEM, NULL);
	AV_MODEM(m)->object = g_object_ref(mmobject);
	return m;
}

MMObject *avmodem_get_mmobject(AvModem *m) {
	return AV_MODEM(m)->object;
}

/* MMModem */
MMModem *avmodem_get_mmmodem(AvModem *m) {
	if (!m->modem)
		m->modem = mm_object_get_modem(m->object);

	return m->modem;
}

gulong avmodem_get_mmmodem_signal_statechange(AvModem *m) {
	return m->modem_signal_statechanged;
}

AvModem *avmodem_set_mmmodem_signal_statechange(AvModem *m, gulong value) {
	m->modem_signal_statechanged = value;
	return m;
}

/* MMModemVoice */
MMModemVoice *avmodem_get_mmmodemvoice(AvModem *m) {
	if (!m->voice) {
		m->voice = mm_object_get_modem_voice(m->object);
	}

	return m->voice;
}

gulong avmodem_get_mmmodemvoice_signal_call_added(AvModem *m) {
	return m->voice_signal_call_added;
}

AvModem *avmodem_set_mmmodemvoice_signal_call_added(AvModem *m, gulong value) {
	m->voice_signal_call_added = value;
	return m;
}

gulong avmodem_get_mmmodemvoice_signal_call_deleted(AvModem *m) {
	return m->voice_signal_call_deleted;
}

AvModem *avmodem_set_mmmodemvoice_signal_call_deleted(AvModem *m, gulong value) {
	m->voice_signal_call_deleted = value;
	return m;
}

GList *avmodem_get_mmmodemvoice_calls_list(AvModem *m) {
	return m->mmcalls;
}

AvModem *avmodem_update_mmmodemvoice_calls_list(AvModem *m, GList *l) {
	m->mmcalls = l;
	return m;
}

gint avmodem_get_active_calls_counter(AvModem *m) {
	return m->active_calls_counter;
}

AvModem *avmodem_set_active_calls_counter(AvModem *m, gint counter) {
	m->active_calls_counter = counter;
	return m;
}

struct av_thread *avmodem_get_sipthread(AvModem *m) {
	return m->sipthread;
}

AvModem *avmodem_set_sipthread(AvModem *m, struct av_thread *t) {
	m->sipthread = t;
	return m;
}

GIOChannel *avmodem_get_sip_GIOchannel(AvModem *m) {
	return m->voice_sip_giochannel;
}

AvModem *avmodem_set_sip_GIOchannel(AvModem *m, GIOChannel *sip_giochannel) {
	m->voice_sip_giochannel = sip_giochannel;
	return m;
}

guint avmodem_get_sip_GIOchannel_gsource_id(AvModem *m) {
	return m->voice_sip_giochannel_watch_id;
}

AvModem *avmodem_set_sip_GIOchannel_gsource_id(AvModem *m, guint src_id) {
	m->voice_sip_giochannel_watch_id = src_id;
	return m;
}
