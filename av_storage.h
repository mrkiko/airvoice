// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef __av_storage_h__
#define __av_storage_h__

#include <av_gobjects.h>

AvModem *av_storage_find_mmobject(MMObject *object);
AvModem *av_storage_find_mmobject_by_path(const gchar *object_path);

AvModem *av_storage_add_mmobject(MMObject *object);

gint av_storage_remove_avmodem(MMObject *object);

#endif
