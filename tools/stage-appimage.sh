#!/bin/sh
# Stage one backend only. Dependencies: see appimage-dependencies.sh GTK VTE.
# Afterwards run linuxdeploy with DEPLOY_GTK_VERSION matching GTK.
set -eu
appdir=${1:?Usage: stage-appimage.sh APPDIR [GTK=2] [WITH_VTE=0]}
gtk=${2:-2}
vte=${3:-0}
cd "$(dirname "$0")/.."
tools/appimage-dependencies.sh "$gtk" "$vte" >/dev/null
case "$gtk" in 2) gtk2=1; gtk3=0 ;; 3) gtk2=0; gtk3=1 ;; esac
# A fresh staging directory prevents stale plugins from a different GTK build.
if [ -e "$appdir" ]; then
    echo "Use a new AppDir: $appdir already exists" >&2
    exit 1
fi
mkdir -p "$appdir"
appdir=$(cd "$appdir" && pwd)
make -j"${JOBS:-2}" PREFIX=/usr WITH_GTK2="$gtk2" WITH_GTK3="$gtk3" WITH_VTE="$vte"
make install PREFIX="$appdir/usr" WITH_GTK2="$gtk2" WITH_GTK3="$gtk3" WITH_VTE="$vte"
mkdir -p "$appdir/usr/share/applications" "$appdir/usr/share/icons/hicolor/48x48/apps"
cp docs/desktop_environment/emelfm2.desktop "$appdir/usr/share/applications/"
cp icons/emelfm2_48.png "$appdir/usr/share/icons/hicolor/48x48/apps/emelfm2.png"
sed -i 's|^Exec=.*|Exec=emelfm2|g' "$appdir/usr/share/applications/emelfm2.desktop"
# The binary's transitive dependencies include exactly its selected VTE library.
ldd "$appdir/usr/bin/emelfm2"
printf 'Stage ready; run linuxdeploy with DEPLOY_GTK_VERSION=%s\n' "$gtk"
