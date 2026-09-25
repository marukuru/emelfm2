#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
test_dir=$(mktemp -d "${TMPDIR:-/tmp}/emelfm2-directory-context.XXXXXX")
trap 'rm -rf "$test_dir"' EXIT HUP INT TERM
gtk_package=${GTK_PACKAGE:-gtk+-2.0}
${CC:-cc} -shared -fPIC -Wno-deprecated-declarations -fcommon -D_FILE_OFFSET_BITS=64 \
    -Isrc -Isrc/actions -Isrc/build -Isrc/command -Isrc/command/complete \
    -Isrc/config -Isrc/dialogs -Isrc/filesystem -Isrc/utils \
    $(pkg-config --cflags "$gtk_package") tests/directory-context.c \
    $(pkg-config --libs "$gtk_package") -o "$test_dir/test.so"
export E2_DIRECTORY_CONTEXT_TEST="$test_dir"
LC_ALL=C.UTF-8 GDK_BACKEND=x11 NO_AT_BRIDGE=1 GIO_USE_VFS=local dbus-run-session -- xvfb-run -a python3 - <<'PY'
import os, pathlib, subprocess, time
root = pathlib.Path(os.environ['E2_DIRECTORY_CONTEXT_TEST'])
for pane in ('left', 'right'):
    (root/pane).mkdir()
    for name in ('a.ctxtxt', 'b.ctximg', 'c.ctximg'):
        (root/pane/name).write_text('Context menu fixture\n')
(root/'handler').write_text('#!/bin/sh\nprintf "%s\\n" "$PWD" "$@" > "$E2_DIRECTORY_CONTEXT_TEST/result"\n')
(root/'handler').chmod(0o755)
args = ['./emelfm2', '-c', str(root/'config'), '-1', str(root/'left'), '-2', str(root/'right'),
        '-s', 'session-end-warning=false', '-s', 'single-instance=false',
        '-s', 'modern-ui='+os.environ.get('E2_TEST_MODERN_UI', 'false')]
with open(root/'log', 'w') as output:
    process = subprocess.Popen(args, env=dict(os.environ, LD_PRELOAD=str(root/'test.so')),
                               stdout=output, stderr=output)
    try:
        deadline = time.monotonic() + 40
        while not (root/'passed').exists() and process.poll() is None and time.monotonic() < deadline:
            time.sleep(.05)
        assert (root/'passed').exists(), (root/'log').read_text()
        assert process.wait(timeout=10) == 0, (root/'log').read_text()
        log = (root/'log').read_text()
        assert 'CRITICAL' not in log and 'WARNING' not in log, log
        print('Directory context menus: 28 selection and command dispatch scenarios passed')
    finally:
        if process.poll() is None:
            process.kill()
            process.wait(timeout=10)
PY
