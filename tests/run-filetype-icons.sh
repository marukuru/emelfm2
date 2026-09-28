#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
test_dir=$(mktemp -d "${TMPDIR:-/tmp}/emelfm2-filetype-icons.XXXXXX")
trap 'rm -rf "$test_dir"' EXIT HUP INT TERM
gtk_package=${GTK_PACKAGE:-gtk+-2.0}
${CC:-cc} -shared -fPIC -Wno-deprecated-declarations -fcommon -D_FILE_OFFSET_BITS=64 \
    -Isrc -Isrc/actions -Isrc/build -Isrc/command -Isrc/command/complete \
    -Isrc/config -Isrc/dialogs -Isrc/filesystem -Isrc/utils \
    $(pkg-config --cflags "$gtk_package") tests/filetype-icons.c \
    $(pkg-config --libs "$gtk_package") -o "$test_dir/test.so"
export E2_FILETYPE_ICON_TEST="$test_dir"
LC_ALL=C.UTF-8 GDK_BACKEND=x11 NO_AT_BRIDGE=1 GIO_USE_VFS=local dbus-run-session -- xvfb-run -a python3 - <<'PY'
import os, pathlib, subprocess
root = pathlib.Path(os.environ['E2_FILETYPE_ICON_TEST'])
(root/'pane').mkdir()
(root/'pane'/'a.iconprobe').write_text('Filetype icon fixture\n')
(root/'external icons').mkdir()
# Use a bundled icon's basename to catch both missing images and wrong replacements.
(root/'external icons'/'move.svg').write_text(
    '<svg xmlns="http://www.w3.org/2000/svg" width="32" height="20">'
    '<rect width="32" height="20" fill="#ef1234"/>'
    '<rect x="16" width="16" height="20" fill="#1234ef"/></svg>')
for modern in ('false', 'true'):
    args = ['./emelfm2', '-c', str(root/('config-'+modern)), '-1', str(root/'pane'), '-2', str(root/'pane'),
            '-s', 'session-end-warning=false', '-s', 'single-instance=false', '-s', 'modern-ui='+modern]
    for reload in (False, True):
        env = dict(os.environ, LD_PRELOAD=str(root/'test.so'))
        if reload: env['E2_FILETYPE_ICON_RELOAD'] = '1'
        run = subprocess.run(args, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=25)
        log = run.stdout.decode(errors='replace')
        assert run.returncode == 0 and 'CRITICAL' not in log and 'WARNING' not in log, log
        expected = 'filetype icon reload passed' if reload else 'filetype icon picker, removal, compatibility, menus and reorder passed'
        assert expected in log, log
        print('modern-ui='+modern, expected)
PY
