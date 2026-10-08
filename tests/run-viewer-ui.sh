#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
test_dir=$(mktemp -d "${TMPDIR:-/tmp}/emelfm2-viewer-ui.XXXXXX")
trap 'rm -rf "$test_dir"' EXIT HUP INT TERM
gtk_package=${GTK_PACKAGE:-gtk+-2.0}
${CC:-cc} -shared -fPIC -Wno-deprecated-declarations -fcommon -D_FILE_OFFSET_BITS=64 \
    -Isrc -Isrc/actions -Isrc/build -Isrc/command -Isrc/command/complete \
    -Isrc/config -Isrc/dialogs -Isrc/filesystem -Isrc/utils \
    $(pkg-config --cflags "$gtk_package" pangofc) tests/viewer-ui.c \
    $(pkg-config --libs "$gtk_package" pangofc) -o "$test_dir/test.so"
export E2_VIEWER_TEST="$test_dir"
LC_ALL=C.UTF-8 NO_AT_BRIDGE=1 GIO_USE_VFS=local dbus-run-session -- xvfb-run -a python3 - <<'PY'
import os, pathlib, subprocess, time
root = pathlib.Path(os.environ['E2_VIEWER_TEST'])
(root/'plain.txt').write_text('https://example.test/a?x=$(id)&b=1\nHello 日本語\n' + 'long line ' * 100 + '\n' + 'another line\n' * 60)
for name in ('viewer.log', 'viewer.LOG', 'viewer.LoG'):
    (root/name).write_text('old line\n' * 2000 + 'newest line\n')
(root/'empty.log').write_text('')
# Cancel the sparse large files; use small fixtures for boundary/continue tests.
with (root/'large 100% <notes> & 日本語.txt').open('wb') as file:
    file.truncate(128 * 1024 * 1024 + 1)
(root/'large-link.txt').symlink_to('large 100% <notes> & 日本語.txt')
with (root/'five-gb.txt').open('wb') as file:
    file.truncate(5 * 1024 * 1024 * 1024)
payload = (b'ordinary text ' * 4 + b'1234567\n') * 16384
assert len(payload) == 1024 * 1024
(root/'below-limit.txt').write_bytes(payload[:-1])
(root/'at-limit.txt').write_bytes(payload)
(root/'over-limit.txt').write_bytes(payload + b'X')
(root/'growing.txt').write_text('before refresh\n')
(root/'pc.nfo').write_bytes(bytes.fromhex('c9cdcdcdbb0ab ab0b1b2ba0ac8cdcd cdbc'.replace(' ', '')))
(root/'amiga.nfo').write_bytes('ÆØØØ:........:ØØØ\r\n'.encode('latin1'))
for name in ('pc.NFO', 'art.txt', 'untyped'):
    (root/name).write_bytes((root/'pc.nfo').read_bytes())
(root/'amiga.aSc').write_bytes((root/'amiga.nfo').read_bytes())
(root/'100% <notes> & longer file viewer filename.txt').write_text('Viewer layout regression\n')
(root/'syntax.c').write_text('/* 日本語 */\nint main(void) { return 42; }\n// https://example.test/\n')
(root/'syntax.py').write_bytes('def greet():\n    return "世界"\n'.encode('utf-16'))
(root/'binary.c').write_bytes(b'int main(void) { return 0; }\n\0')
(root/'art.c').write_bytes((root/'pc.nfo').read_bytes() + b'\nreturn\n')
(root/'utf16.txt').write_bytes('Hello 世界\r\nlast line\r\n'.encode('utf-16'))
for columns, lines in ((80, 100), (80, 1000), (120, 100)):
    (root/f'{columns}-columns-{lines}-lines.txt').write_text('\n'.join(['X' * columns] * lines))
browser = root/'fake browser'
browser.write_text('#!/bin/sh\nprintf "%s\\n" "$#" "$1" >> "$E2_VIEWER_TEST/opened"\n')
browser.chmod(0o755)
(root/'xdg-config').mkdir()
(root/'xdg-data'/'applications').mkdir(parents=True)
(root/'xdg-data'/'applications'/'viewer-browser.desktop').write_text(
    '[Desktop Entry]\nType=Application\nName=Viewer test browser\nExec="' + str(browser) + '" %u\n'
    'MimeType=x-scheme-handler/http;x-scheme-handler/https;\nNoDisplay=true\n')
(root/'xdg-config'/'mimeapps.list').write_text(
    '[Default Applications]\nx-scheme-handler/http=viewer-browser.desktop\nx-scheme-handler/https=viewer-browser.desktop\n')
os.environ['XDG_CONFIG_HOME'] = str(root/'xdg-config')
os.environ['XDG_DATA_HOME'] = str(root/'xdg-data')
with open(root/'log', 'w') as output:
    p = subprocess.Popen([os.environ.get('E2_VIEWER_BINARY', './emelfm2'), '-c', str(root/'config'), '-1', str(root), '-s', 'session-end-warning=false'],
        env=dict(os.environ, LD_PRELOAD=str(root/'test.so')), stdout=output, stderr=output)
    try:
        deadline = time.monotonic() + 60
        while not (root/'passed').exists() and p.poll() is None and time.monotonic() < deadline: time.sleep(.05)
        assert (root/'passed').exists(), (root/'log').read_text()
        assert p.wait(timeout=10) == 0, (root/'log').read_text()
        configs = list((root/'config').glob('config-*'))
        assert any('dialog-view-syntax-highlighting=false' in p.read_text() for p in configs), configs
        assert any('dialog-view-size-limit=64' in p.read_text() for p in configs), configs
        log = (root/'log').read_text()
        assert 'CRITICAL' not in log and 'WARNING' not in log, log
        print('viewer UI: syntax detection, live toggle, encoding and refresh highlighting, artwork and extension filters, fonts, decoding, links, text sizing and reflow, line numbers, compact controls, refresh, automatic end positioning, large-file warnings and settings passed')
    finally:
        if p.poll() is None: p.kill(); p.wait(timeout=10)
PY
