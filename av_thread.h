// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef __av_thread_h__
#define __av_thread_h__

/* GLib2 headers */
#include <glib.h>

struct av_thread {
	int sockets[2];
	GThread *thread;
};

struct av_thread *av_thread_setup(gchar *name, GThreadFunc entry);
gint av_thread_teardown(struct av_thread *t);

#endif
