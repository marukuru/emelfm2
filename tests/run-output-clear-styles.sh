#!/bin/sh
# Clearing the command log during ANSI-styled command output.
set -eu
cd "$(dirname "$0")/.."
test_dir=$(mktemp -d "${TMPDIR:-/tmp}/emelfm2-output-clear.XXXXXX")
trap 'rm -rf "$test_dir"' EXIT HUP INT TERM
gtk_package=${GTK_PACKAGE:-gtk+-2.0}
${CC:-cc} -shared -fPIC -Wno-deprecated-declarations -fcommon -D_FILE_OFFSET_BITS=64 \
    -Isrc -Isrc/actions -Isrc/build -Isrc/command -Isrc/command/complete \
    -Isrc/config -Isrc/dialogs -Isrc/filesystem -Isrc/utils \
    $(pkg-config --cflags "$gtk_package") tests/output-clear-styles.c \
    $(pkg-config --libs "$gtk_package") -o "$test_dir/test.so"
cat > "$test_dir/styled.sh" <<'SH'
#!/bin/sh
i=0
while [ $i -lt 60 ]; do
    printf '\033[1;31mred %d\033[0m plain\n' $i
    [ $i = 10 ] && : > "$1/started"
    i=$((i+1))
    sleep 0.05
done
: > "$1/finished"
SH
chmod +x "$test_dir/styled.sh"
export E2_OUTPUT_CLEAR_TEST="$test_dir"
LC_ALL=C.UTF-8 NO_AT_BRIDGE=1 GIO_USE_VFS=local G_SLICE=always-malloc dbus-run-session -- xvfb-run -a python3 - <<'PY'
import os, pathlib, subprocess
root = pathlib.Path(os.environ['E2_OUTPUT_CLEAR_TEST'])
env = dict(os.environ, LD_PRELOAD=str(root/'test.so'))
result = subprocess.run(['./emelfm2', '-c', str(root/'config'), '-1', str(root), '-2', str(root),
    '-s', 'single-instance=false', '-s', 'session-end-warning=false'],
    env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=60)
log = result.stdout.decode(errors='replace')
assert (root/'passed').exists(), log
assert result.returncode == 0, log
assert 'CRITICAL' not in log and 'AddressSanitizer' not in log, log
print('output clear: styled command output continues safely after clearing the log')
PY
