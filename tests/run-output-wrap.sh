#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
test_dir=$(mktemp -d "${TMPDIR:-/tmp}/emelfm2-output-wrap.XXXXXX")
trap 'rm -rf "$test_dir"' EXIT HUP INT TERM
gtk_package=${GTK_PACKAGE:-gtk+-2.0}
${CC:-cc} -shared -fPIC -Wno-deprecated-declarations -fcommon -D_FILE_OFFSET_BITS=64 \
    -Isrc -Isrc/actions -Isrc/build -Isrc/command -Isrc/command/complete \
    -Isrc/config -Isrc/dialogs -Isrc/filesystem -Isrc/utils \
    $(pkg-config --cflags "$gtk_package") tests/output-wrap.c \
    $(pkg-config --libs "$gtk_package") -o "$test_dir/test.so"
export E2_OUTPUT_WRAP_TEST="$test_dir"
LC_ALL=C.UTF-8 NO_AT_BRIDGE=1 GIO_USE_VFS=local dbus-run-session -- xvfb-run -a python3 - <<'PY'
import os, pathlib, subprocess, time
root = pathlib.Path(os.environ['E2_OUTPUT_WRAP_TEST'])
for mode in ('words', 'everywhere', 'none'):
    (root/'passed').unlink(missing_ok=True)
    with open(root/'log', 'w') as output:
        process = subprocess.Popen(['./emelfm2', '-c', str(root/mode), '-1', str(root),
            '-s', 'session-end-warning=false', '-s', 'output-wrap-mode=' + mode],
            env=dict(os.environ, LD_PRELOAD=str(root/'test.so')), stdout=output, stderr=output)
        try:
            deadline = time.monotonic() + 20
            while not (root/'passed').exists() and process.poll() is None and time.monotonic() < deadline:
                time.sleep(.05)
            assert (root/'passed').exists(), (mode, (root/'log').read_text())
            assert process.wait(timeout=10) == 0, (root/'log').read_text()
            log = (root/'log').read_text()
            assert 'CRITICAL' not in log and 'WARNING' not in log, log
        finally:
            if process.poll() is None: process.kill(); process.wait(timeout=10)
print('command log: startup wrap modes, long words, live changes, resize and rebuild passed')
PY
