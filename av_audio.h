// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef __av_audio_h__

#include <ortp/ortp.h>

#define __av_audio_h__

enum AUDIO_EVENTS {
	AUDIO_EVENT_READY,
	AUDIO_EVENT_RTP_OK,
};

enum AUDIO_CMDs {
	CMD_AUDIO_INIT,
	CMD_AUDIO_EXIT,
};

#endif
void *av_audiothread_startup(gpointer data);