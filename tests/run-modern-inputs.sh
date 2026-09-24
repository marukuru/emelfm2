#!/bin/sh
# Check real focused-entry/text-area pixels, including GTK 2 state-hint themes.
set -eu
cd "$(dirname "$0")/.."
test_dir=$(mktemp -d "${TMPDIR:-/tmp}/emelfm2-modern-inputs.XXXXXX")
trap 'rm -rf "$test_dir"' EXIT HUP INT TERM
gtk_package=${GTK_PACKAGE:-gtk+-2.0}
${CC:-cc} -shared -fPIC -Wno-deprecated-declarations -fcommon -D_FILE_OFFSET_BITS=64 \
    -Isrc -Isrc/actions -Isrc/build -Isrc/command -Isrc/command/complete \
    -Isrc/config -Isrc/dialogs -Isrc/filesystem -Isrc/utils \
    $(pkg-config --cflags "$gtk_package") tests/modern-inputs.c \
    $(pkg-config --libs "$gtk_package") -o "$test_dir/test.so"
export E2_INPUT_TEST_DIR="$test_dir"
LC_ALL=C.UTF-8 GDK_BACKEND=x11 NO_AT_BRIDGE=1 GIO_USE_VFS=local dbus-run-session -- xvfb-run -a python3 - <<'PY'
import os, pathlib, subprocess
root = pathlib.Path(os.environ['E2_INPUT_TEST_DIR'])
(root/'pane').mkdir()
for theme in os.environ.get('E2_MODERN_TEST_THEMES', 'Adwaita,Adwaita-dark').split(','):
    rc = root/'gtkrc'
    theme_rc = pathlib.Path('/usr/share/themes')/theme/'gtk-2.0/gtkrc'
    if not theme_rc.exists(): theme_rc = pathlib.Path.home()/'.themes'/theme/'gtk-2.0/gtkrc'
    # Exercise state-hint even on desktops whose installed theme disables it.
    rc.write_text(f'include "{theme_rc}"\ngtk-font-name="DejaVu Sans 10"\n'
                  'style "e2-focus-test" { GtkEntry::state-hint = 1 base[ACTIVE] = "#2eb398" }\n'
                  'widget "*e2-focus-test-input" style "e2-focus-test"\n')
    for mode in ('false', 'true'):
        case = theme+'-'+mode
        config = root/case
        env = dict(os.environ, LD_PRELOAD=str(root/'test.so'), GTK_THEME=theme,
                   GTK2_RC_FILES=str(rc), E2_INPUT_TEST_CASE=case)
        run = subprocess.run(['./emelfm2', '-c', str(config), '-1', str(root/'pane'), '-2', str(root/'pane'),
                '-s', 'single-instance=false', '-s', 'session-end-warning=false', '-s', 'modern-ui='+mode],
                env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=20)
        log = run.stdout.decode(errors='replace')
        assert run.returncode == 0 and 'CRITICAL' not in log and 'WARNING' not in log, case+'\n'+log[-10000:]
        assert 'input backgrounds, focus borders and text selections passed' in log, log
        print(theme, 'modern-ui='+mode, 'passed')
PY
