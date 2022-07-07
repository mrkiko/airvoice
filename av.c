// SPDX-License-Identifier: GPL-2.0-or-later

/* GLib2 headers */
#include <glib.h>
#include <glib-unix.h>

/* AV headers */
#include <av.h>
#include <av_mm.h>

/* global AV lifecycle state structure */
struct av_ll *ll;

/*
 * This function runs as a timeout GSource, and as such follows GLib2 semantics.
 * It's purpose is to give time for things to deinit "cleanly".
*/
static gboolean av_handle_exit(gpointer user_data) {
	if (!ll->async_counter) {
		g_print("Exiting...\n");
		g_main_loop_quit(ll->loop);
		ll->exit_timeout_src_tag = 0; /* to be understood */
		return G_SOURCE_REMOVE;
	}

	return G_SOURCE_CONTINUE;
}

/*
 * AV exit logic. This function should be invoked to exit the program.
 * It's purpose is to start deinitializing e.g.: MM interaction code, while
 * the main event loop still runs.
 *
 * Returns:
 * nothing, since if an error occurs here we have no way to handle that in a
 * way that makes sense.
*/
static void av_exit(void) {
	av_mm_deinit();
	ll->exit_timeout_src_tag = g_timeout_add_seconds(1, G_SOURCE_FUNC(av_handle_exit), NULL);
	if (!ll->exit_timeout_src_tag)
		g_printerr("Failure when starting exit logic.\n");

	return;
}

/*
 * UNIX signals GSource callback, invoked when SIGINT is received. This
 * function follows GLib2 semantics for GSources.
*/
static gboolean av_sigint(void) {
	g_print("Got SIGINT!\n");
	av_exit();
	ll->unix_signals_src_tag = 0;
	return G_SOURCE_REMOVE;
}

/*
 * Prepares before entering the main loop.
 * In particular:
 * - allocates an AV state structure, usually the global one
 * - allocates main loop
 * - adds to main loop the UNIX signals intercept source to e.g.: handle CTRL+C
 *
 * Even tough attaching the UNIX signals GSource may fail, this is not
 * considered to be a fatal error.
 *
 * Returns:
 * - an allocated struct av_ll on success, NULL on failure.
*/
static struct av_ll *av_ll_prepare(void) {
	struct av_ll *new_ll;

	new_ll = g_try_malloc0(sizeof *new_ll);

	if (!new_ll) {
		g_printerr("Failure while allocating AV global instance state\n");
		return new_ll;
	}

	/* new GLib event loop: only default GMainContext is used */
	new_ll->loop = g_main_loop_new(NULL, FALSE);

	/* UNIX signals GSource and other nice things here */
	new_ll->unix_signals_src_tag = g_unix_signal_add(SIGINT, G_SOURCE_FUNC(av_sigint), NULL);
	if (!new_ll->unix_signals_src_tag)
		g_printerr("Failure connecting UNIX signal source to GMainContext\n");

	return new_ll;
}

/*
 * Deallocates memory after the main loop.
 *
 * Returns:
 * nothing.
*/
static void av_ll_end(void) {
	if (ll->unix_signals_src_tag) {
		g_source_remove(ll->unix_signals_src_tag);
		ll->unix_signals_src_tag = 0;
	}

	if (ll->exit_timeout_src_tag) {
		g_source_remove(ll->exit_timeout_src_tag);
		ll->exit_timeout_src_tag = 0;
	}

	if (ll->loop) {
		g_main_loop_unref(ll->loop);
		ll->loop = NULL;
	}

	g_free(ll);
	ll = NULL;
}

/*
 * Starts the main loop dependant logic and enters main loop.
 *
 * Returns:
 * nothing.
*/
static void av_ll_start(void) {
	if (av_mm_init())
		av_exit();

	g_main_loop_run(ll->loop);

	return;
}

/*
 * The main function.

 * Exit codes:
 * 1 - allocation failure from av_ll_prepare()
*/
gint main(void) {
	/* Prepare AV for running */
	ll = av_ll_prepare();
	if (!ll)
		return 1;

	av_ll_start();

	av_ll_end();

	return 0;
}
