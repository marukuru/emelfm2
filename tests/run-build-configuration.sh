#!/bin/sh
# Use a clean checkout/copy for each invocation; generated headers are shared.
set -eu
cd "$(dirname "$0")/.."
tests/check-terminal-build-flags.sh
tests/check-modern-build-flags.sh
gtk=${TEST_GTK:-2}
vte=${TEST_VTE:-0}
command=${TEST_COMMAND:-0}
modern=${TEST_MODERN_UI:-1}
case "$gtk" in
    2) gtk2=1; gtk3=0; export GTK_PACKAGE=gtk+-2.0 ;;
    3) gtk2=0; gtk3=1; export GTK_PACKAGE=gtk+-3.0 ;;
    *) exit 2 ;;
esac
make -j"${JOBS:-2}" WITH_GTK2="$gtk2" WITH_GTK3="$gtk3" WITH_VTE="$vte" WITH_MODERN_UI="$modern" NEW_COMMAND="$command" DEBUG=0 I18N=0
tests/run-modern-ui.sh
export E2_TEST_MODERN_UI=true
libraries=$(ldd ./emelfm2)
if [ "$gtk" = 2 ]; then
    forbidden='libgtk-3|libvte-2.91'
else
    forbidden='libgtk-x11-2|libvte.so'
fi
if printf '%s\n' "$libraries" | grep -Eq "$forbidden"; then
    echo "Mixed GTK/VTE generations" >&2; exit 1
fi
if [ "$vte" = 0 ]; then
    if printf '%s\n' "$libraries" | grep -q libvte; then
        echo 'Unexpected VTE dependency' >&2; exit 1
    fi
else
    printf '%s\n' "$libraries" | grep libvte
    tests/run-terminal.sh
fi
sh tests/run-command-wildcards.sh
tests/run-command-progress.sh
TEST_VTE="$vte" tests/run-application-smoke.sh
tests/run-content-drag.sh
tests/run-output-wrap.sh
tests/run-viewer-text.sh
tests/run-viewer-ui.sh
