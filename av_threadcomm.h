// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef __av_threadcomm_h__
#define __av_threadcomm_h__

/* Is it worth to use __attribute__((__packed__)) or not? */
struct av_thread_cmd {
	int msgtype;
	void *payload;
};

struct av_thread_cmd *av_thread_cmd(int msg, void *payload);
int av_thread_txcmd(struct av_thread *t, struct av_thread_cmd *c, int mysocket);
struct av_thread_cmd *av_thread_rxcmd(struct av_thread *t, int mysocket);

#endif
