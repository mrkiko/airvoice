// SPDX-License-Identifier: GPL-2.0-or-later

/* GLib2 headers */
#include <glib.h>

/* AV headers */
#include <av.h>

void av_utils_print_gerror(GError **current_error) {

	if (!current_error || !*current_error)
		return;

	g_printerr("ERROR: %s\n",(*current_error)->message ? (*current_error)->message : "(no error message in passed GError");
	g_error_free(*current_error);
	*current_error = NULL;

	return;
}

gint av_utils_async_start(GObject *o) {
	ll->async_counter++;

	if (ll->async_counter > 2)
		g_printerr("WARNING - suspicious async_counter value (%" G_GINT16_FORMAT")\n",ll->async_counter);

	if (o)
		g_object_ref(o);

	return ll->async_counter;
}

gint av_utils_async_end(GObject *o) {
	ll->async_counter--;

	if (ll->async_counter < 0)
		g_printerr("BUG - async counter got negative!\n");

	if (o)
		g_object_unref(o);

	return ll->async_counter;
}

MMCall *av_utils_mm_call_search(GList *l, const gchar *object_path) {
	MMCall *c = NULL;
	GList *k; /* because I always use l, but it's already in use ... :) like I am in high school again */

	for (k = l; k; k = g_list_next(k)) {
		if (!g_strcmp0(mm_call_get_path(MM_CALL(k->data)), object_path)) {
			c = MM_CALL(k->data);
			break;
		}
	}

	return c;
}
