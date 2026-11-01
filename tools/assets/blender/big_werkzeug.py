# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
# .big-Archive lesen und schreiben für die Modell-Werkzeuge (TODO 56/62). Nur lesend auf EA-Archive.
#
#   python big_werkzeug.py suche <muster> <archiv.big>...        Einträge, deren Pfad <muster> enthält (mit MD5)
#   python big_werkzeug.py entpacke <archiv.big> <ziel> <name>... Einträge nach Dateinamen (ohne Pfad, ohne
#                                                                 Groß-/Kleinschreibung) in <ziel> entpacken
#   python big_werkzeug.py grep <text> <archiv.big> [kontext]     Text in INI/TXT-Einträgen suchen
#   python big_werkzeug.py packe <ziel.big> <archivpfad>=<datei>...   Testarchiv schreiben
#                                                                 (z. B. Art\W3D\AvHummer.W3D=export\AVHUMMER.w3d)
# DDS-Kodierung für das Packen einer Textur: humvee_packen.py.
import hashlib
import os
import struct
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))


def eintraege(pfad):
    with open(pfad, 'rb') as f:
        kopf = f.read(16)
        n = struct.unpack('>I', kopf[8:12])[0]
        out = []
        for _ in range(n):
            off, gr = struct.unpack('>II', f.read(8))
            name = b''
            while True:
                c = f.read(1)
                if c in (b'\0', b''):
                    break
                name += c
            out.append((name.decode('latin-1'), off, gr))
        return out


def suche(muster, archive):
    muster = muster.lower()
    for a in archive:
        with open(a, 'rb') as f:
            for name, off, gr in eintraege(a):
                if muster in name.lower():
                    f.seek(off)
                    h = hashlib.md5(f.read(gr)).hexdigest()[:10]
                    print('%-50s %-45s %8d %s' % (a[-50:], name, gr, h))


def entpacke(archiv, ziel, namen):
    wuensche = {n.lower() for n in namen}
    os.makedirs(ziel, exist_ok=True)
    with open(archiv, 'rb') as f:
        for name, off, gr in eintraege(archiv):
            kurz = name.split('\\')[-1].lower()
            if kurz in wuensche:
                f.seek(off)
                with open(os.path.join(ziel, kurz), 'wb') as g:
                    g.write(f.read(gr))
                print('entpackt', name, gr)


def grep(text, archiv, ktx=0):
    text = text.lower()
    with open(archiv, 'rb') as f:
        for name, off, gr in eintraege(archiv):
            if not name.lower().endswith(('.ini', '.txt', '.str')):
                continue
            f.seek(off)
            zeilen = f.read(gr).decode('latin-1').splitlines()
            for i, z in enumerate(zeilen):
                if text in z.lower():
                    print('--- %s:%d' % (name, i + 1))
                    for j in range(max(0, i - ktx), min(len(zeilen), i + ktx + 1)):
                        print('   ', zeilen[j])


def packe(ziel, paare):
    from build_texture_tiers import write_big
    eintr = []
    for p in paare:
        name, _, datei = p.partition('=')
        with open(datei, 'rb') as f:
            eintr.append((name, f.read()))
    os.makedirs(os.path.dirname(os.path.abspath(ziel)), exist_ok=True)
    write_big(ziel, eintr)
    print('geschrieben', ziel, [(n, len(d)) for n, d in eintr])


if __name__ == '__main__':
    cmd = sys.argv[1]
    if cmd == 'suche':
        suche(sys.argv[2], sys.argv[3:])
    elif cmd == 'entpacke':
        entpacke(sys.argv[2], sys.argv[3], sys.argv[4:])
    elif cmd == 'grep':
        grep(sys.argv[2], sys.argv[3], int(sys.argv[4]) if len(sys.argv) > 4 else 0)
    elif cmd == 'packe':
        packe(sys.argv[2], sys.argv[3:])
    else:
        sys.exit('unbekannter Befehl: ' + cmd + ' (suche, entpacke, grep, packe)')
