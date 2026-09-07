#!/bin/sh
# Run from any directory; optional argument is gtk+-2.0 or gtk+-3.0.
set -eu
cd "$(dirname "$0")/.."
build_dir=$(mktemp -d /tmp/emelfm2-columns-test.XXXXXX)
trap 'rm -rf "$build_dir"' EXIT HUP INT TERM
gtk=${1:-gtk+-3.0}
includes=""
for dir in src src/actions src/build src/command src/command/complete src/config src/dialogs src/filesystem src/utils; do
    includes="$includes -I$dir"
done
${CC:-cc} ${CFLAGS:-} -Wno-deprecated-declarations -fcommon $includes \
    $(pkg-config --cflags "$gtk") tests/fileview_columns.c \
    -o "$build_dir/test" $(pkg-config --libs "$gtk") -pthread
G_DEBUG=fatal-warnings xvfb-run -a timeout 30 "$build_dir/test"
