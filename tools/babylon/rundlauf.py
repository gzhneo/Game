#!/usr/bin/env python3
# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
#
# Rundlauf des Übersetzungswerkzeugs mit eigenen Kleinst-.csf-Dateien, wie bei der Abnahme mit den echten
# Spieldaten: englische .csf -> csf-to-str -> Data/Generals.str -> check -> update -> import-game German
# <deutsche .csf> -> generate German und US -> Label für Label gegen die Ausgangsdateien.
#
# Abgedeckt: Anführungszeichen im Text (auch am Anfang), Zeilenumbruch ohne Leerzeichen davor, leere
# Übersetzung eines nicht leeren englischen Texts, Backslash, Tabulator, Sprachdateien, Formatangaben,
# Zeichen außerhalb von Latin-1 im Deutschen, Labels nur in einer der beiden Dateien, ein noch nicht
# vorhandener Ausgabeordner. Die englischen Texte stehen mit Leerzeichen um Zeilenumbruch und Tabulator, wie EAs
# Babylon die Generals.str liest (formatMetaString); die deutschen kommen wortgetreu aus der .csf.
#
#   python3 tools/babylon/rundlauf.py <BabylonTool> [Arbeitsordner]
#
# Rückgabewert 0, wenn alles gleich ist.
import os
import shutil
import struct
import subprocess
import sys
import tempfile

# (Label, englischer Text, englische Sprachdatei, deutscher Text, deutsche Sprachdatei); None: fehlt in der Datei
TEXTS = [
    ('GUI:HostWantsToStart', 'The host is ready. Click "Accept".', '', 'Der Host ist bereit. Klicke auf "Annehmen".', ''),
    ('TOOLTIP:BioStrategyLong_Pos0', 'General Malcolm "Ace" Granger trusts the air force.', '',
     'General Malcolm "Ass" Granger vertraut der Luftwaffe.', ''),
    ('TOOLTIP:BioStrategyLong_Pos5', '"Pinpoint" Townes hits every target.', '', '"Nadelspitze" Townes trifft jedes Ziel.', ''),
    ('OBJECT:Aurora', 'Aurora \n Bomber', '', 'Aurora-\n Bomber', ''),
    ('TIP:Long', 'First line \n second line', '', 'Erste Zeile\nzweite Zeile', ''),
    ('GUI:GSErrorTitle', 'Error', '', '', ''),
    ('DIALOG:Hello', 'Hello, commander.', 'W01e', 'Hallo, Kommandant.', 'W01g'),
    ('GUI:Path', 'C:\\Games\\', '', 'C:\\Spiele\\', ''),
    ('GUI:Tab', 'Name \t Value', '', 'Name\tWert', ''),
    ('GUI:Size', 'Size "big" - done...', '', 'Gr\u00f6\u00dfe \u201egro\u00df\u201c \u2013 fertig\u2026', ''),
    ('GUI:Built', '%d units built', '', '%d Einheiten gebaut', ''),
    ('GUI:OnlyEnglish', 'Only in English', '', None, ''),
    ('GUI:OnlyGerman', None, '', 'Nur auf Deutsch', ''),
]

LANG_US = 0
LANG_GERMAN = 3


def write_csf(path, language, entries):
    out = bytearray(b' FSC' + struct.pack('<IIIII', 3, len(entries), len(entries), 0, language))
    for label, text, wave in entries:
        name = label.encode('latin-1')
        out += b' LBL' + struct.pack('<II', 1, len(name)) + name
        units = text.encode('utf-16-le')
        inverted = bytes(b ^ 0xFF for b in units)
        out += (b'WRTS' if wave else b' RTS') + struct.pack('<I', len(units) // 2) + inverted
        if wave:
            w = wave.encode('ascii')
            out += struct.pack('<I', len(w)) + w
    with open(path, 'wb') as f:
        f.write(out)


def read_csf(path):
    with open(path, 'rb') as f:
        data = f.read()
    assert data[:4] == b' FSC', path
    count = struct.unpack_from('<I', data, 8)[0]
    pos = 24
    entries = {}
    order = []
    for _ in range(count):
        assert data[pos:pos + 4] == b' LBL', (path, pos)
        strings, length = struct.unpack_from('<II', data, pos + 4)
        label = data[pos + 12:pos + 12 + length].decode('latin-1')
        pos += 12 + length
        text, wave = None, ''
        for k in range(strings):
            tag = data[pos:pos + 4]
            n = struct.unpack_from('<I', data, pos + 4)[0]
            raw = bytes(b ^ 0xFF for b in data[pos + 8:pos + 8 + 2 * n])
            pos += 8 + 2 * n
            w = ''
            if tag == b'WRTS':
                m = struct.unpack_from('<I', data, pos)[0]
                w = data[pos + 4:pos + 4 + m].decode('ascii')
                pos += 4 + m
            if k == 0:
                text, wave = raw.decode('utf-16-le'), w
        entries[label] = (text, wave)
        order.append(label)
    return entries, order


def main():
    if len(sys.argv) < 2:
        print('rundlauf.py <BabylonTool> [Arbeitsordner]')
        return 2
    tool = os.path.abspath(sys.argv[1])
    work = os.path.abspath(sys.argv[2]) if len(sys.argv) > 2 else tempfile.mkdtemp(prefix='babylon-rundlauf-')
    shutil.rmtree(work, ignore_errors=True)
    os.makedirs(os.path.join(work, 'Data'))
    english = [(l, e, ew) for l, e, ew, g, gw in TEXTS if e is not None]
    german = [(l, g, gw) for l, e, ew, g, gw in TEXTS if g is not None]
    write_csf(os.path.join(work, 'english.csf'), LANG_US, english)
    write_csf(os.path.join(work, 'german.csf'), LANG_GERMAN, german)

    failed = 0

    def run(*args):
        nonlocal failed
        r = subprocess.run([tool, *args], cwd=work, capture_output=True, text=True, encoding='latin-1')
        log = r.stdout + r.stderr
        with open(os.path.join(work, 'rundlauf.log'), 'a', encoding='utf-8') as f:
            f.write('$ BabylonTool ' + ' '.join(args) + '\n' + log + '\n')
        if r.returncode != 0:
            print('FEHLER BabylonTool %s: Rückgabewert %d\n%s' % (' '.join(args), r.returncode, log))
            failed += 1
        return log

    run('csf-to-str', 'english.csf', 'Data/Generals.str')
    check = run('check')
    if 'quote' in check:
        print('FEHLER check meldet Anführungszeichen:\n' + check)
        failed += 1
    run('update')
    run('import-game', 'German', 'german.csf')
    generated = run('generate', 'generals', '--out', 'neu/ausgabe', 'German', 'US')
    if 'format conversions' in generated:
        print('FEHLER generate meldet andere Formatangaben:\n' + generated)
        failed += 1

    def compare(name, path, expected, untranslated):
        nonlocal failed
        if not os.path.exists(path):
            print('FEHLER %s nicht erzeugt' % path)
            failed += 1
            return 0
        got, order = read_csf(path)
        if order != [l for l, _, _ in english]:
            print('FEHLER %s: Labels %s, erwartet %s' % (name, order, [l for l, _, _ in english]))
            failed += 1
        same = 0
        for label, text, wave in expected:
            if label not in got:
                continue
            if got[label] != (text, wave):
                print('FEHLER %s %s: %r, erwartet %r' % (name, label, got[label], (text, wave)))
                failed += 1
            else:
                same += 1
        for label in untranslated:
            text = got.get(label, ('', ''))[0]
            if not text.isdigit():
                print('FEHLER %s %s: %r, erwartet die Nummer des Texts' % (name, label, text))
                failed += 1
        return same

    out = os.path.join(work, 'neu', 'ausgabe')
    english_labels = {l for l, _, _ in english}
    german_labels = {l for l, _, _ in german}
    ge = compare('German', os.path.join(out, 'generals_ge.csf'), german, sorted(english_labels - german_labels))
    us = compare('US', os.path.join(out, 'generals_us.csf'), english, [])
    print('Rundlauf .csf -> Generals.str -> main.db -> .csf: %d deutsche und %d englische Texte gleich, %d Fehler' % (ge, us, failed))
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
