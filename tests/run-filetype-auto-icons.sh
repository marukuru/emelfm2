#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
test_dir=$(mktemp -d "${TMPDIR:-/tmp}/emelfm2-auto-icons.XXXXXX")
trap 'rm -rf "$test_dir"' EXIT HUP INT TERM
gtk_package=${GTK_PACKAGE:-gtk+-2.0}
${CC:-cc} -shared -fPIC -Wno-deprecated-declarations -fcommon -D_FILE_OFFSET_BITS=64 \
    -Isrc -Isrc/actions -Isrc/build -Isrc/command -Isrc/command/complete \
    -Isrc/config -Isrc/dialogs -Isrc/filesystem -Isrc/utils \
    $(pkg-config --cflags "$gtk_package") tests/filetype-auto-icons.c \
    $(pkg-config --libs "$gtk_package") -o "$test_dir/test.so"
export E2_AUTO_ICON_TEST="$test_dir"
LC_ALL=C.UTF-8 GDK_BACKEND=x11 NO_AT_BRIDGE=1 GIO_USE_VFS=local dbus-run-session -- xvfb-run -a python3 - <<'PY'
import os, pathlib, shutil, subprocess
root = pathlib.Path(os.environ['E2_AUTO_ICON_TEST'])
(root/'pane').mkdir()
(root/'pane'/'a.autoprobe').write_text('Automatic filetype icon fixture\n')
shutil.copyfile('icons/plugin_config_48.png', root/'application.png')
binaries = root/'bin with spaces'
binaries.mkdir()
program = binaries/'e2-icon-probe'
program.write_text('#!/bin/sh\ntouch "$E2_AUTO_ICON_TEST/executed"\n')
program.chmod(0o755)
applications = root/'data'/'applications'
applications.mkdir(parents=True)
(applications/'org.example.probe.desktop').write_text(
    f'[Desktop Entry]\nType=Application\nName=Unrelated application name\nExec="{program}" %F\nIcon={root}/application.png\n')
theme = root/'data'/'icons'/'hicolor'
(theme/'16x16'/'apps').mkdir(parents=True)
(theme/'index.theme').write_text('[Icon Theme]\nName=Test icons\nDirectories=16x16/apps\n\n[16x16/apps]\nSize=16\nType=Fixed\nContext=Applications\n')
shutil.copyfile('icons/plugin_config_48.png', theme/'16x16'/'apps'/'e2-theme-probe.png')
# A shared executable with conflicting application icons must not pick one arbitrarily.
launcher = binaries/'e2-ambiguous-probe'
shutil.copyfile(program, launcher)
launcher.chmod(0o755)
for index, icon in enumerate((root/'application.png', theme/'16x16'/'apps'/'e2-theme-probe.png')):
    (applications/f'org.example.shared{index}.desktop').write_text(
        f'[Desktop Entry]\nType=Application\nName=Shared launcher {index}\nExec="{launcher}" --app{index} %F\nIcon={icon}\n')
for modern in ('false', 'true'):
    args = ['./emelfm2', '-c', str(root/('config-'+modern)), '-1', str(root/'pane'), '-2', str(root/'pane'),
            '-s', 'session-end-warning=false', '-s', 'single-instance=false', '-s', 'modern-ui='+modern]
    for reload in (False, True):
        env = dict(os.environ, LD_PRELOAD=str(root/'test.so'), XDG_DATA_HOME=str(root/'data'),
                   PATH=str(binaries)+os.pathsep+os.environ['PATH'])
        if reload: env['E2_AUTO_ICON_RELOAD'] = '1'
        run = subprocess.run(args, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=25)
        log = run.stdout.decode(errors='replace')
        assert run.returncode == 0 and 'CRITICAL' not in log and 'WARNING' not in log, log
        expected = 'automatic filetype icons reload passed' if reload else 'automatic filetype icons buttons, matching, overrides and discard passed'
        assert expected in log, log
        assert not (root/'executed').exists(), 'Icon discovery executed a command'
        print('modern-ui='+modern, expected)
PY
