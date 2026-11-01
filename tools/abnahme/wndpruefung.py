#!/usr/bin/env python3
# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
"""Alle Fensterlayouts (.wnd) der .big-Archive gegen die Regeln des Layout-Lesers pruefen (R-339).

Liest jede .wnd-Datei wie GameWindowManagerScript.cpp (Woerter, Abschnitte bis ';', WINDOW/CHILD/END) und meldet:
  - Dateien, die vor ihrem Ende abbrechen (der Leser haengte frueher darin),
  - Datenfelder, die nicht zum Fenstertyp passen (LISTBOXDATA in einem STATICTEXT usw.; der Leser ignoriert sie jetzt),
  - Werte ausserhalb der Grenzen, auf die der Leser sie begrenzt (Listenlaenge, Spalten, Textlaenge, Tabs, Schieber, Rechtecke),
  - die groesste Schachtelungstiefe von Kindfenstern (der Leser bricht bei zu tiefer Schachtelung ab).
Erwartet: nichts davon in EAs Dateien; die Grenzen aendern dann kein gueltiges Layout.

Aufruf:
    python tools/abnahme/wndpruefung.py [--auspacken <Ordner>] [Asset-Ordner ...]
        (Vorgabe: die beiden Asset-Ordner des Projekts). Mit --auspacken kommen die Layouts als Dateien in den
        Ordner, etwa fuer den Rundlauf des GUI-Editors: WndTool --roundtrip <Ordner>.
Ende 0 = nichts gefunden, 1 = Abweichungen.
"""
import os
import re
import struct
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
import pfade  # noqa: E402
ASSET_ORDNER = pfade.alle_assets()
STACK_TIEFE = 9          # WIN_STACK_DEPTH - 1 im Leser
NUM_TAB_PANES = 16

# Datenfelder je Fenstertyp (wie Engine::wndFieldFitsType)
DATENFELDER = {
    'LISTBOXDATA': ('SCROLLLISTBOX',),
    'COMBOBOXDATA': ('COMBOBOX',),
    'SLIDERDATA': ('VERTSLIDER', 'HORZSLIDER'),
    'RADIOBUTTONDATA': ('RADIOBUTTON',),
    'STATICTEXTDATA': ('STATICTEXT',),
    'TEXTENTRYDATA': ('ENTRYFIELD',),
    'TABCONTROLDATA': ('TABCONTROL',),
}
STANDARDFARBEN = ('ENABLEDCOLOR', 'DISABLEDCOLOR', 'HILITECOLOR', 'SELECTEDCOLOR', 'TEXTCOLOR', 'BACKGROUNDCOLOR', 'FONT')


def big_eintraege(pfad):
    """Verzeichnis eines .big-Archivs: [(Name klein, Offset, Groesse)]."""
    with open(pfad, 'rb') as f:
        kopf = f.read(16)
        if kopf[:4] not in (b'BIGF', b'BIG4'):
            return []
        n = struct.unpack('>I', kopf[8:12])[0]
        erster = struct.unpack('>I', kopf[12:16])[0]
        verz = f.read(max(0, erster - 16))
    out = []
    off = 0
    for _ in range(n):
        o, s = struct.unpack('>II', verz[off:off + 8])
        off += 8
        e = verz.index(b'\0', off)
        out.append((verz[off:e].decode('latin-1').lower().replace('/', '\\'), o, s))
        off = e + 1
    return out


class Text:
    """Ein Layout im Speicher, gelesen wie RAMFile: Woerter und Abschnitte."""

    def __init__(self, data):
        self.d = data
        self.p = 0

    def wort(self):
        d, p = self.d, self.p
        while p < len(d) and d[p:p + 1].isspace():
            p += 1
        if p >= len(d):
            self.p = len(d)
            return None
        q = p
        while q < len(d) and not d[q:q + 1].isspace():
            q += 1
        self.p = q
        return d[p:q].decode('latin-1')

    def abschnitt(self):
        """Wie readUntilSemicolon: fuehrender Leerraum weg, Leerraum wird zu Leerzeichen, bis ';' oder Dateiende."""
        d, p = self.d, self.p
        out = []
        start = True
        while p < len(d):
            c = d[p:p + 1]
            p += 1
            if c.isspace():
                if not start:
                    out.append(' ')
            else:
                start = False
                if c == b';':
                    self.p = p
                    return ''.join(out)
                out.append(c.decode('latin-1'))
        self.p = p
        return ''.join(out)

    def zeilenende(self):
        d = self.d
        while self.p < len(d) and d[self.p:self.p + 1] != b'\n':
            self.p += 1
        if self.p < len(d):
            self.p += 1


def zahlen(abschnitt, trenner=' :,\n\r\t'):
    return [t for t in re.split('[' + re.escape(trenner) + ']+', abschnitt) if t]


def zahl(t, i):
    try:
        return int(t[i])
    except (IndexError, ValueError):
        return None


class Pruefung:
    def __init__(self, name):
        self.name = name
        self.meldungen = []
        self.fenster = 0
        self.tiefe_max = 0
        self.leere_texte = 0

    def melde(self, text):
        self.meldungen.append(text)

    # ---------------------------------------------------------------- Fenster
    def fenster_lesen(self, t, tiefe, fenstername):
        """parseWindow: liest bis END; gibt False zurueck, wenn die Datei vorher endet."""
        self.fenster += 1
        self.tiefe_max = max(self.tiefe_max, tiefe)
        typ = None
        kopf = t.abschnitt()                       # WINDOWTYPE = X
        tk = zahlen(kopf, ' =;\n\r\t')
        if len(tk) >= 2 and tk[0] == 'WINDOWTYPE':
            typ = tk[1]
        else:
            self.melde('%s: erster Abschnitt eines Fensters ist kein WINDOWTYPE: %r' % (fenstername, kopf[:40]))
        rect = zahlen(t.abschnitt(), ' ,:=\n\r\t')   # SCREENRECT = ...
        if len(rect) >= 10 and rect[0] == 'SCREENRECT':
            werte = {'links': zahl(rect, 2), 'oben': zahl(rect, 3), 'rechts': zahl(rect, 5), 'unten': zahl(rect, 6),
                     'aufloesung x': zahl(rect, 8), 'aufloesung y': zahl(rect, 9)}
            for k, v in werte.items():
                if v is None:
                    self.melde('%s: SCREENRECT ohne Zahl (%s)' % (fenstername, k))
                elif k.startswith('aufl'):
                    if not 0 <= v <= 65535:
                        self.melde('%s: SCREENRECT %s %d ausserhalb 0..65535' % (fenstername, k, v))
                elif not -32768 <= v <= 32767:
                    self.melde('%s: SCREENRECT %s %d ausserhalb -32768..32767' % (fenstername, k, v))
        else:
            self.melde('%s: zweiter Abschnitt eines Fensters ist kein SCREENRECT: %r' % (fenstername, ' '.join(rect[:3])))
        name = fenstername
        while True:
            w = t.wort()
            if w is None:
                self.melde('%s: Datei endet vor END (Fenster %s)' % (fenstername, name))
                return False
            if w == 'END':
                return True
            if w == 'CHILD':
                if tiefe + 1 > STACK_TIEFE:
                    self.melde('%s: Kindfenster mit Tiefe %d (Grenze des Lesers %d)' % (fenstername, tiefe + 1, STACK_TIEFE))
                if not self.kinder_lesen(t, tiefe + 1, fenstername):
                    return False
                continue
            if w == 'DATA':
                t.wort()
                t.abschnitt()
                continue
            # a field: eat '=' and the chunk; an unknown word eats a chunk too (the reader does that)
            t.wort()
            abschnitt = t.abschnitt()
            if w == 'NAME':
                m = re.search(r'"([^"]*)"', abschnitt)
                if m:
                    name = m.group(1)
            self.feld(w, abschnitt, typ, name)

    def kinder_lesen(self, t, tiefe, fenstername):
        """parseChildWindows: Standardfarben, WINDOW, bis END oder ENDALLCHILDREN."""
        while True:
            w = t.wort()
            if w is None:
                return True                       # the reader breaks at the end of the file
            if w in ('END', 'ENDALLCHILDREN'):
                return True
            if w in STANDARDFARBEN:
                t.wort()
                t.abschnitt()
            elif w == 'WINDOW':
                if not self.fenster_lesen(t, tiefe, fenstername):
                    return False

    # ---------------------------------------------------------------- Felder
    def feld(self, w, abschnitt, typ, name):
        if w in DATENFELDER and typ not in DATENFELDER[w]:
            self.melde('%s (%s): Feld %s gehoert nicht zu diesem Fenstertyp' % (name, typ, w))
        t = zahlen(abschnitt)
        if w == 'LISTBOXDATA':
            laenge = zahl(t, 1)
            if laenge is None or not 1 <= laenge <= 32767:
                self.melde('%s: Listenlaenge %r ausserhalb 1..32767' % (name, laenge))
            if 'COLUMNS' in t:
                spalten = zahl(t, t.index('COLUMNS') + 1)
                if spalten is None or not 1 <= spalten <= 64:
                    self.melde('%s: Spalten %r ausserhalb 1..64' % (name, spalten))
        elif w == 'TEXTENTRYDATA':
            laenge = zahl(t, 1)
            if laenge is None or not 0 <= laenge <= 32767:
                self.melde('%s: Textlaenge %r ausserhalb 0..32767' % (name, laenge))
        elif w == 'COMBOBOXDATA':
            for i, bez in ((3, 'maxChars'), (5, 'maxDisplay')):
                v = zahl(t, i)
                if v is None or not 0 <= v <= 32767:
                    self.melde('%s: Kombifeld %s %r ausserhalb 0..32767' % (name, bez, v))
        elif w == 'TABCONTROLDATA':
            anzahl = zahl(t, 9)
            if anzahl is None or not 0 <= anzahl <= NUM_TAB_PANES:
                self.melde('%s: Tabs %r ausserhalb 0..%d' % (name, anzahl, NUM_TAB_PANES))
            for i, bez in ((5, 'Tabbreite'), (7, 'Tabhoehe'), (11, 'Rand')):
                v = zahl(t, i)
                if v is None or not 0 <= v <= 65535:
                    self.melde('%s: %s %r ausserhalb 0..65535' % (name, bez, v))
        elif w == 'SLIDERDATA':
            lo, hi = zahl(t, 1), zahl(t, 3)
            if lo is None or hi is None or not (-1000000 <= lo <= 1000000 and -1000000 <= hi <= 1000000):
                self.melde('%s: Schieberwerte %r %r ausserhalb +-1000000' % (name, lo, hi))
            elif hi <= lo:
                self.melde('%s: Schieber ohne Spanne (%d bis %d)' % (name, lo, hi))
        elif w in ('TEXT', 'TOOLTIPTEXT'):
            i = abschnitt.find('"')
            rest = abschnitt[i + 1:] if i >= 0 else ''
            wert = re.split(r'[\n\r\t"]', rest, maxsplit=1)[0] if rest else ''
            if not wert:
                self.leere_texte += 1

    # ---------------------------------------------------------------- Datei
    def datei(self, daten):
        t = Text(daten)
        # FILE_VERSION = n
        t.p = min(len('FILE_VERSION = '), len(daten))
        version = 0
        m = re.match(rb'\s*(-?\d+)', daten[t.p:t.p + 16])
        if m:
            version = int(m.group(1))
        t.zeilenende()
        if version >= 2:
            if t.wort() != 'STARTLAYOUTBLOCK':
                self.melde('Layoutblock fehlt')
                return
            while True:
                w = t.wort()
                if w is None:
                    self.melde('Datei endet vor ENDLAYOUTBLOCK')
                    return
                if w == 'ENDLAYOUTBLOCK':
                    break
                if w in ('LAYOUTINIT', 'LAYOUTUPDATE', 'LAYOUTSHUTDOWN'):
                    t.abschnitt()
        while True:
            w = t.wort()
            if w is None:
                return
            if w == 'END':
                continue
            if w in STANDARDFARBEN:
                t.wort()
                t.abschnitt()
            elif w == 'WINDOW':
                if not self.fenster_lesen(t, 0, self.name):
                    return


def main(argv):
    argv = list(argv)
    ziel = None
    if '--auspacken' in argv:
        i = argv.index('--auspacken')
        ziel = argv[i + 1]
        del argv[i:i + 2]
    ordner = argv[1:] or ASSET_ORDNER
    if not ordner:
        sys.exit('keine Asset-Ordner: als Argumente angeben oder GZH_ASSETS_GENERALS/GZH_ASSETS_ZH setzen')
    gesehen = {}
    for o in ordner:
        for pfad in sorted(set(__import__('glob').glob(os.path.join(o, '*.big')) + __import__('glob').glob(os.path.join(o, '*', '*.big')))):
            try:
                eintraege = big_eintraege(pfad)
            except (OSError, ValueError, struct.error):
                continue
            with open(pfad, 'rb') as f:
                for name, off, size in eintraege:
                    if name.endswith('.wnd') and name not in gesehen:
                        f.seek(off)
                        gesehen[name] = f.read(size)
    if ziel:
        for name, daten in gesehen.items():
            pfad = os.path.join(ziel, *name.split('\\'))
            os.makedirs(os.path.dirname(pfad), exist_ok=True)
            with open(pfad, 'wb') as f:
                f.write(daten)
    dateien = 0
    fenster = 0
    tiefe = 0
    leer = 0
    alle = []
    for name, daten in sorted(gesehen.items()):
        p = Pruefung(name)
        p.datei(daten)
        dateien += 1
        fenster += p.fenster
        tiefe = max(tiefe, p.tiefe_max)
        leer += p.leere_texte
        for m in p.meldungen:
            alle.append('%s: %s' % (name, m))
    for m in alle:
        print(m)
    print('%d Layouts, %d Fenster, groesste Schachtelung %d, %d leere TEXT-Felder, %d Abweichungen' % (dateien, fenster, tiefe, leer, len(alle)))
    return 1 if alle else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
