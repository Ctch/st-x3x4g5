#!/bin/sh
set -eu

tmpdir=$(mktemp -d "${TMPDIR:-/tmp}/st-split-state.XXXXXXXX")
trap 'rm -rf "$tmpdir"' EXIT HUP INT TERM

case $(uname -s) in
	OpenBSD) feature_flags='-D_XOPEN_SOURCE=600 -D_BSD_SOURCE' ;;
	*)       feature_flags='-D_XOPEN_SOURCE=600 -D_DEFAULT_SOURCE' ;;
esac

${CC:-cc} \
	-DDEBUG_SPLIT $feature_flags \
	-I. -std=c99 -Wall -Wextra \
	tests/split-state.c st.c -o "$tmpdir/split-state" -lutil

"$tmpdir/split-state"
