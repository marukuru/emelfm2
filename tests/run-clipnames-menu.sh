#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
test_dir=$(mktemp -d "${TMPDIR:-/tmp}/emelfm2-clipnames-menu.XXXXXX")
trap 'rm -rf "$test_dir"' EXIT HUP INT TERM
gtk_package=${GTK_PACKAGE:-gtk+-2.0}
${CC:-cc} -shared -fPIC -Wno-deprecated-declarations -fcommon -D_FILE_OFFSET_BITS=64 \
    -Isrc -Isrc/actions -Isrc/build -Isrc/command -Isrc/command/complete \
    -Isrc/config -Isrc/dialogs -Isrc/filesystem -Isrc/utils \
    $(pkg-config --cflags "$gtk_package") tests/clipnames-menu.c \
    $(pkg-config --libs "$gtk_package") -o "$test_dir/test.so"
export E2_CLIPNAMES_TEST="$test_dir"
export E2_CLIPNAMES_PLUGIN_DIR="$PWD/objs/plugins"
LC_ALL=C.UTF-8 GDK_BACKEND=x11 NO_AT_BRIDGE=1 GIO_USE_VFS=local dbus-run-session -- xvfb-run -a python3 - <<'PY'
import os, pathlib, subprocess
root = pathlib.Path(os.environ['E2_CLIPNAMES_TEST'])
parent = root/"parent ' with spaces"
shell_names = [b'q0 space.txt', b'q1 \'single\' "double".txt',
    b'q2 $HOME $(printf INJECTED) `printf INJECTED`.txt', b'q3 !*?[x] {a,b}; &|<>.txt',
    b'q4 \\backslash\\n literal.txt', 'q5 日本語.txt'.encode(),
    b'q6-tab\tline\ncarriage\r.txt', b'q7-control\x01\x1b\x7f.txt', b'q8-invalid-\xff.txt']
(root/'shell-names').write_bytes(b'\0'.join(shell_names)+b'\0')
for pane in ('left', 'right'):
    (parent/pane/'folder name').mkdir(parents=True)
    for name in shell_names:
        with open(os.fsencode(parent/pane)+b'/'+name, 'wb'):
            pass
    for name in ('a file.txt', 'b-日本語.txt'):
        (parent/pane/name).touch()

def run(case):
    env = dict(os.environ, LD_PRELOAD=str(root/'test.so'), E2_CLIPNAMES_CASE=case)
    result = subprocess.run(['./emelfm2', '-c', str(root/'config'),
        '-1', str(parent/'left'), '-2', str(parent/'right'),
        '-s', 'single-instance=false', '-s', 'session-end-warning=false'],
        env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=20)
    log = result.stdout.decode(errors='replace')
    assert result.returncode == 0 and 'CRITICAL' not in log and 'WARNING' not in log, log
    assert 'clipnames menu checks passed' in log, log
    if case == 'enabled':
        # Evaluate the actual clipboard text as Bash words, then compare bytes.
        # The controlled fixtures contain harmless substitution probes only.
        bash_env = dict(os.environ)
        bash_env.pop('BASH_ENV', None)
        for step in range(6, 10):
            text = (root/f'bash-{step}').read_text()
            expected = (root/f'bash-{step}.expected').read_bytes()
            count = expected.count(b'\0')
            assert text.count('\n') == (count-1 if step % 2 else 0), repr(text)
            code = "paths=(\n"+text+"\n)\nprintf '%s\\0' \"${paths[@]}\""
            result = subprocess.run(['bash', '--noprofile', '--norc', '-c', code],
                cwd=root, env=bash_env, capture_output=True, timeout=5)
            assert result.returncode == 0, result.stderr
            assert result.stdout == expected, (text, result.stdout, expected)
    print(case, 'passed')

run('setup')
config = next((root/'config').glob('config-*'))
saved = config.read_text()
for case, loaded, visible in (('enabled', True, True), ('hidden', True, False),
                             ('unloaded', False, True), ('disabled', False, False),
                             ('enabled', True, True)):
    lines = saved.splitlines()
    for i, line in enumerate(lines):
        if '|e2p_clipnames.so|' in line:
            fields = line.split('|')
            fields[0] = str(loaded).lower()
            fields[1] = str(visible).lower()
            lines[i] = '|'.join(fields)
            break
    else:
        raise AssertionError('Copy names plugin configuration missing')
    config.write_text('\n'.join(lines)+'\n')
    run(case)
PY
