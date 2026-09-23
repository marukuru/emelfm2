#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
test_dir=$(mktemp -d "${TMPDIR:-/tmp}/emelfm2-modern-flags.XXXXXX")
trap 'rm -rf "$test_dir"' EXIT HUP INT TERM
for value in '' 2 yes '0 1'; do
    if make -s help WITH_MODERN_UI="$value" >"$test_dir/log" 2>&1; then
        echo "Invalid WITH_MODERN_UI accepted: $value" >&2
        exit 1
    fi
    grep -q 'WITH_MODERN_UI must be 0 or 1' "$test_dir/log"
done
make -s help WITH_MODERN_UI=0 >"$test_dir/log" 2>&1
make -s help WITH_MODERN_UI=1 >"$test_dir/log" 2>&1
printf '%s\n' 'modern build flag: enabled, disabled and invalid values checked'
