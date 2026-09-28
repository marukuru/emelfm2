#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
test_dir=$(mktemp -d "${TMPDIR:-/tmp}/emelfm2-viewer-syntax.XXXXXX")
trap 'rm -rf "$test_dir"' EXIT HUP INT TERM
gtk_package=${GTK_PACKAGE:-gtk+-2.0}
case "$gtk_package" in
    gtk+-2.0)
        syntax_package=source-highlight
        ${CXX:-c++} ${CXXFLAGS:--O2 -g} -std=c++11 -Wno-deprecated-declarations \
            -Isrc/utils $(pkg-config --cflags "$gtk_package" "$syntax_package") \
            -c src/utils/e2_source_highlight.cpp -o "$test_dir/source.o"
        extra="$test_dir/source.o -lstdc++" ;;
    gtk+-3.0) syntax_package=gtksourceview-4; extra= ;;
    *) exit 2 ;;
esac
${CC:-cc} ${CFLAGS:--O2 -g} -Wall -Wextra -Wno-deprecated-declarations -Isrc/utils \
    $(pkg-config --cflags "$gtk_package" "$syntax_package") \
    tests/viewer-syntax.c src/utils/e2_viewer_syntax.c $extra \
    $(pkg-config --libs "$gtk_package" "$syntax_package") ${LDFLAGS:-} -o "$test_dir/test"
LC_ALL=C.UTF-8 NO_AT_BRIDGE=1 xvfb-run -a "$test_dir/test"

# The release must find its definitions after relocation, without a matching
# source-highlight installation on the host.
mkdir -p "$test_dir/AppDir/usr/bin"
mv "$test_dir/test" "$test_dir/AppDir/usr/bin/test"
if [ "$gtk_package" = gtk+-2.0 ]; then gtk=2; else gtk=3; fi
sh tools/appimage-syntax-data.sh "$test_dir/AppDir" "$gtk"
LC_ALL=C.UTF-8 NO_AT_BRIDGE=1 SOURCE_HIGHLIGHT_DATADIR="$test_dir/missing" \
    xvfb-run -a "$test_dir/AppDir/usr/bin/test"
