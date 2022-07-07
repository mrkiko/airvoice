// SPDX-License-Identifier: GPL-2.0-or-later

/* GLib2 headers */
#include <glib.h>

/* AV headers */
#include <av.h>
#include <av_mm.h>

AvModem *av_storage_find_mmobject(MMObject *object) {
	GList *l;
	AvModem *m;

	m = NULL;

	for (l = ll->av_modems; l; l = g_list_next(l)) {
		m = AV_MODEM(l->data);
		if (avmodem_get_mmobject(m) == object)
			break;
	}

	return m;
}

AvModem *av_storage_find_mmobject_by_path(const gchar *object_path) {
	GList *l;
	AvModem *m;

	m = NULL;

	for (l = ll->av_modems; l; l = g_list_next(l)) {
		m = AV_MODEM(l->data);
		if (!g_strcmp0(mm_object_get_path(avmodem_get_mmobject(m)), object_path))
			break;
	}

	return m;
}

AvModem *av_storage_add_mmobject(MMObject *object) {
	AvModem *m;

	m = av_modem_new(object);
	ll->av_modems = g_list_append(ll->av_modems, m);

	return m;
}

gint av_storage_remove_avmodem(MMObject *object) {
	AvModem *m;
	gint retval = 1;

	m = av_storage_find_mmobject(object);
	if (m) {
		ll->av_modems = g_list_remove(ll->av_modems, m);
		g_object_unref(m);
		retval = 0;
	}

	return retval;
}
