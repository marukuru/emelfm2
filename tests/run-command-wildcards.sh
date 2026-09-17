#!/bin/sh
# Build the production wildcard implementation with a synchronous test adapter.
set -eu
cd "$(dirname "$0")/.."
test_build=$(mktemp -d "${TMPDIR:-/tmp}/emelfm2-wildcard-build.XXXXXX")
trap 'rm -rf "$test_build"' EXIT HUP INT TERM
gtk_package=${GTK_PACKAGE:-gtk+-3.0}
${CC:-cc} ${CFLAGS:--O2 -g} -Wall -Wno-deprecated-declarations \
    -ffunction-sections -fdata-sections -fcommon -D_FILE_OFFSET_BITS=64 \
    -Isrc -Isrc/actions -Isrc/build -Isrc/command -Isrc/command/complete \
    -Isrc/config -Isrc/dialogs -Isrc/filesystem -Isrc/utils \
    $(pkg-config --cflags "$gtk_package") \
    tests/command-wildcards.c src/utils/e2_utils.c src/utils/e2_list.c \
    -Wl,--gc-sections $(pkg-config --libs "$gtk_package") -lm \
    ${LDFLAGS:-} -o "$test_build/command-wildcards"
"$test_build/command-wildcards" "$@"
