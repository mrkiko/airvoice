// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef __av_config_h__
#define __av_config_h__

/* AV headers */
#include <av_gobjects.h>

struct av_modem_config {
	gchar *username;
	gchar *password;
	gchar *sip_host;
	gchar *sip_id;
	gchar *modem_audio_port;
	gchar *sip_local_ip_addr;
};

struct av_modem_config *av_config_parse(AvModem *m);
void av_config_free(struct av_modem_config **c);

#endif
