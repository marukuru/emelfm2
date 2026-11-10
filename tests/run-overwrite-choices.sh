#!/bin/sh
# Overwrite dialog: Keep all and Merge choices for copy and move.
set -eu
cd "$(dirname "$0")/.."
test_dir=$(mktemp -d "${TMPDIR:-/tmp}/emelfm2-overwrite.XXXXXX")
trap 'rm -rf "$test_dir"' EXIT HUP INT TERM
gtk_package=${GTK_PACKAGE:-gtk+-2.0}
${CC:-cc} -shared -fPIC -Wno-deprecated-declarations -fcommon -D_FILE_OFFSET_BITS=64 \
    -Isrc -Isrc/actions -Isrc/build -Isrc/command -Isrc/command/complete \
    -Isrc/config -Isrc/dialogs -Isrc/filesystem -Isrc/utils \
    $(pkg-config --cflags "$gtk_package") tests/overwrite-choices.c \
    $(pkg-config --libs "$gtk_package") -ldl -o "$test_dir/test.so"
export E2_OVERWRITE_TEST="$test_dir"
LC_ALL=C.UTF-8 NO_AT_BRIDGE=1 GIO_USE_VFS=local dbus-run-session -- xvfb-run -a python3 - <<'PY'
import os, pathlib, shutil, subprocess, tempfile
root = pathlib.Path(os.environ['E2_OVERWRITE_TEST'])
ALL = 'a.txt,b.txt,dir,extra.txt,full'

def write(path, text):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text)

def fixture(base):
    src, dst, outside = base/'src', base/'dst', base/'outside'
    for name in ('a', 'b', 'extra'):
        write(src/(name+'.txt'), 'new '+name)
    for name in ('x', 'y'):
        write(src/'dir'/(name+'.txt'), 'new '+name)
    for name in ('z', 'w'):
        write(src/'dir'/'sub'/(name+'.txt'), 'new '+name)
    write(src/'dir'/'linked'/'q.txt', 'new q')
    (src/'dir'/'empty-new').mkdir()
    write(src/'full'/'n.txt', 'new n')
    for name in ('a', 'b'):
        write(dst/(name+'.txt'), 'old '+name)
    write(dst/'dir'/'x.txt', 'old x')
    write(dst/'dir'/'sub'/'z.txt', 'old z')
    write(dst/'full'/'other.txt', 'old other')
    write(outside/'keep.txt', 'outside')
    # an existing link to a dir is kept, never merged through
    (dst/'dir'/'linked').symlink_to('../../outside')
    return src, dst, outside

def run(name, action, select, response, keepall, merge, extra={}, srcbase=None,
        fixture_extra=None):
    base = root/name
    src, dst, outside = fixture(base)
    if fixture_extra is not None:
        fixture_extra(src)
    if srcbase is not None:
        # put the source on another filesystem
        shutil.move(str(src), str(srcbase))
        src = srcbase
    result = base/'result'
    env = dict(os.environ, LD_PRELOAD=str(root/'test.so'), E2_OW_ACTION=action,
               E2_OW_SELECT=select, E2_OW_RESPONSE=response, E2_OW_KEEPALL=str(keepall),
               E2_OW_MERGE=str(merge), E2_OW_RESULT=str(result), **extra)
    process = subprocess.run(['./emelfm2', '-c', str(base/'config'), '-1', str(src), '-2', str(dst),
        '-s', 'single-instance=false', '-s', 'session-end-warning=false',
        '-s', 'confirm-overwrite=true'],
        env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=60)
    log = process.stdout.decode(errors='replace')
    assert process.returncode == 0 and result.exists(), (name, log)
    assert 'CRITICAL' not in log, (name, log)
    assert result.read_text() == 'dialogs 1\n', (name, result.read_text(), log)
    return src, dst, outside

def text(path):
    return path.read_text() if path.exists() else None

def tree(base):
    # every entry below base, without following links
    found = set()
    for top, dirs, files in os.walk(base):
        for name in dirs + files:
            found.add(str((pathlib.Path(top)/name).relative_to(base)))
    return found

def kept_existing(dst):
    assert text(dst/'a.txt') == 'old a' and text(dst/'b.txt') == 'old b'
    assert text(dst/'dir'/'x.txt') == 'old x' and text(dst/'dir'/'sub'/'z.txt') == 'old z'
    assert text(dst/'full'/'other.txt') == 'old other'

def merged(dst, outside):
    kept_existing(dst)
    assert text(dst/'extra.txt') == 'new extra'
    assert text(dst/'dir'/'y.txt') == 'new y' and text(dst/'dir'/'sub'/'w.txt') == 'new w'
    assert (dst/'dir'/'empty-new').is_dir()
    assert text(dst/'full'/'n.txt') == 'new n'
    assert (dst/'dir'/'linked').is_symlink() and not (outside/'q.txt').exists()
    # nothing else, such as a temporary copy, is left behind
    assert tree(dst) == {'a.txt', 'b.txt', 'extra.txt', 'dir', 'dir/x.txt', 'dir/y.txt',
        'dir/sub', 'dir/sub/z.txt', 'dir/sub/w.txt', 'dir/linked', 'dir/empty-new',
        'full', 'full/other.txt', 'full/n.txt'}, tree(dst)

def source_intact(src):
    assert text(src/'a.txt') == 'new a' and text(src/'extra.txt') == 'new extra'
    assert text(src/'dir'/'y.txt') == 'new y' and text(src/'dir'/'sub'/'w.txt') == 'new w'
    assert text(src/'full'/'n.txt') == 'new n'

# Keep all: no existing item changes and no folder is entered
src, dst, outside = run('copy-keepall', 'copy', ALL, 'keepall', 1, 1)
kept_existing(dst)
assert text(dst/'extra.txt') == 'new extra'
assert not (dst/'dir'/'y.txt').exists() and not (dst/'full'/'n.txt').exists()
source_intact(src)
print('copy, Keep all: existing items kept, new items copied, without further questions')

# Merge: existing folders get only their missing items, at every depth
src, dst, outside = run('copy-merge', 'copy', ALL, 'merge', 1, 1)
merged(dst, outside)
source_intact(src)
print('copy, Merge: only missing items copied into existing folders')

src, dst, outside = run('move-merge', 'move', ALL, 'merge', 1, 1)
merged(dst, outside)
# kept items stay in the source, and only emptied folders are removed
assert text(src/'a.txt') == 'new a' and text(src/'b.txt') == 'new b'
assert not (src/'extra.txt').exists()
assert text(src/'dir'/'x.txt') == 'new x' and text(src/'dir'/'sub'/'z.txt') == 'new z'
assert text(src/'dir'/'linked'/'q.txt') == 'new q'
assert not (src/'dir'/'y.txt').exists() and not (src/'dir'/'sub'/'w.txt').exists()
assert not (src/'dir'/'empty-new').exists() and not (src/'full').exists()
print('move, Merge: missing items moved, kept items left in emptied-out source folders')

src, dst, outside = run('move-keepall', 'move', ALL, 'keepall', 1, 1)
kept_existing(dst)
assert text(dst/'extra.txt') == 'new extra' and not (src/'extra.txt').exists()
assert not (dst/'dir'/'y.txt').exists() and text(src/'dir'/'y.txt') == 'new y'
assert text(src/'full'/'n.txt') == 'new n' and not (dst/'full'/'n.txt').exists()
print('move, Keep all: existing items kept in both places, new items moved')

# One item: Keep all has nothing more to apply to, Merge only suits two folders
src, dst, outside = run('copy-single-dir', 'copy', 'dir', 'merge', 0, 1)
assert text(dst/'dir'/'x.txt') == 'old x' and text(dst/'dir'/'y.txt') == 'new y'
assert text(dst/'dir'/'sub'/'w.txt') == 'new w' and not (outside/'q.txt').exists()
src, dst, outside = run('copy-single-file', 'copy', 'a.txt', 'keep', 0, 0)
assert text(dst/'a.txt') == 'old a'
print('single items: Keep all and Merge offered only where they apply')

# Between filesystems, a move copies and then deletes the source. When some of
# the source cannot be deleted, the complete copy must remain at the destination.
shm = pathlib.Path('/dev/shm')
if shm.is_dir() and shm.stat().st_dev != root.stat().st_dev:
    srcbase = pathlib.Path(tempfile.mkdtemp(dir=str(shm), prefix='emelfm2-overwrite-'))
    try:
        srcbase.rmdir()
        src, dst, outside = run('move-merge-devices', 'move', 'dir', 'merge', 0, 1,
            {'E2_OW_UNDELETABLE': '.undeletable'}, srcbase,
            fixture_extra=lambda src: (write(src/'dir'/'newdir'/'one.txt', 'new one'),
                                       write(src/'dir'/'newdir'/'two.undeletable', 'new two')))
        assert text(dst/'dir'/'newdir'/'one.txt') == 'new one', tree(dst)
        assert text(dst/'dir'/'newdir'/'two.undeletable') == 'new two', tree(dst)
        assert text(dst/'dir'/'y.txt') == 'new y' and text(dst/'dir'/'x.txt') == 'old x'
        # the undeletable source item stays, the rest of its dir has gone
        assert text(src/'dir'/'newdir'/'two.undeletable') == 'new two'
        assert not (src/'dir'/'newdir'/'one.txt').exists() and not (src/'dir'/'y.txt').exists()
        print('move between filesystems, Merge: the copy is kept when its source cannot be fully deleted')
    finally:
        shutil.rmtree(str(srcbase), ignore_errors=True)
else:
    print('move between filesystems: skipped, no separate /dev/shm filesystem')
PY
