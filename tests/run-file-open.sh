#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
test_dir=$(mktemp -d "${TMPDIR:-/tmp}/emelfm2-file-open.XXXXXX")
trap 'rm -rf "$test_dir"' EXIT HUP INT TERM
gtk_package=${GTK_PACKAGE:-gtk+-2.0}
# Optimization is essential: incorrect malloc attributes hide alias checks.
${CC:-cc} -O2 -shared -fPIC -Wno-deprecated-declarations -fcommon -D_FILE_OFFSET_BITS=64 \
    -Isrc -Isrc/actions -Isrc/build -Isrc/command -Isrc/command/complete \
    -Isrc/config -Isrc/dialogs -Isrc/filesystem -Isrc/utils \
    $(pkg-config --cflags "$gtk_package") tests/file-open.c \
    $(pkg-config --libs "$gtk_package") -o "$test_dir/test.so"
export E2_FILE_OPEN_TEST="$test_dir"
LC_ALL=C.UTF-8 GDK_BACKEND=x11 G_DEBUG=fatal-criticals NO_AT_BRIDGE=1 GIO_USE_VFS=local dbus-run-session -- xvfb-run -a python3 - <<'PY'
import os, pathlib, subprocess
root = pathlib.Path(os.environ['E2_FILE_OPEN_TEST'])
(root/'pane').mkdir()
# The launcher only needs a regular file; no private image fixture is required.
for name in ('ordinary.JPG', 'image with spaces.JPG'):
    (root/'pane'/name).write_text('External file activation fixture\n')
(root/'handler').write_text('#!/bin/sh\nprintf "%s\\0" "$@" > "$E2_FILE_OPEN_TEST/result.pending"\n'
    'mv "$E2_FILE_OPEN_TEST/result.pending" "$E2_FILE_OPEN_TEST/result"\n')
(root/'handler').chmod(0o755)
run = subprocess.run([os.environ.get('E2_FILE_OPEN_BINARY', './emelfm2'),
    '-c', str(root/'config'), '-1', str(root/'pane'), '-2', str(root/'pane'),
    '-s', 'session-end-warning=false', '-s', 'single-instance=false'],
    env=dict(os.environ, LD_PRELOAD=os.environ.get('E2_FILE_OPEN_PRELOAD', '') + str(root/'test.so')),
    stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=40)
log = run.stdout.decode(errors='replace')
assert run.returncode == 0, log
assert 'file activation: all 8 commands received the image path' in log, log
assert 'CRITICAL' not in log, log
print('File activation: optimized macro ownership, plain/labeled commands, no macro/%f/%p, uppercase extensions and spaces passed')
PY
