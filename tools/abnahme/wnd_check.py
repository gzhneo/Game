#!/usr/bin/env python3
# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
"""Check all window layouts (.wnd) of the .big archives against the rules of the layout reader.

Reads every .wnd file like GameWindowManagerScript.cpp (words, sections up to ';', WINDOW/CHILD/END) and reports:
  - files that break off before their end (the reader used to hang in them),
  - data fields that do not fit the window type (LISTBOXDATA in a STATICTEXT etc.; the reader ignores them now),
  - values outside the limits the reader clamps them to (list length, columns, text length, tabs, sliders, rects),
  - the greatest nesting depth of child windows (the reader aborts at too deep a nesting).
Expected: none of this in EA's files; the limits then change no valid layout.

Usage:
    python tools/abnahme/wnd_check.py [--extract <folder>] [asset folder ...]
        (default: the two asset folders of the project). With --extract the layouts are written as files into the
        folder, e.g. for the round trip of the GUI editor: WndTool --roundtrip <folder>.
Exit 0 = nothing found, 1 = deviations.
"""
import os
import re
import struct
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
import pfade  # noqa: E402
ASSET_FOLDERS = pfade.alle_assets()
STACK_DEPTH = 9          # WIN_STACK_DEPTH - 1 in the reader
NUM_TAB_PANES = 16

# data fields per window type (as Engine::wndFieldFitsType)
DATA_FIELDS = {
    'LISTBOXDATA': ('SCROLLLISTBOX',),
    'COMBOBOXDATA': ('COMBOBOX',),
    'SLIDERDATA': ('VERTSLIDER', 'HORZSLIDER'),
    'RADIOBUTTONDATA': ('RADIOBUTTON',),
    'STATICTEXTDATA': ('STATICTEXT',),
    'TEXTENTRYDATA': ('ENTRYFIELD',),
    'TABCONTROLDATA': ('TABCONTROL',),
}
STANDARD_COLORS = ('ENABLEDCOLOR', 'DISABLEDCOLOR', 'HILITECOLOR', 'SELECTEDCOLOR', 'TEXTCOLOR', 'BACKGROUNDCOLOR', 'FONT')


def big_entries(path):
    """Directory of a .big archive: [(name in lower case, offset, size)]."""
    with open(path, 'rb') as f:
        head = f.read(16)
        if head[:4] not in (b'BIGF', b'BIG4'):
            return []
        n = struct.unpack('>I', head[8:12])[0]
        first = struct.unpack('>I', head[12:16])[0]
        directory = f.read(max(0, first - 16))
    out = []
    off = 0
    for _ in range(n):
        o, s = struct.unpack('>II', directory[off:off + 8])
        off += 8
        e = directory.index(b'\0', off)
        out.append((directory[off:e].decode('latin-1').lower().replace('/', '\\'), o, s))
        off = e + 1
    return out


class Text:
    """A layout in memory, read like RAMFile: words and sections."""

    def __init__(self, data):
        self.d = data
        self.p = 0

    def word(self):
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

    def section(self):
        """As readUntilSemicolon: leading white space dropped, white space becomes a blank, up to ';' or the end of the file."""
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

    def line_end(self):
        d = self.d
        while self.p < len(d) and d[self.p:self.p + 1] != b'\n':
            self.p += 1
        if self.p < len(d):
            self.p += 1


def numbers(section, trenner=' :,\n\r\t'):
    return [t for t in re.split('[' + re.escape(trenner) + ']+', section) if t]


def number(t, i):
    try:
        return int(t[i])
    except (IndexError, ValueError):
        return None


class Check:
    def __init__(self, name):
        self.name = name
        self.messages = []
        self.windows = 0
        self.depth_max = 0
        self.empty_texts = 0

    def report(self, text):
        self.messages.append(text)

    # ---------------------------------------------------------------- Windows
    def read_window(self, t, depth, window_name):
        """parseWindow: reads up to END; returns False when the file ends before."""
        self.windows += 1
        self.depth_max = max(self.depth_max, depth)
        typ = None
        head = t.section()                       # WINDOWTYPE = X
        tk = numbers(head, ' =;\n\r\t')
        if len(tk) >= 2 and tk[0] == 'WINDOWTYPE':
            typ = tk[1]
        else:
            self.report('%s: first section of a window is no WINDOWTYPE: %r' % (window_name, head[:40]))
        rect = numbers(t.section(), ' ,:=\n\r\t')   # SCREENRECT = ...
        if len(rect) >= 10 and rect[0] == 'SCREENRECT':
            werte = {'links': number(rect, 2), 'oben': number(rect, 3), 'rechts': number(rect, 5), 'unten': number(rect, 6),
                     'aufloesung x': number(rect, 8), 'aufloesung y': number(rect, 9)}
            for k, v in werte.items():
                if v is None:
                    self.report('%s: SCREENRECT without a number (%s)' % (window_name, k))
                elif k.startswith('aufl'):
                    if not 0 <= v <= 65535:
                        self.report('%s: SCREENRECT %s %d outside 0..65535' % (window_name, k, v))
                elif not -32768 <= v <= 32767:
                    self.report('%s: SCREENRECT %s %d outside -32768..32767' % (window_name, k, v))
        else:
            self.report('%s: second section of a window is no SCREENRECT: %r' % (window_name, ' '.join(rect[:3])))
        name = window_name
        while True:
            w = t.word()
            if w is None:
                self.report('%s: file ends before END (window %s)' % (window_name, name))
                return False
            if w == 'END':
                return True
            if w == 'CHILD':
                if depth + 1 > STACK_DEPTH:
                    self.report('%s: child window with depth %d (limit of the reader %d)' % (window_name, depth + 1, STACK_DEPTH))
                if not self.read_children(t, depth + 1, window_name):
                    return False
                continue
            if w == 'DATA':
                t.word()
                t.section()
                continue
            # a field: eat '=' and the chunk; an unknown word eats a chunk too (the reader does that)
            t.word()
            section = t.section()
            if w == 'NAME':
                m = re.search(r'"([^"]*)"', section)
                if m:
                    name = m.group(1)
            self.feld(w, section, typ, name)

    def read_children(self, t, depth, window_name):
        """parseChildWindows: standard colours, WINDOW, up to END or ENDALLCHILDREN."""
        while True:
            w = t.word()
            if w is None:
                return True                       # the reader breaks at the end of the file
            if w in ('END', 'ENDALLCHILDREN'):
                return True
            if w in STANDARD_COLORS:
                t.word()
                t.section()
            elif w == 'WINDOW':
                if not self.read_window(t, depth, window_name):
                    return False

    # ---------------------------------------------------------------- Felder
    def feld(self, w, section, typ, name):
        if w in DATA_FIELDS and typ not in DATA_FIELDS[w]:
            self.report('%s (%s): field %s does not belong to this window type' % (name, typ, w))
        t = numbers(section)
        if w == 'LISTBOXDATA':
            length = number(t, 1)
            if length is None or not 1 <= length <= 32767:
                self.report('%s: list length %r outside 1..32767' % (name, length))
            if 'COLUMNS' in t:
                columns = number(t, t.index('COLUMNS') + 1)
                if columns is None or not 1 <= columns <= 64:
                    self.report('%s: columns %r outside 1..64' % (name, columns))
        elif w == 'TEXTENTRYDATA':
            length = number(t, 1)
            if length is None or not 0 <= length <= 32767:
                self.report('%s: text length %r outside 0..32767' % (name, length))
        elif w == 'COMBOBOXDATA':
            for i, bez in ((3, 'maxChars'), (5, 'maxDisplay')):
                v = number(t, i)
                if v is None or not 0 <= v <= 32767:
                    self.report('%s: combo box %s %r outside 0..32767' % (name, bez, v))
        elif w == 'TABCONTROLDATA':
            count = number(t, 9)
            if count is None or not 0 <= count <= NUM_TAB_PANES:
                self.report('%s: tabs %r outside 0..%d' % (name, count, NUM_TAB_PANES))
            for i, bez in ((5, 'Tabbreite'), (7, 'Tabhoehe'), (11, 'Rand')):
                v = number(t, i)
                if v is None or not 0 <= v <= 65535:
                    self.report('%s: %s %r outside 0..65535' % (name, bez, v))
        elif w == 'SLIDERDATA':
            lo, hi = number(t, 1), number(t, 3)
            if lo is None or hi is None or not (-1000000 <= lo <= 1000000 and -1000000 <= hi <= 1000000):
                self.report('%s: slider values %r %r outside +-1000000' % (name, lo, hi))
            elif hi <= lo:
                self.report('%s: slider without a span (%d to %d)' % (name, lo, hi))
        elif w in ('TEXT', 'TOOLTIPTEXT'):
            i = section.find('"')
            rest = section[i + 1:] if i >= 0 else ''
            value = re.split(r'[\n\r\t"]', rest, maxsplit=1)[0] if rest else ''
            if not value:
                self.empty_texts += 1

    # ---------------------------------------------------------------- File
    def datei(self, data):
        t = Text(data)
        # FILE_VERSION = n
        t.p = min(len('FILE_VERSION = '), len(data))
        version = 0
        m = re.match(rb'\s*(-?\d+)', data[t.p:t.p + 16])
        if m:
            version = int(m.group(1))
        t.line_end()
        if version >= 2:
            if t.word() != 'STARTLAYOUTBLOCK':
                self.report('layout block missing')
                return
            while True:
                w = t.word()
                if w is None:
                    self.report('file ends before ENDLAYOUTBLOCK')
                    return
                if w == 'ENDLAYOUTBLOCK':
                    break
                if w in ('LAYOUTINIT', 'LAYOUTUPDATE', 'LAYOUTSHUTDOWN'):
                    t.section()
        while True:
            w = t.word()
            if w is None:
                return
            if w == 'END':
                continue
            if w in STANDARD_COLORS:
                t.word()
                t.section()
            elif w == 'WINDOW':
                if not self.read_window(t, 0, self.name):
                    return


def main(argv):
    argv = list(argv)
    target = None
    if '--extract' in argv:
        i = argv.index('--extract')
        target = argv[i + 1]
        del argv[i:i + 2]
    folders = argv[1:] or ASSET_FOLDERS
    if not folders:
        sys.exit('no asset folders: give them as arguments or set GZH_ASSETS_GENERALS/GZH_ASSETS_ZH')
    seen = {}
    for o in folders:
        for path in sorted(set(__import__('glob').glob(os.path.join(o, '*.big')) + __import__('glob').glob(os.path.join(o, '*', '*.big')))):
            try:
                entries = big_entries(path)
            except (OSError, ValueError, struct.error):
                continue
            with open(path, 'rb') as f:
                for name, off, size in entries:
                    if name.endswith('.wnd') and name not in seen:
                        f.seek(off)
                        seen[name] = f.read(size)
    if target:
        for name, data in seen.items():
            path = os.path.join(target, *name.split('\\'))
            os.makedirs(os.path.dirname(path), exist_ok=True)
            with open(path, 'wb') as f:
                f.write(data)
    files = 0
    windows = 0
    depth = 0
    empty = 0
    all_messages = []
    for name, data in sorted(seen.items()):
        p = Check(name)
        p.datei(data)
        files += 1
        windows += p.windows
        depth = max(depth, p.depth_max)
        empty += p.empty_texts
        for m in p.messages:
            all_messages.append('%s: %s' % (name, m))
    for m in all_messages:
        print(m)
    print('%d layouts, %d windows, greatest nesting %d, %d empty TEXT fields, %d deviations' % (files, windows, depth, empty, len(all_messages)))
    return 1 if all_messages else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
