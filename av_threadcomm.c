// SPDX-License-Identifier: GPL-2.0-or-later

/* System headers for SOCKETPAIR(2) usage */
#include <unistd.h>

/* AV headers */
#include <av_thread.h>
#include <av_threadcomm.h>

struct av_thread_cmd *av_thread_cmd(int msg, void *payload) {
	struct av_thread_cmd *cmd;

	cmd = g_try_malloc0(sizeof *cmd);
	if (!cmd)
		g_printerr("Failure allocating thead command structure\n");
	else {
		cmd->msgtype = msg;
		cmd->payload = payload;
	}

	return cmd;
}

int av_thread_txcmd(struct av_thread *t, struct av_thread_cmd *c, int mysocket) {
	int remaining_data = (sizeof *c);
	ssize_t written_data;
	char *bufptr = (char*)c;
	int retval;

	g_assert(mysocket == 0 || mysocket == 1);
	retval = 0;

	while (remaining_data) {
		written_data = write(t->sockets[mysocket], bufptr, remaining_data);
		if (written_data<0) {
			if (errno == EAGAIN)
				continue;
			g_printerr("Error while writing command to thread socket: %s\n",strerror(errno));
			retval++;
			break;
		}
		remaining_data -= written_data;
		bufptr += written_data;
	}

	return retval;
}

struct av_thread_cmd *av_thread_rxcmd(struct av_thread *t, int mysocket) {
	ssize_t bytes_read;
	struct av_thread_cmd *rxc;
	int remaining_data = (sizeof *rxc);
	char *bufptr;

	g_assert(mysocket == 0 || mysocket == 1);

	rxc = g_try_malloc0(sizeof *rxc);
	if (!rxc) {
		g_printerr("Failure while allocating buffer for receiving message\n");
		return rxc;
	}

	bufptr = (char*)rxc;

	while(remaining_data) {
		bytes_read = read(t->sockets[mysocket], bufptr, remaining_data);
		if (bytes_read < 0) {
			if (errno == EAGAIN)
				continue;
			g_printerr("Unable to read message from socket: %s\n",strerror(errno));
			g_clear_pointer(&rxc, g_free);
			break;
		}
		remaining_data -= bytes_read;
		bufptr += bytes_read;
	}

	return rxc;
}
