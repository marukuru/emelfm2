#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
test_build=$(mktemp -d "${TMPDIR:-/tmp}/emelfm2-title-build.XXXXXX")
trap 'rm -rf "$test_build"' EXIT HUP INT TERM
gtk_package=${GTK_PACKAGE:-gtk+-3.0}
case "$gtk_package" in
    gtk+-2.0) vte_package=vte; backend=2 ;;
    gtk+-3.0) vte_package=vte-2.91; backend=3 ;;
    *) exit 1 ;;
esac
${CC:-cc} -O2 -Wall -Wno-deprecated-declarations -fcommon -D_FILE_OFFSET_BITS=64 \
    -Isrc -Isrc/actions -Isrc/build -Isrc/command -Isrc/command/complete \
    -Isrc/config -Isrc/dialogs -Isrc/filesystem -Isrc/utils \
    $(pkg-config --cflags "$gtk_package" "$vte_package") \
    tests/terminal-title.c src/command/e2_terminal_context.c "src/command/e2_terminal_vte${backend}.c" \
    $(pkg-config --libs "$gtk_package" "$vte_package") -o "$test_build/title"
LC_ALL=C.UTF-8 NO_AT_BRIDGE=1 GIO_USE_VFS=local dbus-run-session -- xvfb-run -a "$test_build/title"
