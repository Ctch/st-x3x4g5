#!/bin/sh
set -eu

if [ -z "${DISPLAY:-}" ]; then
	echo 'csi-u-input: no X display; skipped'
	exit 77
fi

tmpdir=$(mktemp -d "${TMPDIR:-/tmp}/st-csi-u.XXXXXXXX")
trap 'rm -rf "$tmpdir"' EXIT HUP INT TERM

case $(uname -s) in
	OpenBSD) feature_flags='-D_XOPEN_SOURCE=700 -D_BSD_SOURCE' ;;
	*)       feature_flags='-D_XOPEN_SOURCE=700 -D_DEFAULT_SOURCE' ;;
esac

make st
${CC:-cc} $feature_flags -std=c99 -Wall -Wextra \
	tests/csi-u-input.c -o "$tmpdir/csi-u-input"
${CC:-cc} $feature_flags -I/usr/X11R6/include -std=c99 -Wall -Wextra \
	tests/csi-u-x11.c -o "$tmpdir/csi-u-x11" \
	-L/usr/X11R6/lib -lX11 -lXtst

"$tmpdir/csi-u-x11" "$tmpdir/csi-u-input" "$tmpdir/report"
