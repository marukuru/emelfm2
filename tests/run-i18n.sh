#!/bin/sh
# Exercise the real translation build using private copies of the catalogues.
set -eu
cd "$(dirname "$0")/.."
python3 - <<'PY'
import gettext, json, os, pathlib, shutil, subprocess, tempfile

gtk = os.environ.get('TEST_GTK', '2')
vte = os.environ.get('TEST_VTE', '0')
args = ['make', '--no-print-directory', 'install_i18n', 'I18N=1',
        'WITH_GTK2='+str(int(gtk == '2')), 'WITH_GTK3='+str(int(gtk == '3')),
        'WITH_VTE='+vte]
messages = {'Permissions': 'Rechte', 'Permissions…': 'Rechte…',
            'Choose directory': 'Verzeichnis wählen',
            'Choose directory…': 'Verzeichnis wählen…',
            '↑ / ↓: select action    Enter: run action    Esc: close': '↑ / ↓: auswählen'}
if vte == '1':
    messages['Starting shell…'] = 'Shell wird gestartet…'

with tempfile.TemporaryDirectory(prefix='emelfm2-i18n.') as temporary:
    root = pathlib.Path(temporary)
    catalogs = root/'po files'
    catalogs.mkdir()
    for source in pathlib.Path('po').glob('*.po'):
        shutil.copy2(source, catalogs/source.name)
    header = ('Project-Id-Version: i18n-test\nLanguage: zz\nMIME-Version: 1.0\n'
              'Content-Type: text/plain; charset=UTF-8\nContent-Transfer-Encoding: 8bit\n'
              'Plural-Forms: nplurals=2; plural=(n != 1);\n')
    fixture = 'msgid ""\nmsgstr '+json.dumps(header)+'\n\n'
    for message, translation in messages.items():
        fixture += 'msgid '+json.dumps(message, ensure_ascii=False)+'\n'
        fixture += 'msgstr '+json.dumps(translation, ensure_ascii=False)+'\n\n'
    (catalogs/'zz.po').write_text(fixture, encoding='utf-8')

    def build(prefix, locale='C', extra=()):
        return subprocess.run(args+['PO_DIR='+str(catalogs), 'PREFIX='+str(prefix)]+list(extra),
                              env=dict(os.environ, LC_ALL=locale), stdout=subprocess.PIPE,
                              stderr=subprocess.STDOUT, text=True, encoding='utf-8', timeout=60)

    for locale in ('C', 'C.UTF-8'):
        prefix = root/('install '+locale)
        result = build(prefix, locale)
        assert result.returncode == 0, result.stdout
        assert 'warning:' not in result.stdout.lower(), result.stdout
        assert 'invalid multibyte' not in result.stdout, result.stdout
        assert 'duplicate message' not in result.stdout, result.stdout
        template = (catalogs/'emelfm2.pot').read_text(encoding='utf-8')
        assert 'charset=UTF-8' in template, template[:1000]
        duplicates = subprocess.run(['msguniq', '--repeated', str(catalogs/'emelfm2.pot')],
                                    stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=True)
        assert not duplicates.stdout and not duplicates.stderr, duplicates
        for message in messages:
            assert json.dumps(message, ensure_ascii=False) in template, message
        for source in catalogs.glob('*.po'):
            installed = prefix/'share/locale'/source.stem/'LC_MESSAGES/emelfm2.mo'
            with installed.open('rb') as stream:
                translated = gettext.GNUTranslations(stream)
            if source.stem == 'zz':
                for message, expected in messages.items():
                    assert translated.gettext(message) == expected, message

    # A failed stage must prevent installation, even with old .mo files present.
    for tool in ('BIN_XGETTEXT', 'BIN_MSGMERGE', 'BIN_MSGFMT'):
        prefix = root/tool
        result = build(prefix, extra=(tool+'='+shutil.which('false'),))
        assert result.returncode != 0, tool+' failure was ignored\n'+result.stdout
        assert not prefix.exists(), tool+' failure installed stale catalogues'

    # A failure for the first language must not be hidden by later installs.
    prefix = root/'install-failure'
    blocked = prefix/'share/locale/de'
    blocked.parent.mkdir(parents=True)
    blocked.write_text('not a directory')
    result = build(prefix)
    assert result.returncode != 0, 'installation failure was ignored\n'+result.stdout
    assert not (blocked.parent/'fr').exists(), 'installation continued after an error'

print('i18n: UTF-8 messages, all catalogues, installed translations, C/UTF-8 locales and failed stages passed')
PY
