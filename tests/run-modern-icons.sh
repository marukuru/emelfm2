#!/bin/sh
# Verify stock/bundled coverage and real icon-picker previews/selection.
set -eu
cd "$(dirname "$0")/.."
test_dir=$(mktemp -d "${TMPDIR:-/tmp}/emelfm2-modern-icons.XXXXXX")
trap 'rm -rf "$test_dir"' EXIT HUP INT TERM
gtk_package=${GTK_PACKAGE:-gtk+-2.0}
${CC:-cc} -shared -fPIC -Wno-deprecated-declarations -fcommon -D_FILE_OFFSET_BITS=64 \
    -Isrc -Isrc/actions -Isrc/build -Isrc/command -Isrc/command/complete \
    -Isrc/config -Isrc/dialogs -Isrc/filesystem -Isrc/utils \
    $(pkg-config --cflags "$gtk_package") tests/modern-icons.c \
    $(pkg-config --libs "$gtk_package") -o "$test_dir/test.so"
export E2_ICON_TEST_DIR="$test_dir"
LC_ALL=C.UTF-8 GDK_BACKEND=x11 NO_AT_BRIDGE=1 GIO_USE_VFS=local dbus-run-session -- xvfb-run -a python3 - <<'PY'
import os, pathlib, re, subprocess
root = pathlib.Path(os.environ['E2_ICON_TEST_DIR'])
stocks = re.findall(r'#define STOCK_NAME_\w+\s+"([^"]+)"', pathlib.Path('src/utils/e2_icons.h').read_text())
(root/'stock.txt').write_text('\n'.join(sorted(set(stocks))))
# Derive coverage expectations from shipped files, not the renderer's own table.
names = {p.name for p in pathlib.Path('icons').rglob('*')
         if p.suffix in ('.svg', '.png') and 'stock' not in p.parts and not p.name.startswith('emelfm2')}
(root/'bundled.txt').write_text('\n'.join(sorted(names)))
(root/'pane').mkdir()
(root/'custom').mkdir()
(root/'custom'/'move.svg').write_text('<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24"><rect width="24" height="24" fill="#ff0000"/></svg>')
for theme in os.environ.get('E2_MODERN_TEST_THEMES', 'Adwaita,Adwaita-dark').split(','):
    rc = root/'gtkrc'
    theme_rc = pathlib.Path('/usr/share/themes')/theme/'gtk-2.0/gtkrc'
    if not theme_rc.exists(): theme_rc = pathlib.Path.home()/'.themes'/theme/'gtk-2.0/gtkrc'
    rc.write_text(f'include "{theme_rc}"\ngtk-font-name="DejaVu Sans 10"\n')
    for mode in ('false', 'true'):
        config = root/(theme+'-'+mode)
        config.mkdir()
        env = dict(os.environ, LD_PRELOAD=str(root/'test.so'), GTK_THEME=theme,
                   GTK2_RC_FILES=str(rc), E2_ICON_CUSTOM_DIR=str(root/'custom'))
        args = ['./emelfm2', '-c', str(config), '-1', str(root/'pane'), '-2', str(root/'pane'),
                '-s', 'single-instance=false', '-s', 'session-end-warning=false', '-s', 'modern-ui='+mode]
        run = subprocess.run(args, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=20)
        log = run.stdout.decode(errors='replace')
        assert run.returncode == 0 and 'CRITICAL' not in log and 'WARNING' not in log, theme+' '+mode+'\n'+log[-10000:]
        assert 'History selection and custom files passed' in log, log
        print(theme, 'modern-ui='+mode, 'passed')
print(f'Icon coverage: {len(set(stocks))} stock IDs and {len(names)} bundled filenames')
PY
