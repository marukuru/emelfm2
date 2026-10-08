#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
test_dir=$(mktemp -d "${TMPDIR:-/tmp}/emelfm2-command-quoting-ui.XXXXXX")
trap 'rm -rf "$test_dir"' EXIT HUP INT TERM
gtk_package=${GTK_PACKAGE:-gtk+-2.0}
${CC:-cc} -shared -fPIC -Wno-deprecated-declarations -fcommon -D_FILE_OFFSET_BITS=64 \
    -Isrc -Isrc/actions -Isrc/build -Isrc/command -Isrc/command/complete \
    -Isrc/config -Isrc/dialogs -Isrc/filesystem -Isrc/utils \
    $(pkg-config --cflags "$gtk_package") tests/command-quoting-ui.c \
    $(pkg-config --libs "$gtk_package") -o "$test_dir/test.so"
export E2_COMMAND_QUOTING_TEST="$test_dir"
LC_ALL=C.UTF-8 NO_AT_BRIDGE=1 GIO_USE_VFS=local dbus-run-session -- xvfb-run -a python3 - <<'PY'
import os, pathlib, shlex, subprocess
root = pathlib.Path(os.environ['E2_COMMAND_QUOTING_TEST'])
(root/'capture.py').write_text('''import os, pathlib, sys
args = [os.environ['E2_QUOTED_VALUE']] if sys.argv[2:] == ['--env'] else sys.argv[2:]
pathlib.Path(sys.argv[1]).write_bytes(b''.join(os.fsencode(arg) + b'\\0' for arg in args))
print('captured arguments', flush=True)
''')
# Match the plugin's syntax, including octal escapes for native filename bytes.
def quote(data):
    if isinstance(data, str): data = data.encode()
    return "$'" + ''.join({39:r"\'", 92:r'\\', 9:r'\t', 10:r'\n', 13:r'\r'}.get(
        byte, chr(byte) if 32 <= byte < 127 else f'\\{byte:03o}') for byte in data) + "'"
paths = [str(root).encode() + b'/' + name for name in [
    b'ordinary space', b"apostrophe's", b'back\\slash\\', b'"double" ; & | <>',
    b'$HOME ${HOME} $$ $[dialog-view-width] $(printf INJECTED) `printf INJECTED`',
    b'%f %p %d %c %t', b'tab\tline\ncarriage\r', b'\x01\x7f\xff',
    '日本語'.encode(), b'*? [pattern]']]
cases = []
def add(args, expected, prefix='', suffix='', kind='capture'):
    index = len(cases)
    base = 'python3 ' + shlex.quote(str(root/'capture.py')) + ' ' + shlex.quote(str(root/f'{index}.actual'))
    cases.append((prefix + base + ' ' + args + suffix, kind, b''.join(arg+b'\0' for arg in expected)))
args = ' '.join(map(quote, paths))
add(args, paths)
add(args, paths, prefix='>')
add(args, paths, suffix=' | cat')
add(args, paths, suffix=' > ' + quote(str(root/'redirect ; \'$%d')))
add(args, paths, prefix='|')
add('\n'.join(map(quote, paths)), paths)
add("--file=" + quote(paths[1]) + " prefix$'\\t'suffix $''", [b'--file='+paths[1], b'prefix\tsuffix', b''])
add(r"$'\x41\101\u65e5\U0001f600' $'a\0ignored'b", ['AA日😀'.encode(), b'ab'])
add(quote(b'literal $HOME %d *.txt') + ' ${E2_QUOTING_VALUE} %d wild*.txt',
    [b'literal $HOME %d *.txt', b'expanded-value', str(root).encode(), b'wild-one.txt'])
(root/'wild-one.txt').touch()
add(args, paths, prefix="|true; ")
add('--env', [paths[4]], prefix='set E2_QUOTED_VALUE ' + quote(paths[4]) + '; ')
cases.append(("set E2_QUOTED_VALUE $'valid'\"unclosed", 'error', b'Invalid quoting in variable value'))
add('--env', [paths[4]])
add('$E2_QUOTED_INTERNAL', [paths[5]], prefix='E2_QUOTED_INTERNAL=' + quote(paths[5]) + '; ')
add("$'unterminated", [], kind='error')
cases[-1] = (cases[-1][0], 'error', b'Unterminated ANSI-C quoted string')
# Both quoted executable names and literal cd must use the same parser.
index = len(cases)
command = quote('/usr/bin/python3') + ' ' + shlex.quote(str(root/'capture.py')) + ' ' + shlex.quote(str(root/f'{index}.actual')) + ' ' + args
cases.append((command, 'capture', b''.join(p+b'\0' for p in paths)))
directory = root/"directory ' \\ $HOME %d ; & 日本語"
directory.mkdir()
cases.append(('cd ' + quote(str(directory)), 'cd', (str(directory)+'/').encode()))
cases.append(('cd ' + quote(str(root)), 'cd', (str(root)+'/').encode()))
for index, (command, kind, expected) in enumerate(cases):
    (root/f'{index}.command').write_text(command)
    (root/f'{index}.kind').write_text(kind)
    (root/f'{index}.expected').write_bytes(expected)
result = subprocess.run([os.environ.get('E2_COMMAND_QUOTING_BINARY', './emelfm2'),
    '-c', str(root/'config'), '-1', str(root), '-s', 'session-end-warning=false', '-s', 'single-instance=false'],
    env=dict(os.environ, SHELL='/bin/sh', LD_PRELOAD=str(root/'test.so'), E2_QUOTING_VALUE='expanded-value'),
    capture_output=True, timeout=60)
log = (result.stdout+result.stderr).decode(errors='replace')
assert result.returncode == 0 and (root/'passed').exists(), log
assert 'CRITICAL' not in log and 'WARNING' not in log, log
print('command entry and Commandlog: direct/shell/sync commands, pipes, redirection, multiline paths, macros, raw bytes, errors and cd passed')
PY
