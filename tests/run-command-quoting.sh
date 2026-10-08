#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
test_dir=$(mktemp -d "${TMPDIR:-/tmp}/emelfm2-command-quoting.XXXXXX")
trap 'rm -rf "$test_dir"' EXIT HUP INT TERM
gtk_package=${GTK_PACKAGE:-gtk+-2.0}
${CC:-cc} -Wall -Wno-deprecated-declarations -fcommon -ffunction-sections -fdata-sections \
    -D_FILE_OFFSET_BITS=64 -Isrc -Isrc/actions -Isrc/build -Isrc/command \
    -Isrc/command/complete -Isrc/config -Isrc/dialogs -Isrc/filesystem -Isrc/utils \
    $(pkg-config --cflags "$gtk_package") tests/command-quoting.c \
    src/command/e2_command_quoting.c src/utils/e2_utils.c \
    -Wl,--gc-sections $(pkg-config --libs "$gtk_package") -lm -o "$test_dir/parse"
LC_ALL=C.UTF-8 python3 - "$test_dir/parse" <<'PY'
import os, subprocess, sys
parser = sys.argv[1]
env = dict(os.environ)
env.pop('BASH_ENV', None)
cases = [
    r"$'/tmp/ordinary file.txt'", r"$'/tmp/apostrophe\'s.txt'",
    r'''$'/tmp/"quotes" ; & | < > `printf INJECTED` $(printf INJECTED) $HOME $$ ${HOME} $[dialog-view-width] %f %p %d' '' "ordinary"''',
    r"$'back\\slash\\' $'tab\tline\ncarriage\r' $'\a\b\e\E\f\v'",
    r"$'\001\177\200\377' $'\x01\xff' $'\1017\x414\400discard'",
    r"$'日本語' $'\u65e5\u672c\u8a9e' $'\U0001f600'",
    r"$'\cA\c?\cZ' $'\"\?\q\x\u\U'",
    r"$'before\0after'suffix $'' --file=$'space and \'quotes' prefix$'\t'suffix",
    r'''"$'literal\n'" '$'"'literal'" \$'literal\t' 'ordinary \' "double ' quote"''',
    "$'one'\n$'two'\t$'three'",
]
# Every possible non-NUL byte emitted by the copy-path plugin.
cases += ["$'" + ''.join(f'\\{byte:03o}' for byte in range(1, 256)) + "'"]
for case in cases:
    expected = subprocess.run(['bash', '--noprofile', '--norc', '-c', "words=(\n" + case + "\n)\nprintf '%s\\0' \"${words[@]}\""],
        env=env, check=True, capture_output=True).stdout
    actual = subprocess.run([parser, 'direct', case], check=True, capture_output=True).stdout
    assert actual == expected, (case, actual, expected)
    if '\n' in case: continue
    normalized = subprocess.run([parser, 'shell', case], check=True, capture_output=True).stdout
    # The shell route must also work when /bin/sh has no ANSI-C quote support.
    actual = subprocess.run([b'/bin/sh', b'-c', b"printf '%s\\0' " + normalized],
        env=env, check=True, capture_output=True).stdout
    assert actual == expected, (case, normalized, actual, expected)
for case in ("$'unclosed", "command $'trailing\\", "$'\\uD800'"):
    result = subprocess.run([parser, 'direct', case], capture_output=True)
    assert result.returncode == 1 and result.stderr, (case, result)
for case in [r'''cmd $'one\' ; "two" \\' ; next''',
             r'''cmd 'one\' ; next''', r'''cmd "one ' two" ; next''',
             r'''cmd \\; next''', r'''cmd $'semi;colon' ; next''']:
    index = int(subprocess.run([parser, 'separator', case], check=True, capture_output=True).stdout)
    assert index == case.index('; next'), (case, index)
print('ANSI-C quoting: Bash byte round trips, portable shell output, literal syntax, errors and separators passed')
PY
