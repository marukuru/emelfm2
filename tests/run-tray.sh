#!/bin/sh
# Requires Xvfb and a private session bus; indicator checks need its runtime library.
set -eu
cd "$(dirname "$0")/.."
test_build=$(mktemp -d "${TMPDIR:-/tmp}/emelfm2-tray-build.XXXXXX")
trap 'rm -rf "$test_build"' EXIT HUP INT TERM
gtk_package=${GTK_PACKAGE:-gtk+-3.0}
${CC:-cc} ${CFLAGS:--O2 -g} -Wall -Wno-deprecated-declarations -fcommon \
    -D_FILE_OFFSET_BITS=64 -ffunction-sections -fdata-sections -fvisibility=hidden \
    -Isrc -Isrc/actions -Isrc/build -Isrc/command -Isrc/command/complete \
    -Isrc/config -Isrc/dialogs -Isrc/filesystem -Isrc/utils \
    $(pkg-config --cflags "$gtk_package" gio-2.0 gmodule-2.0 x11) \
    tests/tray.c src/dialogs/e2_dialog.c -Wl,--gc-sections $(pkg-config --libs "$gtk_package" gio-2.0 gmodule-2.0 x11) \
    ${LDFLAGS:-} -o "$test_build/tray"
NO_AT_BRIDGE=1 GIO_USE_VFS=local TMPDIR="$test_build" dbus-run-session -- xvfb-run -a "$test_build/tray" "$@"
