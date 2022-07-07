// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef __av_sip_h__
#define __av_sip_h__

#include <osip2/osip.h>
#include <eXosip2/eXosip.h>

#ifdef OSIP_MONOTHREAD
#error "This code has not been tested with MONOTHREAD configuration."
#endif

enum SIP_CMDs {
	SIP_CMD_EXIT = 0,
	SIP_CMD_REGISTER = 1,
	SIP_CMD_CALL_IN_PROGRESS = 2,
};

enum CORE_MSG {
	SIP_EVENT_READY = 10,
	SIP_EVENT_INCOMING_CALL = 11
};

struct av_rtp_connection {
	char *addr;
	int port;
	int call_direction;
	gchar *serial_device;
};

void *av_sip_init(gpointer data);

#endif
