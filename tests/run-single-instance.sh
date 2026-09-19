#!/bin/sh
# Exercise real concurrent launches and activation without a session bus.
set -eu
cd "$(dirname "$0")/.."
test_dir=$(mktemp -d "${TMPDIR:-/tmp}/emelfm2-instance.XXXXXX")
trap 'rm -rf "$test_dir"' EXIT HUP INT TERM
gtk_package=${GTK_PACKAGE:-gtk+-3.0}
${CC:-cc} -shared -fPIC -Wno-deprecated-declarations -fcommon -D_FILE_OFFSET_BITS=64 \
    -Isrc -Isrc/actions -Isrc/build -Isrc/command -Isrc/command/complete \
    -Isrc/config -Isrc/dialogs -Isrc/filesystem -Isrc/utils \
    $(pkg-config --cflags "$gtk_package") tests/single-instance-smoke.c \
    $(pkg-config --libs "$gtk_package") -o "$test_dir/smoke.so"
export E2_INSTANCE_TEST="$test_dir"
NO_AT_BRIDGE=1 GIO_USE_VFS=local DBUS_SESSION_BUS_ADDRESS=unix:path=/nonexistent \
    xvfb-run -a python3 - <<'PY'
import os, pathlib, subprocess, time
root = pathlib.Path(os.environ['E2_INSTANCE_TEST'])
args = ['./emelfm2', '-c', str(root/'config'), '-s', 'single-instance=true',
        '-s', 'session-end-warning=false']
processes = []
def launch(extra=(), preload=False):
    env = dict(os.environ)
    if preload: env['LD_PRELOAD'] = str(root/'smoke.so')
    p = subprocess.Popen(args + list(extra), env=env, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    processes.append(p)
    return p
def wait_file(name):
    deadline = time.monotonic()+15
    while not (root/name).exists() and time.monotonic()<deadline:
        assert primary.poll() is None, primary.communicate()[1].decode()
        time.sleep(.05)
    assert (root/name).exists(), name
try:
    primary = launch(preload=True)
    wait_file('ready')
    secondaries = [launch() for _ in range(8)]
    for p in secondaries:
        _, stderr = p.communicate(timeout=10)
        assert p.returncode == 0, stderr.decode()
        assert not stderr, stderr.decode()
    wait_file('restored')
    assert primary.poll() is None
    # Turning the setting off still permits an independent process.
    independent = launch(['-s', 'single-instance=false'])
    time.sleep(1)
    assert independent.poll() is None
    independent.terminate()
    independent.communicate(timeout=10)
    # A crash must not leave a stale lock preventing the next startup.
    primary.kill()
    primary.communicate(timeout=10)
    primary = launch()
    time.sleep(1)
    assert primary.poll() is None
    p = launch()
    _, stderr = p.communicate(timeout=10)
    assert p.returncode == 0 and not stderr, stderr.decode()
    primary.terminate()
    primary.communicate(timeout=10)
    # The preference was saved; another normal startup acquires the lock.
    saved = next((root/'config').glob('config-*')).read_text()
    assert 'single-instance=true' in saved
    print('single-instance: concurrent launches, quiet activation, restore, opt-out and crash recovery passed')
finally:
    for p in processes:
        if p.poll() is None:
            p.kill()
            p.communicate(timeout=10)
PY
