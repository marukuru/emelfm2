#!/bin/sh
# File-list type-ahead search: arrow keys move directly between matches.
set -eu
cd "$(dirname "$0")/.."
test_dir=$(mktemp -d "${TMPDIR:-/tmp}/emelfm2-quick-search.XXXXXX")
trap 'rm -rf "$test_dir"' EXIT HUP INT TERM
gtk_package=${GTK_PACKAGE:-gtk+-2.0}
${CC:-cc} -shared -fPIC -Wno-deprecated-declarations -fcommon -D_FILE_OFFSET_BITS=64 \
    -Isrc -Isrc/actions -Isrc/build -Isrc/command -Isrc/command/complete \
    -Isrc/config -Isrc/dialogs -Isrc/filesystem -Isrc/utils \
    $(pkg-config --cflags "$gtk_package") tests/quick-search.c \
    $(pkg-config --libs "$gtk_package") -o "$test_dir/test.so"
export E2_QUICK_SEARCH_TEST="$test_dir"
LC_ALL=C.UTF-8 GDK_BACKEND=x11 NO_AT_BRIDGE=1 GIO_USE_VFS=local dbus-run-session -- xvfb-run -a python3 - <<'PY'
import os, pathlib, subprocess
root = pathlib.Path(os.environ['E2_QUICK_SEARCH_TEST'])
pane = root/'pane'
pane.mkdir()
# Enough matches to need scrolling, with non-matching names on both sides.
for i in range(40):
    (pane/('quick-%02d' % i)).touch()
for i in range(10):
    (pane/('alpha-%02d' % i)).touch()
    (pane/('zulu-%02d' % i)).touch()
env = dict(os.environ, LD_PRELOAD=str(root/'test.so'))
result = subprocess.run(['./emelfm2', '-c', str(root/'config'), '-1', str(pane), '-2', str(pane),
    '-s', 'single-instance=false', '-s', 'session-end-warning=false'],
    env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=60)
log = result.stdout.decode(errors='replace')
assert result.returncode == 0 and 'CRITICAL' not in log, log
assert 'quick search arrow navigation passed' in log, log
print('quick search: arrow keys move directly between matches')
PY
