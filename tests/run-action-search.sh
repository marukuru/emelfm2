#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
test_dir=$(mktemp -d "${TMPDIR:-/tmp}/emelfm2-action-search.XXXXXX")
trap 'rm -rf "$test_dir"' EXIT HUP INT TERM
gtk_package=${GTK_PACKAGE:-gtk+-3.0}
${CC:-cc} -shared -fPIC -Wno-deprecated-declarations -fcommon -D_FILE_OFFSET_BITS=64 \
    -Isrc -Isrc/actions -Isrc/build -Isrc/command -Isrc/command/complete \
    -Isrc/config -Isrc/dialogs -Isrc/filesystem -Isrc/utils \
    $(pkg-config --cflags "$gtk_package") tests/action-search.c \
    $(pkg-config --libs "$gtk_package") -o "$test_dir/test.so"
export E2_SEARCH_TEST="$test_dir"
LC_ALL=C.UTF-8 NO_AT_BRIDGE=1 GIO_USE_VFS=local dbus-run-session -- xvfb-run -a python3 - <<'PY'
import os, pathlib, subprocess, time
root = pathlib.Path(os.environ['E2_SEARCH_TEST'])
(root/'source').mkdir()
(root/'destination').mkdir()
(root/'source'/'one.txt').write_text('Action search copy test 日本語\n')

def run(scenario=None):
    (root/'passed').unlink(missing_ok=True)
    env = dict(os.environ, LD_PRELOAD=str(root/'test.so'))
    if scenario:
        env['E2_SEARCH_CASE'] = scenario
    with open(root/'log', 'w') as log:
        p = subprocess.Popen(['./emelfm2', '-c', str(root/'config'),
            '-1', str(root/'source'), '-2', str(root/'destination'),
            '-s', 'session-end-warning=false'], env=env, stdout=log, stderr=log)
        try:
            deadline = time.monotonic()+30
            while not (root/'passed').exists() and p.poll() is None and time.monotonic()<deadline:
                time.sleep(.05)
            assert (root/'passed').exists(), (root/'log').read_text()
            assert p.wait(timeout=10) == 0, (root/'log').read_text()
            output = (root/'log').read_text()
            assert 'CRITICAL' not in output and 'WARNING' not in output, output
        finally:
            if p.poll() is None:
                p.kill()
                p.wait(timeout=10)

run()
assert (root/'destination'/'one.txt').read_bytes() == (root/'source'/'one.txt').read_bytes()
config = next((root/'config').glob('config-*'))
saved = config.read_text()
# Explicitly removing the new binding remains effective after restart.
removed = '\n'.join(line for line in saved.split('\n') if '|actions.search|' not in line)
config.write_text(removed)
run('removed')
# An old profile with an unused shortcut receives the new binding.
upgrade = removed.replace('action-search-binding-added=true', 'action-search-binding-added=false')
assert upgrade != removed
config.write_text(upgrade)
run('upgraded')
assert '|actions.search|' in config.read_text()
# An old profile's conflicting shortcut is preserved during the one-time upgrade.
conflict = upgrade
lines = conflict.splitlines()
for index, line in enumerate(lines):
    if line.strip().split('|')[0] == 'main':
        lines.insert(index+1, '\t\t|<Control><Shift>a||panes.switch|')
        break
else:
    raise AssertionError('Missing main keybinding category: ' + conflict[conflict.index('keybindings=<'):][:300])
conflict = '\n'.join(lines) + '\n'
config.write_text(conflict)
run('conflict')
print('action search: shortcut, copy dispatch, filtering, navigation, cancellation, availability, aliases, Unicode, effective bindings, registry updates and profile migration passed')
PY
