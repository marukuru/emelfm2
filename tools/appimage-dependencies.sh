#!/bin/sh
# Package names for an explicitly selected AppImage backend. No auto-detection.
set -eu
gtk=${1:-2}
vte=${2:-0}
case "$gtk" in
    2) packages=libgtk2.0-dev; terminal=libvte-dev ;;
    3) packages=libgtk-3-dev; terminal=libvte-2.91-dev ;;
    *) echo 'GTK must be 2 or 3' >&2; exit 2 ;;
esac
case "$vte" in
    0) ;;
    1) packages="$packages $terminal" ;;
    *) echo 'WITH_VTE must be 0 or 1' >&2; exit 2 ;;
esac
printf '%s\n' "$packages libfontconfig1-dev libpango1.0-dev"
