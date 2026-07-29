#!/bin/sh
set -eu

if [ -z "${DISPLAY:-}" ]; then
	echo 'cursor-trail: no X display; skipped'
	exit 77
fi

tmpdir=$(mktemp -d "${TMPDIR:-/tmp}/st-cursor-trail.XXXXXXXX")
trap 'rm -rf "$tmpdir"' EXIT HUP INT TERM

make st
${CC:-cc} -D_XOPEN_SOURCE=700 -I/usr/X11R6/include \
	-std=c99 -Wall -Wextra tests/cursor-trail-x11.c \
	-o "$tmpdir/cursor-trail-x11" -L/usr/X11R6/lib -lX11

"$tmpdir/cursor-trail-x11"
