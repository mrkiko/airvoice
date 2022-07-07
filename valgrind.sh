#!/bin/sh

# Point this to the appropriate path.
GLIB2_SUPPRESSIONS_FILE="../valgrind.suppressions"

if [ ! -e "${GLIB2_SUPPRESSIONS_FILE}" ]; then
	echo "Can not find valgrind GLib2 suppressions file; please edit $0 to supply one"
	exit 2
fi

rm -f valgrind.out
G_MESSAGES_DEBUG="all" G_SLICE=always-malloc G_DEBUG=gc-friendly valgrind \
--suppressions="${GLIB2_SUPPRESSIONS_FILE}" \
--leak-check=full \
--leak-resolution=high \
--show-reachable=no \
--log-file=valgrind.out \
--expensive-definedness-checks=yes \
$@

if [ -f valgrind.out ]; then
	tail -n 13 valgrind.out
fi