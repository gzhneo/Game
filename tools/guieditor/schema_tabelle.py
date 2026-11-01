#!/usr/bin/env python3
# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
"""Erzeugt die Tabelle der Layout-Schemata des GUI-Editors (Code/Tools/Editors/GuiEditor/Core/WndSchemeTable.h).

Ein Schema des alten GUIEdit (.ls) hat je Zustand (StateIdentifier in GUIEdit/Include/Properties.h) ein Bild,
eine Farbe und eine Randfarbe. LayoutScheme::applyPropertyTablesToWindow (GUIEdit/Source/LayoutScheme.cpp)
setzt sie je Fenstertyp mit den Gadget-Funktionen der Engine (GameClient/Gadget*.h), die in Index n der
Zeichendaten eines Fensters oder eines seiner Teile schreiben. Das Skript liest alle drei Quellen und schreibt,
welcher Zustand bei welchem Fenstertyp in welches .wnd-Feld (wie Save.cpp es nennt) und welchen Index geht.

Aufruf: python3 tools/guieditor/schema_tabelle.py [--pruefen]
  --pruefen  nur vergleichen, Rueckgabewert 1, wenn die Tabelle nicht mehr zum Code passt
"""
import os
import re
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
PROPERTIES = os.path.join(ROOT, 'Code', 'Tools', 'GUIEdit', 'Include', 'Properties.h')
SCHEME = os.path.join(ROOT, 'Code', 'Tools', 'GUIEdit', 'Source', 'LayoutScheme.cpp')
GADGETS = os.path.join(ROOT, 'Code', 'GameEngine', 'GameClient')
OUT = os.path.join(ROOT, 'Code', 'Tools', 'Editors', 'GuiEditor', 'Core', 'WndSchemeTable.h')

STYLES = {
    'GWS_PUSH_BUTTON': 'PUSHBUTTON', 'GWS_RADIO_BUTTON': 'RADIOBUTTON', 'GWS_CHECK_BOX': 'CHECKBOX',
    'GWS_VERT_SLIDER': 'VERTSLIDER', 'GWS_HORZ_SLIDER': 'HORZSLIDER', 'GWS_SCROLL_LISTBOX': 'SCROLLLISTBOX',
    'GWS_COMBO_BOX': 'COMBOBOX', 'GWS_ENTRY_FIELD': 'ENTRYFIELD', 'GWS_STATIC_TEXT': 'STATICTEXT',
    'GWS_PROGRESS_BAR': 'PROGRESSBAR', 'GWS_TAB_CONTROL': 'TABCONTROL',
}
# Parts of a gadget as the scheme code reaches them.
GETTERS = {
    'GadgetListBoxGetUpButton': 'up', 'GadgetListBoxGetDownButton': 'down', 'GadgetListBoxGetSlider': 'slider',
    'GadgetComboBoxGetDropDownButton': 'dropdown', 'GadgetComboBoxGetEditBox': 'editbox',
    'GadgetComboBoxGetListBox': 'listbox',
}
# The .wnd field of a part's draw data (Save.cpp), %s is ENABLED, DISABLED or HILITE.
FIELDS = {
    (): '%sDRAWDATA',
    ('child',): 'SLIDERTHUMB%sDRAWDATA',
    ('up',): 'LISTBOX%sUPBUTTONDRAWDATA', ('down',): 'LISTBOX%sDOWNBUTTONDRAWDATA', ('slider',): 'LISTBOX%sSLIDERDRAWDATA',
    ('slider', 'child'): 'SLIDERTHUMB%sDRAWDATA',
    ('dropdown',): 'COMBOBOXDROPDOWNBUTTON%sDRAWDATA', ('editbox',): 'COMBOBOXEDITBOX%sDRAWDATA',
    ('listbox',): 'COMBOBOXLISTBOX%sDRAWDATA', ('listbox', 'up'): 'LISTBOX%sUPBUTTONDRAWDATA',
    ('listbox', 'down'): 'LISTBOX%sDOWNBUTTONDRAWDATA', ('listbox', 'slider'): 'LISTBOX%sSLIDERDRAWDATA',
    ('listbox', 'slider', 'child'): 'SLIDERTHUMB%sDRAWDATA',
}
KINDS = {'Image': 'i', 'Color': 'c', 'BorderColor': 'b'}


TAG = 'GUIEdit_Letzter_Stand'


def read(path):
    """Die Datei; die Quellen des alten GUIEdit kommen aus dem Tag, seit der Ordner entfernt ist."""
    if os.path.exists(path):
        with open(path, encoding='latin-1') as f:
            return f.read()
    rel = os.path.relpath(path, ROOT).replace(os.sep, '/')
    try:
        data = subprocess.run(['git', '-C', ROOT, 'show', '%s:%s' % (TAG, rel)], check=True, capture_output=True).stdout
    except (OSError, subprocess.CalledProcessError):
        sys.exit('%s fehlt; die Quellen von GUIEdit liegen im Tag %s (git fetch origin tag %s)' % (rel, TAG, TAG))
    return data.decode('latin-1').replace('\r\n', '\n')


def states():
    text = read(PROPERTIES)
    body = text[:text.index('NUM_STATE_IDENTIFIERS')]
    body = body[body.rindex('{') + 1:]
    body = re.sub(r'//[^\n]*', '', body)
    # The names by their value; of two names with one value (FIRST_VALID_IDENTIFIER, BUTTON_ENABLED) the later.
    values = {}
    n = 0
    for item in (i.strip() for i in body.split(',')):
        if not item:
            continue
        name, _, value = (x.strip() for x in item.partition('='))
        if value:
            n = int(value) if value.isdigit() else values[value]
        values[name] = n
        n += 1
    names = [None] * n
    for name, value in values.items():
        names[value] = name
    return names


def constants():
    """Enum values of the gadget headers (GTC_TAB_0 = 1, ...)."""
    values = {}
    for name in os.listdir(GADGETS):
        if not name.startswith('Gadget') or not name.endswith('.h'):
            continue
        for block in re.findall(r'enum\s*\w*\s*\{([^}]*)\}', read(os.path.join(GADGETS, name))):
            n = 0
            for item in re.sub(r'//[^\n]*', '', block).split(','):
                item = item.strip()
                if not item:
                    continue
                if '=' in item:
                    key, value = (s.strip() for s in item.split('=', 1))
                    n = int(value, 0) if re.fullmatch(r'-?\w+', value) and value.lstrip('-').isdigit() else values.get(value, n)
                else:
                    key = item
                values[key] = n
                n += 1
    return values


def setters(consts):
    """Gadget setter name -> list of (part path, state, kind, index)."""
    found = {}
    pattern = re.compile(r'inline\s+void\s+(Gadget\w+)\s*\(\s*GameWindow\s*\*\s*(\w+)[^)]*\)\s*\{', re.S)
    for name in os.listdir(GADGETS):
        if not name.startswith('Gadget') or not name.endswith('.h'):
            continue
        text = read(os.path.join(GADGETS, name))
        for m in pattern.finditer(text):
            depth, i = 1, m.end()
            while depth and i < len(text):
                depth += {'{': 1, '}': -1}.get(text[i], 0)
                i += 1
            found[m.group(1)] = (m.group(2), text[m.end():i - 1])
    resolved = {}

    def resolve(fn):
        if fn in resolved:
            return resolved[fn]
        arg, body = found[fn]
        out = []
        for m in re.finditer(r'\b' + arg + r'->winSet(Enabled|Disabled|Hilite)(Image|Color|BorderColor)\(\s*(\w+)\s*,\s*(\w+)\s*\)', body):
            if m.group(4) in ('nullptr', 'NULL', '0'):
                continue  # clearing another slot, not where the scheme's value goes
            index = int(m.group(3)) if m.group(3).isdigit() else consts[m.group(3)]
            out.append(((), m.group(1).upper(), KINDS[m.group(2)], index))
        children = set(re.findall(r'GameWindow\s*\*\s*(\w+)\s*=\s*' + arg + r'->winGetChild\(\)', body))
        for var in children:
            for m in re.finditer(r'\b(Gadget\w+)\(\s*' + var + r'\s*,', body):
                out += [(('child',) + p, s, k, x) for p, s, k, x in resolve(m.group(1))]
        resolved[fn] = out
        return out

    return {fn: resolve(fn) for fn in found}


def table():
    names = states()
    consts = constants()
    gadget = setters(consts)
    text = read(SCHEME)
    start = text.index('void LayoutScheme::applyPropertyTablesToWindow')
    body = text[start:text.index('applyPropertyTablesToWindow( root->winGetChild() )', start)]
    slots = []
    blocks = re.split(r'(BitTest\(\s*root->winGetStyle\(\),\s*\w+\s*\)|\n\telse\n)', body)
    window = None
    paths = {'root': ()}
    state = None
    for piece in blocks:
        m = re.match(r'BitTest\(\s*root->winGetStyle\(\),\s*(\w+)\s*\)', piece)
        if m:
            window = STYLES[m.group(1)]
            continue
        if piece == '\n\telse\n':
            window = 'GENERIC'
            continue
        if window is None:
            continue
        for line in piece.split('\n'):
            g = re.search(r'GameWindow\s*\*\s*(\w+)\s*=\s*(\w+)\(\s*(\w+)\s*\)', line)
            if g:
                paths[g.group(1)] = paths[g.group(3)] + (GETTERS[g.group(2)],)
                continue
            s = re.search(r'GetStateInfo\(\s*(\w+)\s*\)', line)
            if s:
                state = s.group(1)
                continue
            c = re.search(r'\b(Gadget\w+)\(\s*(\w+)\s*,\s*info->(image|color|borderColor)', line)
            d = re.search(r'\b(\w+)->winSet(Enabled|Disabled|Hilite)(Image|Color|BorderColor)\(\s*(\d+)\s*,\s*info->', line)
            if c and state:
                for path, st, kind, index in gadget[c.group(1)]:
                    slots.append((window, state, paths[c.group(2)] + path, st, kind, index))
            elif d and state:
                slots.append((window, state, paths[d.group(1)], d.group(2).upper(), KINDS[d.group(3)], int(d.group(4))))
    rows = []
    for window, state, path, st, kind, index in slots:
        # GUIEdit sets the highlighted images of the vertical slider into the disabled ones (a slip in
        # LayoutScheme.cpp); the name of the state says what is meant.
        if '_HILITE' in state and st == 'DISABLED':
            st = 'HILITE'
        rows.append((window, names.index(state), FIELDS[path] % st, index, kind))
    return names, rows


def header(names, rows):
    lines = [
        '/*',
        '**\tCommand & Conquer Generals(tm) / Generals Zero Hour(tm)',
        '**\tCopyright 2025 Electronic Arts Inc.',
        '**\tModified by GZH Neo, Copyright (C) 2026 GZH Neo',
        '**',
        '**\tThis program is free software: you can redistribute it and/or modify',
        '**\tit under the terms of the GNU General Public License as published by',
        '**\tthe Free Software Foundation, either version 3 of the License, or',
        '**\t(at your option) any later version.',
        '**',
        '**\tThis program is distributed in the hope that it will be useful,',
        '**\tbut WITHOUT ANY WARRANTY; without even the implied warranty of',
        '**\tMERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the',
        '**\tGNU General Public License for more details.',
        '**',
        '**\tYou should have received a copy of the GNU General Public License',
        '**\talong with this program.  If not, see <http://www.gnu.org/licenses/>.',
        '*/',
        '',
        '// WndSchemeTable.h -- where the states of a layout scheme (.ls) of the old GUIEdit go in a .wnd file.',
        '// Generated by tools/guieditor/schema_tabelle.py from GUIEdit\'s LayoutScheme.cpp and Properties.h and the',
        '// gadget headers of the engine; do not edit.',
        '',
        '#pragma once',
        '',
        'namespace WndModel',
        '{',
        '',
        '// The states in the order of the scheme file.',
        'inline constexpr const char *SCHEME_STATES[] = {',
    ]
    lines += ['\t"%s",' % n for n in names]
    lines += [
        '};',
        '',
        '// A value of a state goes into entry index of the draw data field of every window of the type (GENERIC:',
        '// every window that is no gadget); part: i image, c colour, b border colour.',
        'struct SchemeSlot',
        '{',
        '\tconst char *type;',
        '\tint state;',
        '\tconst char *field;',
        '\tint index;',
        '\tchar part;',
        '};',
        '',
        'inline constexpr SchemeSlot SCHEME_SLOTS[] = {',
    ]
    lines += ['\t{"%s", %d, "%s", %d, \'%s\'},' % r for r in rows]
    lines += ['};', '', '} // namespace WndModel', '']
    return '\n'.join(lines)


def main():
    names, rows = table()
    text = header(names, rows)
    if '--pruefen' in sys.argv:
        current = read(OUT) if os.path.exists(OUT) else ''
        same = current == text
        print('Schema-Tabelle %s (%d Zustaende, %d Eintraege)' % ('aktuell' if same else 'VERALTET', len(names), len(rows)))
        return 0 if same else 1
    with open(OUT, 'w', encoding='latin-1', newline='') as f:
        f.write(text)
    used = {r[1] for r in rows}
    print('%d Zustaende, %d Eintraege, %d Zustaende ohne Ziel: %s' % (len(names), len(rows), len(names) - len(used),
          ', '.join(n for i, n in enumerate(names) if i not in used)))
    return 0


if __name__ == '__main__':
    sys.exit(main())
