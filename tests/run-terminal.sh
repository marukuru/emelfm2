#!/bin/sh
# Run after building with WITH_VTE=1 and the matching GTK_PACKAGE.
set -eu
cd "$(dirname "$0")/.."
test_build=$(mktemp -d "${TMPDIR:-/tmp}/emelfm2-terminal-build.XXXXXX")
trap 'rm -rf "$test_build"' EXIT HUP INT TERM
gtk_package=${GTK_PACKAGE:-gtk+-3.0}
case "$gtk_package" in
    gtk+-2.0) vte_package=vte; backend=2 ;;
    gtk+-3.0) vte_package=vte-2.91; backend=3 ;;
    *) echo "Unsupported GTK_PACKAGE: $gtk_package" >&2; exit 1 ;;
esac
${CC:-cc} ${CFLAGS:--O2 -g} -Wall -Wno-deprecated-declarations \
    -ffunction-sections -fdata-sections -fcommon -D_FILE_OFFSET_BITS=64 \
    -Isrc -Isrc/actions -Isrc/build -Isrc/command -Isrc/command/complete \
    -Isrc/config -Isrc/dialogs -Isrc/filesystem -Isrc/utils \
    $(pkg-config --cflags "$gtk_package" "$vte_package") \
    tests/terminal.c "src/command/e2_terminal_vte${backend}.c" \
    -Wl,--gc-sections $(pkg-config --libs "$gtk_package" "$vte_package") -lm -pthread \
    ${LDFLAGS:-} -o "$test_build/terminal"
LC_ALL=C.UTF-8 NO_AT_BRIDGE=1 GIO_USE_VFS=local dbus-run-session -- xvfb-run -a "$test_build/terminal" "$@"
