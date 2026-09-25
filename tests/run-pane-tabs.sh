#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
test_dir=$(mktemp -d "${TMPDIR:-/tmp}/emelfm2-tabs.XXXXXX")
trap 'rm -rf "$test_dir"' EXIT HUP INT TERM
gtk_package=${GTK_PACKAGE:-gtk+-3.0}
${CC:-cc} -shared -fPIC -Wno-deprecated-declarations -fcommon -D_FILE_OFFSET_BITS=64 \
    -Isrc -Isrc/actions -Isrc/build -Isrc/command -Isrc/command/complete \
    -Isrc/config -Isrc/dialogs -Isrc/filesystem -Isrc/utils \
    $(pkg-config --cflags "$gtk_package") tests/pane-tabs-smoke.c \
    $(pkg-config --libs "$gtk_package") -o "$test_dir/smoke.so"
export E2_TABS_TEST="$test_dir"
LC_ALL=C.UTF-8 GDK_BACKEND=x11 NO_AT_BRIDGE=1 GIO_USE_VFS=local dbus-run-session -- xvfb-run -a python3 - <<'PY'
import os, pathlib, subprocess, time
root = pathlib.Path(os.environ['E2_TABS_TEST'])
# Toolbar folding depends on font metrics. Use known GTK 3 settings so a
# compact user font cannot hide resize/rebuild failures.
settings = root/'xdg-config'/'gtk-3.0'
settings.mkdir(parents=True)
(settings/'settings.ini').write_text('[Settings]\ngtk-font-name=DejaVu Sans 10\n')
for name in ['alpha', 'beta', 'gamma', 'delta', '$HOME %f 日本語',
             'left-folder-with-a-very-long-name-and-distinct-ending',
             'right-folder-with-a-very-long-name-and-another-ending',
             '日本語-長いフォルダー名と中央の文字列-終わり']:
    (root/name).mkdir()
    (root/name/'one').write_text('one')
    (root/name/'two').write_text('two')
(root/'shell').write_text('#!/bin/sh\necho $$ > terminal-started\nexec sleep 60\n')
(root/'shell').chmod(0o755)
args = ['./emelfm2', '-c', str(root/'config'), '-1', str(root/'alpha'), '-2', str(root/'beta'),
        '-s', 'pane-tabs=true', '-s', 'session-end-warning=false', '-s', 'single-instance=false']
env = dict(os.environ, LD_PRELOAD=str(root/'smoke.so'), XDG_CONFIG_HOME=str(root/'xdg-config'),
           GTK_THEME=os.environ.get('GTK_THEME', 'Adwaita'))
if os.environ.get("E2_TABS_GDB"):
    args = ["gdb", "-batch", "-ex", "set startup-with-shell off", "-ex", "set environment LD_PRELOAD="+env.pop("LD_PRELOAD"),
            "-ex", "run", "-ex", "thread apply all bt", "--args"] + args
args += ['-s', 'modern-ui='+os.environ.get('E2_TEST_MODERN_UI', 'false')]
with open(root/'log', 'w') as output:
    p = subprocess.Popen(args, env=env,
                         stdout=output, stderr=output)
    try:
        deadline = time.monotonic()+30
        while not (root/'passed').exists() and time.monotonic()<deadline:
            if p.poll() is not None: break
            time.sleep(.05)
        assert (root/'passed').exists(), (root/'log').read_text()
        result = p.wait(timeout=10)
        assert result == 0, str(result)+"\n"+(root/'log').read_text()
        log = (root/'log').read_text()
        assert 'CRITICAL' not in log and 'WARNING' not in log, log
        print('pane-tabs: initial visibility, Ctrl+N, Ctrl+Tab wrapping, cursor/selection, independent folders/state, queued navigation, layout, rebuild, close, option toggling and independent title shortening passed')
    finally:
        if p.poll() is None:
            p.kill()
            p.wait(timeout=10)
PY
