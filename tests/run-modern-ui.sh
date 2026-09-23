#!/bin/sh
# Compare real widget allocations and resolved palettes with the feature off/on.
set -eu
cd "$(dirname "$0")/.."
test_dir=$(mktemp -d "${TMPDIR:-/tmp}/emelfm2-modern-ui.XXXXXX")
trap 'rm -rf "$test_dir"' EXIT HUP INT TERM
gtk_package=${GTK_PACKAGE:-gtk+-2.0}
${CC:-cc} -shared -fPIC -Wno-deprecated-declarations -fcommon -D_FILE_OFFSET_BITS=64 \
    -Isrc -Isrc/actions -Isrc/build -Isrc/command -Isrc/command/complete \
    -Isrc/config -Isrc/dialogs -Isrc/filesystem -Isrc/utils \
    $(pkg-config --cflags "$gtk_package") tests/modern-ui.c \
    $(pkg-config --libs "$gtk_package") -o "$test_dir/test.so"
export E2_MODERN_TEST_DIR="$test_dir"
LC_ALL=C.UTF-8 GDK_BACKEND=x11 NO_AT_BRIDGE=1 GIO_USE_VFS=local dbus-run-session -- xvfb-run -a python3 - <<'PY'
import os, pathlib, subprocess, difflib
root = pathlib.Path(os.environ['E2_MODERN_TEST_DIR'])
(root/'pane').mkdir()
(root/'pane'/'README').write_text('sample')
for theme in os.environ.get('E2_MODERN_TEST_THEMES', 'Adwaita,Adwaita-dark').split(','):
    rc = root/'gtkrc'
    theme_rc = pathlib.Path('/usr/share/themes')/theme/'gtk-2.0/gtkrc'
    if not theme_rc.exists(): theme_rc = pathlib.Path.home()/'.themes'/theme/'gtk-2.0/gtkrc'
    rc.write_text(f'include "{theme_rc}"\ngtk-font-name="DejaVu Sans 10"\n')
    outputs = []
    for mode in ('default', 'false', 'true'):
        case = f'{theme}-{mode}'
        config = root/(case+'-profile')
        config.mkdir()
        (config/'cache').write_text('window-width=1000\nwindow-height=720\nfile-pane-ratio=0.5\n')
        env = dict(os.environ, LD_PRELOAD=str(root/'test.so'), GTK_THEME=theme,
                   GTK2_RC_FILES=str(rc), E2_MODERN_TEST_CASE=case)
        args = [os.environ.get('E2_MODERN_TEST_BINARY', './emelfm2'), '-c', str(config),
                '-1', str(root/'pane'), '-2', str(root/'pane'), '-s', 'single-instance=false',
                '-s', 'session-end-warning=false',
                '-s', 'color-ft-dir=#00ccff', '-s', 'pane-tabs=true']
        if mode != 'default': args += ['-s', 'modern-ui='+mode]
        run = subprocess.run(args, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=20)
        log = run.stdout.decode(errors='replace')
        assert run.returncode == 0 and 'CRITICAL' not in log and 'WARNING' not in log, case+'\n'+log[-10000:]
        assert (root/case).exists(), log
        outputs.append((root/case).read_text().splitlines())
    for actual in outputs[1:]:
        diff = '\n'.join(difflib.unified_diff(outputs[0], actual, fromfile=theme+' default', tofile=theme+' explicit'))
        assert not diff, diff
print('modern appearance: light/dark colors and allocations unchanged; menu hover/keyboard highlight, default, startup latch, rebuild, custom colors/icons passed')
PY
