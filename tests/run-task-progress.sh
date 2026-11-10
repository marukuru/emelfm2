#!/bin/sh
# Copy and move progress window: shown for slow tasks only, with pause, resume
# and cancel, which leaves no partial item and keeps existing destinations.
set -eu
cd "$(dirname "$0")/.."
test_dir=$(mktemp -d "${TMPDIR:-/tmp}/emelfm2-progress.XXXXXX")
trap 'rm -rf "$test_dir"' EXIT HUP INT TERM
gtk_package=${GTK_PACKAGE:-gtk+-2.0}
${CC:-cc} -shared -fPIC -Wno-deprecated-declarations -fcommon -D_FILE_OFFSET_BITS=64 \
    -Isrc -Isrc/actions -Isrc/build -Isrc/command -Isrc/command/complete \
    -Isrc/config -Isrc/dialogs -Isrc/filesystem -Isrc/utils \
    $(pkg-config --cflags "$gtk_package") tests/task-progress.c \
    $(pkg-config --libs "$gtk_package") -ldl -o "$test_dir/test.so"
export E2_PROGRESS_TEST="$test_dir"
LC_ALL=C.UTF-8 NO_AT_BRIDGE=1 GIO_USE_VFS=local dbus-run-session -- xvfb-run -a python3 - <<'PY'
import os, pathlib, shutil, subprocess, tempfile
root = pathlib.Path(os.environ['E2_PROGRESS_TEST'])
# 4 MiB, read in 64 KiB blocks, each delayed: about 1.3 s to copy
BIG = bytes(range(256)) * 16384

def run(name, action, select, request, src, dst, watch):
    base = root/name
    base.mkdir(parents=True, exist_ok=True)
    result = base/'result'
    env = dict(os.environ, LD_PRELOAD=str(root/'test.so'), E2_PG_ACTION=action,
               E2_PG_SELECT=select, E2_PG_DO=request, E2_PG_WATCH=str(watch),
               E2_PG_RESULT=str(result))
    process = subprocess.run(['./emelfm2', '-c', str(base/'config'), '-1', str(src), '-2', str(dst),
        '-s', 'single-instance=false', '-s', 'session-end-warning=false',
        '-s', 'confirm-overwrite=true'],
        env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=90)
    log = process.stdout.decode(errors='replace')
    assert process.returncode == 0 and result.exists(), (name, log)
    assert 'CRITICAL' not in log, (name, log)
    return result.read_text(), log

def dirs(name, srcbase=None):
    src = srcbase if srcbase is not None else root/name/'src'
    dst = root/name/'dst'
    src.mkdir(parents=True)
    dst.mkdir(parents=True)
    (src/'big.slow').write_bytes(BIG)
    (src/'small.txt').write_text('small')
    return src, dst

def entries(path):
    return sorted(p.name for p in path.iterdir())

# a slow copy shows the window, and a pause holds the copy until resumed
src, dst = dirs('copy-pause')
shown, log = run('copy-pause', 'copy', 'big.slow,small.txt', 'pause', src, dst, dst/'big.slow')
assert shown == 'window 1 paused 1\n', (shown, log)
assert (dst/'big.slow').read_bytes() == BIG and (dst/'small.txt').read_text() == 'small'
assert entries(dst) == ['big.slow', 'small.txt'], entries(dst)
print('copy: progress window shown, pause holds the copy, resume completes it')

# cancel removes the part-copied item and skips the rest
src, dst = dirs('copy-cancel')
shown, log = run('copy-cancel', 'copy', 'big.slow,small.txt', 'cancel', src, dst, dst/'big.slow')
assert shown.startswith('window 1'), (shown, log)
assert entries(dst) == [], entries(dst)
assert (src/'big.slow').read_bytes() == BIG and (src/'small.txt').exists()
print('copy: cancel leaves no partial item, and stops the task')

# cancelling a merge keeps the existing destination and what was added to it
src, dst = dirs('merge-cancel')
(src/'dir').mkdir()
shutil.move(str(src/'big.slow'), str(src/'dir'/'big.slow'))
(dst/'dir').mkdir()
(dst/'dir'/'old.txt').write_text('old')
shown, log = run('merge-cancel', 'copy_merge', 'dir', 'cancel', src, dst, dst/'dir'/'big.slow')
assert shown.startswith('window 1'), (shown, log)
assert (dst/'dir'/'old.txt').read_text() == 'old'
assert entries(dst/'dir') == ['old.txt'], entries(dst/'dir')
print('copy merge: cancel keeps the existing folder, without a partial item')

# quick tasks show no window, and the former plugin action still works
src, dst = dirs('quick')
shown, log = run('quick', 'cpbar', 'small.txt', 'none', src, dst, dst/'small.txt')
assert shown == 'window 0 paused 0\n', (shown, log)
assert (dst/'small.txt').read_text() == 'small'
print('quick copy: no progress window, file.cpbar runs the built-in copy')

# between filesystems a move copies, so it shows progress, and cancelling it
# leaves the source untouched
shm = pathlib.Path('/dev/shm')
if shm.is_dir() and shm.stat().st_dev != root.stat().st_dev:
    for request in ('cancel', 'none'):
        name = 'move-devices-' + request
        srcbase = pathlib.Path(tempfile.mkdtemp(dir=str(shm), prefix='emelfm2-progress-'))
        try:
            srcbase.rmdir()
            src, dst = dirs(name, srcbase)
            shown, log = run(name, 'move', 'big.slow', request, src, dst, dst/'big.slow')
            assert shown.startswith('window 1'), (shown, log)
            if request == 'cancel':
                assert entries(dst) == [], entries(dst)
                assert (src/'big.slow').read_bytes() == BIG
            else:
                assert (dst/'big.slow').read_bytes() == BIG and not (src/'big.slow').exists()
        finally:
            shutil.rmtree(str(srcbase), ignore_errors=True)
    print('move between filesystems: progress window, cancel keeps the source intact')
else:
    print('move between filesystems: skipped, no separate /dev/shm filesystem')
PY
