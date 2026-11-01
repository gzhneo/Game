#!/usr/bin/env python3
# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
"""Prueft die erzeugten Daten mit den echten Parsern der Engine, ohne GZHNeoZH.exe.

    python3 tools/wine/testdaten/pruefen.py [Datenordner]

Ohne Datenordner wird ein frischer erzeugt (Ablage neben der Fuzz-Ablage). Die INI-Dateien der
Spiellogik werden in der Ladereihenfolge der Engine zu einer Datei verkettet und mit dem
Fuzz-Harness (tools/fuzz/engine, IniFuzz --run) geladen; jede Karte geht durch MapFuzz --run.
Der Harness legt die Datenspeicher der Spiellogik an; Oberflaeche, Ton und Fenster prueft er
nicht (Blocktypen, die er nicht kennt, ueberspringt er).

    ENGINEFUZZ_DIR=<Ordner>   Ablage des Harness (Standard ${TMPDIR:-/tmp}/cnc-enginefuzz)
    HARNESS_VERBOSE=1         Debug-Log und jede Assertion ausgeben (hier gesetzt)

Rueckgabewert 0: alles geladen, sonst der erste Fehler.
"""
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..', '..'))
sys.path.insert(0, HERE)

# Ladereihenfolge der Engine (GameEngine::init); Blocktypen ohne Speicher im Harness fehlen hier.
ORDER = [
    'Data/INI/Default/GameData.ini',
    'Data/INI/Science.ini',
    'Data/INI/PlayerTemplate.ini',
    'Data/INI/FXList.ini',
    'Data/INI/Weapon.ini',
    'Data/INI/ObjectCreationList.ini',
    'Data/INI/Locomotor.ini',
    'Data/INI/SpecialPower.ini',
    'Data/INI/DamageFX.ini',
    'Data/INI/Armor.ini',
    'Data/INI/Default/Object.ini',
    'Data/INI/Object/TestObjects.ini',
    'Data/INI/Upgrade.ini',
    'Data/INI/AIData.ini',
    'Data/INI/Crate.ini',
]


# Asked for by the code but absent on purpose: the debug button is created by the code itself; the local
# general's portrait, name and traits exist only in the campaign layout (the load screen then takes the
# faction image, LoadScreen.cpp).
ABSENT_ON_PURPOSE = {'mainmenu.wnd:DebugButtonLoadMap', 'multiplayerloadscreen.wnd:LocalGeneralFeatures',
                     'multiplayerloadscreen.wnd:LocalGeneralName', 'multiplayerloadscreen.wnd:LocalGeneralPortrait'}


def strip_labels(text):
    """The harness has no game text (TheGameText is null); fields that translate a label at parse time
    (DisplayName, RankName, Description) would crash it and are dropped before loading."""
    import re
    return re.sub(r'(?m)^\s*(DisplayName|RankName|Description)\s*=.*$', '', text)


def check_rects():
    """SCREENRECT is absolute, also for children: a child outside its parent is usually written relative to the
    parent by mistake. Returns "file: child outside parent" lines."""
    import re
    import gen_wnd
    out = []
    for fn, text in sorted(gen_wnd.window_files().items()):
        stack, last, upper = [], None, None
        for line in text.splitlines():
            line = line.strip()
            m = re.match(r'SCREENRECT = UPPERLEFT: (-?\d+) (-?\d+),', line)
            if m:
                upper = (int(m.group(1)), int(m.group(2)))
                continue
            m = re.match(r'BOTTOMRIGHT: (-?\d+) (-?\d+),', line)
            if m and upper:
                r = upper + (int(m.group(1)), int(m.group(2)))
                upper = None
                p = stack[-1] if stack else None
                if p and (r[0] < p[0] or r[1] < p[1] or r[2] > p[2] or r[3] > p[3]):
                    out.append('%s: %s ausserhalb %s' % (fn, r, p))
                last = r
            elif line == 'CHILD':
                stack.append(last)
            elif line == 'ENDALLCHILDREN':
                stack.pop()
    return out


def check_windows(data):
    """Every "File.wnd:Name" the engine code asks for must exist in the generated file of that name
    (files the data set does not generate are skipped). Format names (%d, %02d) must match at least
    one window. Returns the list of missing names."""
    import re
    have = {}
    wnd_dir = os.path.join(data, 'Window')
    for base, _dirs, files in os.walk(wnd_dir):
        for name in files:
            with open(os.path.join(base, name), 'rb') as f:
                text = f.read().decode('latin-1')
            have[name.lower()] = set(re.findall(r'NAME = "[^:"]+:([^"]+)"', text))
    asked = {}
    for top in ('GameEngine', 'Platform'):
        for base, _dirs, files in os.walk(os.path.join(ROOT, 'Code', top)):
            for name in files:
                if not name.endswith('.cpp'):
                    continue
                with open(os.path.join(base, name), 'rb') as f:
                    text = re.sub(r'//[^\n]*', '', f.read().decode('latin-1'))
                for m in re.finditer(r'"([A-Za-z0-9_]+\.wnd):([A-Za-z0-9_%]+)"', text):
                    asked.setdefault(m.group(1).lower(), set()).add(m.group(2))
    missing = []
    for wnd, names in sorted(asked.items()):
        if wnd not in have:
            continue
        for n in sorted(names):
            if '%' in n:
                pattern = re.compile('^' + re.sub(r'%0?\d*d', r'\\d+', re.escape(n).replace('\\%', '%')) + '$')
                ok = any(pattern.match(h) for h in have[wnd])
            else:
                ok = n in have[wnd]
            if not ok and '%s:%s' % (wnd, n) not in ABSENT_ON_PURPOSE:
                missing.append('%s:%s' % (wnd, n))
    return missing


def run(args, env):
    p = subprocess.run(args, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    return p.returncode, p.stdout


def main():
    env = dict(os.environ)
    env.setdefault('ENGINEFUZZ_DIR', os.path.join(os.environ.get('TMPDIR', '/tmp'), 'cnc-enginefuzz'))
    env['HARNESS_VERBOSE'] = '1'
    if len(sys.argv) > 1:
        data = sys.argv[1]
    else:
        import generate
        data = tempfile.mkdtemp(prefix='cnc-testdaten-')
        generate.build(data)
    text = []
    for rel in ORDER:
        with open(os.path.join(data, *rel.split('/')), 'rb') as f:
            text.append(strip_labels(f.read().decode('latin-1')))
    work = tempfile.mkdtemp(prefix='cnc-harness-')
    os.makedirs(os.path.join(work, 'seeds', 'base'))
    os.makedirs(os.path.join(work, 'Data', 'INI'))
    os.makedirs(os.path.join(work, 'Data', 'English'))
    for stub in ('ParticleSystem.ini', 'Animation2D.ini'):
        open(os.path.join(work, 'Data', 'INI', stub), 'wb').close()
    with open(os.path.join(ROOT, 'tools', 'fuzz', 'seeds', 'text', 'small.csf'), 'rb') as f:
        csf = f.read()
    with open(os.path.join(work, 'Data', 'English', 'Generals.csf'), 'wb') as f:
        f.write(csf)
    # The logic INIs go in as base data: override mode (a map.ini) rejects modules without AddModule.
    with open(os.path.join(work, 'seeds', 'base', 'logik.ini'), 'wb') as f:
        f.write('\r\n'.join(text).encode('latin-1'))
    with open(os.path.join(work, 'empty.ini'), 'wb') as f:
        f.write(b'; empty\r\n')
    fuzz = env['ENGINEFUZZ_DIR']
    env.setdefault('WINEPREFIX', os.path.expanduser('~/.wine-cnc'))
    env.setdefault('WINEDEBUG', '-all')
    wine = env.get('WINE') or shutil.which('wine64') or '/usr/lib/wine/wine64'

    def harness(exe, name):
        p = subprocess.run([wine, os.path.join(fuzz, exe), '--run', 'seeds', name], cwd=work, env=env,
                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        return p.returncode, p.stdout

    failed = 0
    missing = check_windows(data)
    print('=== Fenster: %d Namen aus dem Code fehlen in den erzeugten Layouts' % len(missing))
    for m in missing:
        print('  fehlt: ' + m)
    failed |= bool(missing)
    outside = check_rects()
    print('=== Fenster: %d Kindfenster ausserhalb ihres Elternfensters' % len(outside))
    for m in outside:
        print('  ' + m)
    failed |= bool(outside)
    rc, out = harness('IniFuzz.exe', 'empty.ini')
    print('=== INI der Spiellogik (Rueckgabe %d)' % rc)
    print(out)
    failed |= rc != 0 or 'loaded, 0 assertions' not in out
    for root, _dirs, files in os.walk(os.path.join(data, 'Maps')):
        for name in sorted(files):
            if name.lower().endswith('.map'):
                rc, out = harness('MapFuzz.exe', os.path.join(root, name))
                print('=== Karte %s (Rueckgabe %d)' % (name, rc))
                print(out)
                failed |= rc != 0
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
