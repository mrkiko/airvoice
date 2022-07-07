#ifndef __av_gobjects_h__
#define __av_gobjects_h__

/* GLib2 GObject headers */
#include <glib-object.h>

/* ModemManager's libmm-glib headers */
#include <libmm-glib.h>

G_BEGIN_DECLS

#define AV_TYPE_MODEM av_modem_get_type()

G_DECLARE_FINAL_TYPE(AvModem, av_modem, AV, MODEM, GObject)

G_END_DECLS

AvModem *av_modem_new(MMObject *mmobject);

/* MMObject */
MMObject *avmodem_get_mmobject(AvModem *m);

/* MMModem */
MMModem *avmodem_get_mmmodem(AvModem *m);
gulong avmodem_get_mmmodem_signal_statechange(AvModem *m);
AvModem *avmodem_set_mmmodem_signal_statechange(AvModem *m, gulong value);

/* MMModemVoice */
MMModemVoice *avmodem_get_mmmodemvoice(AvModem *m);
gulong avmodem_get_mmmodemvoice_signal_call_added(AvModem *m);
AvModem *avmodem_set_mmmodemvoice_signal_call_added(AvModem *m, gulong value);
gulong avmodem_get_mmmodemvoice_signal_call_deleted(AvModem *m);
AvModem *avmodem_set_mmmodemvoice_signal_call_deleted(AvModem *m, gulong value);

GList *avmodem_get_mmmodemvoice_calls_list(AvModem *m);
AvModem *avmodem_update_mmmodemvoice_calls_list(AvModem *m, GList *l);

gint avmodem_get_active_calls_counter(AvModem *m);
AvModem *avmodem_set_active_calls_counter(AvModem *m, gint counter);

struct av_thread *avmodem_get_sipthread(AvModem *m);
AvModem *avmodem_set_sipthread(AvModem *m, struct av_thread *t);

GIOChannel *avmodem_get_sip_GIOchannel(AvModem *m);
AvModem *avmodem_set_sip_GIOchannel(AvModem *m, GIOChannel *sip_giochannel);
guint avmodem_get_sip_GIOchannel_gsource_id(AvModem *m);
AvModem *avmodem_set_sip_GIOchannel_gsource_id(AvModem *m, guint src_id);

#endif
