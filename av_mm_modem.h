// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef __av_mm_modem_h__
#define __av_mm_modem_h__

/* GLib2 headers */

/* AV headers */
#include <av.h>
#include <av_gobjects.h>

/* connects / disconnects GSignals from the MMModem objects contained in AvModem ones */
gint av_mm_modem_register(AvModem *m);
gint av_mm_modem_unregister(AvModem *m);

#endif
