#!/bin/sh
set -eu

tmpdir=$(mktemp -d "${TMPDIR:-/tmp}/st-wide-overwrite.XXXXXXXX")
trap 'rm -rf "$tmpdir"' EXIT HUP INT TERM

case $(uname -s) in
	OpenBSD) feature_flags='-D_XOPEN_SOURCE=600 -D_BSD_SOURCE' ;;
	*)       feature_flags='-D_XOPEN_SOURCE=600 -D_DEFAULT_SOURCE' ;;
esac

${CC:-cc} \
	-DDEBUG_WIDE $feature_flags \
	-I. -std=c99 -Wall -Wextra \
	tests/wide-overwrite.c st.c -o "$tmpdir/wide-overwrite" -lutil

"$tmpdir/wide-overwrite"
