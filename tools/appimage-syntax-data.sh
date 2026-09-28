#!/bin/sh
# Bundle definitions separately from the libraries collected by linuxdeploy.
set -eu
appdir=${1:?Usage: appimage-syntax-data.sh APPDIR GTK_VERSION}
case "${2:-2}" in
    2) package=source-highlight; directory=source-highlight ;;
    3) package=gtksourceview-4; directory=gtksourceview-4 ;;
    *) echo 'GTK must be 2 or 3' >&2; exit 2 ;;
esac
prefix=$(pkg-config --variable=prefix "$package")
mkdir -p "$appdir/usr/share/$directory"
cp -a "$prefix/share/$directory/." "$appdir/usr/share/$directory/"
