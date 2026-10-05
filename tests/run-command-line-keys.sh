#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
test_dir=$(mktemp -d "${TMPDIR:-/tmp}/emelfm2-command-line-keys.XXXXXX")
trap 'rm -rf "$test_dir"' EXIT HUP INT TERM
gtk_package=${GTK_PACKAGE:-gtk+-2.0}
${CC:-cc} -shared -fPIC -Wno-deprecated-declarations -fcommon -D_FILE_OFFSET_BITS=64 \
    -Isrc -Isrc/actions -Isrc/build -Isrc/command -Isrc/command/complete \
    -Isrc/config -Isrc/dialogs -Isrc/filesystem -Isrc/utils \
    $(pkg-config --cflags "$gtk_package") tests/command-line-keys.c \
    $(pkg-config --libs "$gtk_package") -o "$test_dir/test.so"
export E2_COMMAND_KEYS_TEST="$test_dir"
LC_ALL=C.UTF-8 GDK_BACKEND=x11 NO_AT_BRIDGE=1 GIO_USE_VFS=local dbus-run-session -- xvfb-run -a python3 - <<'PY'
import os, pathlib, subprocess
root = pathlib.Path(os.environ['E2_COMMAND_KEYS_TEST'])
(root/'source'/'folder-name').mkdir(parents=True)
(root/'destination').mkdir()
(root/'bin').mkdir()
probe = root/'bin'/'e2-completion-probe'
probe.write_text('#!/bin/sh\nexit 0\n')
probe.chmod(0o755)
for name in ('unique-file.txt', 'spaced file.txt'):
    (root/'source'/name).touch()
    (root/'destination'/name).touch()

def run(case):
    env = dict(os.environ, LD_PRELOAD=str(root/'test.so'), E2_COMMAND_KEYS_CASE=case,
               PATH=str(root/'bin')+os.pathsep+os.environ['PATH'])
    result = subprocess.run(['./emelfm2', '-c', str(root/'config'),
        '-1', str(root/'source'), '-2', str(root/'destination'),
        '-s', 'single-instance=false', '-s', 'session-end-warning=false'],
        env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=30)
    log = result.stdout.decode(errors='replace')
    assert result.returncode == 0 and 'CRITICAL' not in log and 'WARNING' not in log, log
    assert 'command-line completion and Escape navigation toggle passed' in log, log
    print(case, 'passed')

run('default bindings')
# An existing profile may lack completion or bind Tab globally to another action.
config = next((root/'config').glob('config-*'))
saved = config.read_text()
assert '\t\t\t|Tab|false|command.complete|\n' in saved
saved = saved.replace('\t\t\t|Tab|false|command.complete|\n', '')
config.write_text(saved)
run('missing command completion binding')
lines = saved.splitlines()
for index, line in enumerate(lines):
    if line.strip().split('|')[0] == 'main':
        lines.insert(index+1, '\t\t|Tab||panes.switch|')
        break
else:
    raise AssertionError('Missing main keybinding category')
config.write_text('\n'.join(lines)+'\n')
run('existing profile with global Tab binding')
PY
