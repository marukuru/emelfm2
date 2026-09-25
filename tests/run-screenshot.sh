#!/bin/sh
# Run the shared screenshot regression against the selected GTK generation.
set -eu
cd "$(dirname "$0")/.."
test_dir=$(mktemp -d "${TMPDIR:-/tmp}/emelfm2-screenshot.XXXXXX")
trap 'rm -rf "$test_dir"' EXIT HUP INT TERM
gtk_package=${GTK_PACKAGE:-gtk+-2.0}
${CC:-cc} -Wno-deprecated-declarations $(pkg-config --cflags "$gtk_package") \
    tests/screenshot.c $(pkg-config --libs "$gtk_package") -o "$test_dir/test"
LC_ALL=C.UTF-8 GDK_BACKEND=x11 GDK_SCALE=1 NO_AT_BRIDGE=1 GIO_USE_VFS=local \
    dbus-run-session -- xvfb-run -a timeout 15 "$test_dir/test"
