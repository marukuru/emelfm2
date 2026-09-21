#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
test_dir=$(mktemp -d "${TMPDIR:-/tmp}/emelfm2-vte-ui.XXXXXX")
trap 'rm -rf "$test_dir"' EXIT HUP INT TERM
gtk_package=${GTK_PACKAGE:-gtk+-3.0}
${CC:-cc} -shared -fPIC -Wno-deprecated-declarations -fcommon -D_FILE_OFFSET_BITS=64 \
    -Isrc -Isrc/actions -Isrc/build -Isrc/command -Isrc/command/complete \
    -Isrc/config -Isrc/dialogs -Isrc/filesystem -Isrc/utils \
    $(pkg-config --cflags "$gtk_package") tests/vte-ui-smoke.c \
    $(pkg-config --libs "$gtk_package") -o "$test_dir/smoke.so"
export E2_VTE_UI_TEST="$test_dir"
LC_ALL=C.UTF-8 NO_AT_BRIDGE=1 GIO_USE_VFS=local dbus-run-session -- xvfb-run -a python3 - <<'PY'
import os, pathlib, subprocess, time
root = pathlib.Path(os.environ['E2_VTE_UI_TEST'])
for name in ('left', 'right'):
    (root/name).mkdir()
(root/'shell').write_text('#!/bin/sh\npwd > terminal-started\nprintf "Terminal UI test\\n"\nexit 7\n')
(root/'shell').chmod(0o755)
(root/'live-shell').write_text('#!/bin/sh\necho $$ > terminal-running\ntrap "exit 0" HUP TERM\nwhile :; do sleep 1; done\n')
(root/'live-shell').chmod(0o755)
args = ['./emelfm2', '-c', str(root/'config'), '-1', str(root/'left'), '-2', str(root/'right'),
        '-s', 'session-end-warning=false', '-s', 'pane-tabs=true']
with open(root/'log', 'w') as output:
    p = subprocess.Popen(args, env=dict(os.environ, LD_PRELOAD=str(root/'smoke.so')),
                         stdout=output, stderr=output)
    try:
        deadline = time.monotonic() + 25
        while not (root/'passed').exists() and p.poll() is None and time.monotonic() < deadline:
            time.sleep(.05)
        assert (root/'passed').exists(), (root/'log').read_text()
        assert p.wait(timeout=10) == 0, (root/'log').read_text()
        log = (root/'log').read_text()
        assert 'CRITICAL' not in log and 'WARNING' not in log, log
        print('VTE UI regression checks passed')
    finally:
        if p.poll() is None:
            p.kill()
            p.wait(timeout=10)
PY
