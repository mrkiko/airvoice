// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef __av_utils_h__
#define __av_utils_h__

void av_utils_print_gerror(GError **current_error);

gint av_utils_async_start(GObject *o);
gint av_utils_async_end(GObject *o);

MMCall *av_utils_mm_call_search(GList *l, const gchar *object_path);

#endif
