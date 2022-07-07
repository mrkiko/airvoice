// SPDX-License-Identifier: GPL-2.0-or-later

/* System headers for SOCKETPAIR(2) usage */
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>

/* errno */
#include <errno.h>

/* GLib2 headers */
#include <glib.h>

/* AV headers */
#include <av_gobjects.h>
#include <av_utils.h>
#include <av_thread.h>

static void av_thread_close_fd(int fd) {
	if (fd > 0)
		if (close(fd))
			g_printerr("Error while closing thread communication FD: %s",strerror(errno));
}

static void av_thread_close_fds(struct av_thread *t) {
	int i;

	for (i=0;i<2;i++) {
		av_thread_close_fd(t->sockets[i]);
		t->sockets[i] = -1;
	}
}

struct av_thread *av_thread_setup(gchar *name, GThreadFunc entry) {
	struct av_thread *t;
	GError *e = NULL;

	t = g_try_malloc0(sizeof *t);
	if (!t) {
		g_printerr("Failure while allocating thread data structure\n");
		return t;
	}

	/* Create socket pairs we'll use to communicate with this thread. */
	if (socketpair(AF_UNIX, SOCK_STREAM, 0, t->sockets)) {
		g_printerr("Unable to obtain socket pairs for thread communication: %s\n",strerror(errno));
		g_clear_pointer(&t, g_free);
		return t;
	}

	t->thread = g_thread_try_new(name, entry, t, &e);
	if (!t->thread) {
		av_utils_print_gerror(&e);
		av_thread_close_fds(t);
		g_clear_pointer(&t, g_free);
	}

	return t;
}

gint av_thread_teardown(struct av_thread *t) {
	if (!t)
		return 2;

	g_thread_join(t->thread);
	t->thread = NULL;

	av_thread_close_fds(t);

	g_clear_pointer(&t, g_free);
	return 0;
}
