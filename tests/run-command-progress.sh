#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
test_dir=$(mktemp -d "${TMPDIR:-/tmp}/emelfm2-command-progress.XXXXXX")
trap 'rm -rf "$test_dir"' EXIT HUP INT TERM
gtk_package=${GTK_PACKAGE:-gtk+-2.0}
${CC:-cc} -shared -fPIC -Wno-deprecated-declarations -fcommon -D_FILE_OFFSET_BITS=64 \
    -Isrc -Isrc/actions -Isrc/build -Isrc/command -Isrc/command/complete \
    -Isrc/config -Isrc/dialogs -Isrc/filesystem -Isrc/utils \
    $(pkg-config --cflags "$gtk_package") tests/command-progress.c \
    $(pkg-config --libs "$gtk_package") -ldl -o "$test_dir/test.so"
export E2_COMMAND_PROGRESS_TEST="$test_dir"
LC_ALL=C.UTF-8 GIT_CONFIG_GLOBAL=/dev/null GIT_CONFIG_NOSYSTEM=1 NO_AT_BRIDGE=1 GIO_USE_VFS=local dbus-run-session -- xvfb-run -a python3 - <<'PY'
import os, pathlib, subprocess, time
root = pathlib.Path(os.environ['E2_COMMAND_PROGRESS_TEST'])
# Force pack transfer with --no-local, without relying on network access.
source = root/'origin'
source.mkdir()
subprocess.run(['git', 'init', '-q', str(source)], check=True)
base = os.urandom(8192)
for index in range(80):
    (source/str(index)).write_bytes(base + bytes([index]) * 100)
subprocess.run(['git', '-C', str(source), 'add', '.'], check=True)
subprocess.run(['git', '-C', str(source), '-c', 'user.name=Test', '-c', 'user.email=test@example.invalid',
                '-c', 'commit.gpgsign=false', 'commit', '-qm', 'Fixture'], check=True)
(root/'probe.py').write_text('''import os, sys, time
print('stdin-is-pipe' if not os.isatty(0) else 'unexpected-stdin-tty', flush=True)
print('stdout-is-pipe' if not os.isatty(1) else 'unexpected-stdout-tty', flush=True)
sys.stderr.write('stderr-is-terminal\\n' if os.isatty(2) else 'stderr-is-pipe\\n')
sys.stderr.write('progress-start\\rprogress-complete\\n')
sys.stderr.flush()
for i in range(512):
    os.write(1, b'o' * 128 + b'\\n')
    os.write(2, b'e' * 128 + b'\\n')
print('stdout-tail', flush=True)
os.close(1)
sys.stderr.write('stderr-tail\\n')
sys.stderr.flush()
os.close(2)
time.sleep(.2)  # Stream EOF must not invent an exit status before the child exits.
sys.exit(7)
''')
for fallback in ('0', '1'):
    (root/'passed').unlink(missing_ok=True)
    with open(root/'log', 'w') as output:
        process = subprocess.Popen([os.environ.get('E2_COMMAND_PROGRESS_BINARY', './emelfm2'),
            '-c', str(root/('config-' + fallback)), '-1', str(root), '-s', 'session-end-warning=false'],
            env=dict(os.environ, LD_PRELOAD=str(root/'test.so'), E2_COMMAND_PROGRESS_FALLBACK=fallback), stdout=output, stderr=output)
        try:
            deadline = time.monotonic() + 90
            while not (root/'passed').exists() and process.poll() is None and time.monotonic() < deadline:
                time.sleep(.05)
            assert (root/'passed').exists(), (root/'log').read_text()
            assert process.wait(timeout=10) == 0, (root/'log').read_text()
            log = (root/'log').read_text()
            assert 'CRITICAL' not in log and 'WARNING' not in log, log[:3000] + log[-2000:]
        finally:
            if process.poll() is None: process.kill(); process.wait(timeout=10)
print('command log: Git progress, direct/shell/sync execution, large output, exit status, redirections and PTY fallback passed')
PY
