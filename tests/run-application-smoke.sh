#!/bin/sh
# Start the real application, native command dispatch and (optionally) terminal
# action in a private profile. The helper shell exits promptly after recording cwd.
set -eu
cd "$(dirname "$0")/.."
smoke_dir=$(mktemp -d "${TMPDIR:-/tmp}/emelfm2-application.XXXXXX")
trap 'rm -rf "$smoke_dir"' EXIT HUP INT TERM
mkdir "$smoke_dir/pane"
cat > "$smoke_dir/shell" <<'SHELL'
#!/bin/sh
pwd > terminal-started
printf 'Terminal smoke: Unicode 日本語\n'
exit 7
SHELL
chmod +x "$smoke_dir/shell"
gtk_package=${GTK_PACKAGE:-gtk+-3.0}
${CC:-cc} -shared -fPIC -Wno-deprecated-declarations -fcommon -D_FILE_OFFSET_BITS=64 \
    -Isrc -Isrc/actions -Isrc/build -Isrc/command -Isrc/command/complete \
    -Isrc/config -Isrc/dialogs -Isrc/filesystem -Isrc/utils \
    $(pkg-config --cflags "$gtk_package") tests/application-smoke.c \
    $(pkg-config --libs "$gtk_package") -ldl -o "$smoke_dir/smoke.so"
export E2_SMOKE_DIR="$smoke_dir"
export E2_SMOKE_VTE="${TEST_VTE:-0}"
LC_ALL=C.UTF-8 NO_AT_BRIDGE=1 GIO_USE_VFS=local dbus-run-session -- xvfb-run -a python3 - <<'PY'
import os, pathlib, signal, subprocess, time
root = pathlib.Path(os.environ['E2_SMOKE_DIR'])
# Test normal external SIGTERM and signals interrupting a held main-thread lock.
for signum in (None, signal.SIGTERM, signal.SIGHUP, signal.SIGINT):
    for marker in ('native-started', 'terminal-started'):
        (root/'pane'/marker).unlink(missing_ok=True)
    for marker in ('signal-requested', 'signal-returned'):
        (root/marker).unlink(missing_ok=True)
    profile = root/('config-external' if signum is None else 'config-'+signum.name)
    args = [os.environ.get('E2_SMOKE_BINARY', './emelfm2'), '-c', str(profile),
            '-1', str(root/'pane'), '-2', str(root/'pane'),
            '-s', 'session-end-warning=false', '-s', 'single-instance=false',
            '-s', 'modern-ui='+os.environ.get('E2_TEST_MODERN_UI', 'false')]
    env = dict(os.environ, LD_PRELOAD=str(root/'smoke.so'))
    if signum is not None: env['E2_SMOKE_SIGNAL'] = str(int(signum))
    with open(root/'application.log', 'w') as output:
        process = subprocess.Popen(args, stdout=output, stderr=output, env=env)
        try:
            deadline = time.monotonic() + 15
            expected = [root/'pane'/'native-started']
            if os.environ['E2_SMOKE_VTE'] == '1': expected += [root/'pane'/'terminal-started']
            while not all(path.exists() and path.stat().st_size > 0 for path in expected) and time.monotonic() < deadline:
                if process.poll() is not None: break
                time.sleep(.05)
            assert all(path.exists() and path.stat().st_size > 0 for path in expected), 'command did not start'
            assert process.poll() is None, 'application exited unexpectedly'
            if len(expected) == 2:
                assert pathlib.Path(expected[1].read_text().strip()).resolve() == (root/'pane').resolve(), expected[1].read_text()
            if signum is None:
                process.terminate()
            else:
                (root/'signal-requested').touch()
            process.wait(timeout=10)
            expected_signal = signum or signal.SIGTERM
            assert process.returncode == -expected_signal, process.returncode
            if signum is not None:
                assert (root/'signal-returned').exists(), 'signal handler did not return to the interrupted task'
            assert bool(list(profile.glob('config-*'))) == (signum != signal.SIGINT), 'wrong save-on-signal behaviour'
        except Exception:
            print('application failure, signal:', signum or 'external SIGTERM', flush=True)
            print((root/'application.log').read_text(), flush=True)
            raise
        finally:
            if process.poll() is None: process.kill(); process.wait()
print('application: native command, optional terminal action, external/locked signals and shutdown saving passed')
PY
