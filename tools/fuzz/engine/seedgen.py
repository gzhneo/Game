#!/usr/bin/env python3
# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
"""Seeds fuer den INI-Harness: ein Objekt je registriertem Modul mit allen Feldern des Moduls.

Liest aus Code/ die Module, die ModuleFactory::init und W3DModuleFactory::init registrieren, sucht je Modul die
ModuleData-Klasse, deren buildFieldParse (samt Basisklassen) und die Feldnamen mit ihrem
Parser. Jede Feldzeile wird als Kandidat geschrieben ("?" am Zeilenanfang); der Harness nimmt
beim Start nur die Kandidaten auf, mit denen die Datei weiter sauber laedt. Ein falsch geratener
Wert kostet also nur das eine Feld.

Aufruf: seedgen.py <Code-Verzeichnis> <Zielordner>"""
import os, re, sys

# Werte je Parser; die Namen verweisen auf die Testdaten in seeds/base/base.ini.
VALUES = {
    'parseReal': ['1.5'], 'parsePositiveNonZeroReal': ['1.5'], 'parseDurationReal': ['1000'],
    'parseVelocityReal': ['20'], 'parseAccelerationReal': ['20'], 'parseAngleReal': ['45'],
    'parseAngularVelocityReal': ['90'], 'parsePercentToReal': ['50%'],
    'parseInt': ['2'], 'parseUnsignedInt': ['2'], 'parseDurationUnsignedInt': ['1000'],
    'parseUnsignedByte': ['2'], 'parseByte': ['2'], 'parseShort': ['2'], 'parseUnsignedShort': ['2'],
    'parseBool': ['Yes'], 'parseAsciiString': ['TestTank'], 'parseQuotedAsciiString': ['"TestTank"'],
    'parseAsciiStringVector': ['TestTank TestProjectile'], 'parseAsciiStringVectorAppend': ['TestTank'],
    'parseCoord3D': ['X:1 Y:2 Z:3'], 'parseCoord2D': ['X:1 Y:2'], 'parseICoord2D': ['X:1 Y:2'],
    'parseRGBColor': ['R:10 G:20 B:30'], 'parseRGBAColorInt': ['R:10 G:20 B:30 A:40'],
    'parseWeaponTemplate': ['TestGun'], 'parseObjectCreationList': ['OCL_Test'],
    'parseFXList': ['FX_Test'], 'parseScience': ['SCIENCE_Test'], 'parseUpgradeTemplate': ['Upgrade_Test'],
    'parseSpecialPowerTemplate': ['SuperweaponTest'], 'parseArmorTemplate': ['TestArmor'],
    'parseThingTemplate': ['TestTank'], 'parseAudioEventRTS': ['TestSound'],
    'parseDynamicAudioEventRTS': ['TestSound'], 'parseParticleSystemTemplate': ['TestParticles'],
    'parseParticleSystem': ['TestParticles'], 'parseDamageTypeFlags': ['ALL -EXPLOSION'],
    'parseDeathTypeFlags': ['ALL'], 'parseVeterancyLevelFlags': ['ALL'],
    'parseAndTranslateLabel': ['LABEL:Test'],
}
# Parser ohne Eintrag oben: diese Werte der Reihe nach versuchen
FALLBACK = ['1', 'Yes', 'TestTank', 'X:1 Y:1 Z:1', '1 2']
LIST_PARSERS = ('parseIndexList', 'parseBitString32', 'parseBitString8', 'parseByteSizedIndexList', 'parseLookupList')


def read_tree(code):
    files = {}
    for dp, dn, fns in os.walk(code):
        for fn in fns:
            if fn.lower().endswith(('.cpp', '.h')):
                p = os.path.join(dp, fn)
                files[p] = re.sub(r'//[^\n]*', '', open(p, 'rb').read().decode('latin-1'))
    return files


def body_after(s, pos):
    i = s.index('{', pos) + 1
    depth = 1
    start = i
    while depth and i < len(s):
        if s[i] == '{':
            depth += 1
        elif s[i] == '}':
            depth -= 1
        i += 1
    return s[start:i - 1]


def main():
    code, out = sys.argv[1], sys.argv[2]
    files = read_tree(code)
    alltext = '\n'.join(files.values())

    modfactory = next(s for p, s in files.items() if p.endswith(os.path.join('Thing', 'ModuleFactory.cpp')))
    modules = re.findall(r'addModule\(\s*(\w+)\s*\)', modfactory)
    w3dfactory = next(s for p, s in files.items() if p.endswith('W3DModuleFactory.cpp'))
    modules += re.findall(r'addModule\(\s*(\w+)\s*\)', w3dfactory)

    moddata = dict(re.findall(r'MAKE_STANDARD_MODULE_MACRO_WITH_MODULE_DATA\(\s*(\w+)\s*,\s*(\w+)\s*\)', alltext))
    bases = dict(re.findall(r'class\s+(\w+)\s*:\s*public\s+(\w+)', alltext))

    # name lists for the list parsers: first name
    lists = {}
    for m in re.finditer(r'(?:char\s*\*\s*(?:const\s*)?)(\w+)\s*\[\s*\w*\s*\]\s*=\s*\{\s*"([^"]+)"', alltext):
        lists.setdefault(m.group(1), m.group(2))
    for m in re.finditer(r'LookupListRec\s+(\w+)\s*\[\s*\]\s*=\s*\{\s*\{\s*"([^"]+)"', alltext):
        lists.setdefault(m.group(1), m.group(2))

    buildfp = {}
    for m in re.finditer(r'void\s+(\w+)::buildFieldParse\s*\(\s*MultiIniFieldParse\s*&\s*\w+\s*\)', alltext):
        buildfp[m.group(1)] = body_after(alltext, m.end())
    for m in re.finditer(r'class\s+(\w+)[^;{]*\{', alltext):
        cls = m.group(1)
        if cls in buildfp:
            continue
        body = body_after(alltext, m.end() - 1)
        im = re.search(r'static\s+void\s+buildFieldParse\s*\(\s*MultiIniFieldParse\s*&\s*\w+\s*\)\s*\{', body)
        if im:
            buildfp[cls] = body_after(body, im.end() - 1)

    def fields_of(cls, seen):
        if cls in seen or cls is None:
            return []
        seen.add(cls)
        result = []
        body = buildfp.get(cls)
        if body is None:
            return fields_of(bases.get(cls), seen)
        for base in re.findall(r'(\w+)::buildFieldParse\s*\(', body):
            result += fields_of(base, seen)
        for m in re.finditer(r'\{\s*"([^"]+)"\s*,\s*([\w:]+)\s*,\s*([\w:]+)', body):
            result.append((m.group(1), m.group(2).split('::')[-1], m.group(3).split('::')[-1]))
        return result

    os.makedirs(out, exist_ok=True)
    written = 0
    for n, mod in enumerate(modules):
        data = moddata.get(mod)
        fields = fields_of(data, set()) if data else []
        kind = 'Body' if mod.endswith('Body') else ('ClientUpdate' if 'ClientUpdate' in mod else
                                                    ('Draw' if mod.endswith('Draw') else 'Behavior'))
        lines = ['Object GenObject%03d' % n,
                 '  AddModule ModuleTag_Gen_%03d' % n,
                 '    %s = %s ModuleTag_Gen_%03d_Data' % (kind, mod, n)]
        for name, parser, user in fields:
            if parser in LIST_PARSERS and user in lists:
                values = [lists[user]]
            else:
                values = VALUES.get(parser, FALLBACK)
            for v in values:
                lines.append('?      %s = %s' % (name, v))
        lines += ['    End', '  End', 'End', '']
        with open(os.path.join(out, 'gen_%03d_%s.ini' % (n, mod)), 'w') as fh:
            fh.write('\n'.join(lines))
        written += 1
    print('Seeds erzeugt: %d Module' % written)


if __name__ == '__main__':
    main()
