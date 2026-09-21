#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
test_dir=$(mktemp -d "${TMPDIR:-/tmp}/emelfm2-viewer-text.XXXXXX")
trap 'rm -rf "$test_dir"' EXIT HUP INT TERM
${CC:-cc} ${CFLAGS:--O2 -g} -Wall -Wextra -Isrc/utils $(pkg-config --cflags gio-2.0) \
    tests/viewer-text.c src/utils/e2_viewer_text.c $(pkg-config --libs gio-2.0) ${LDFLAGS:-} -o "$test_dir/test"
"$test_dir/test"
