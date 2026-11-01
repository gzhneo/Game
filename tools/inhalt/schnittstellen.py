#!/usr/bin/env python3
# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
"""Inventar der Schnittstellennamen: Namen, die der Engine-Code fest erwartet und die ein Inhaltspaket liefern muss.

Gesucht wird im Code unter Code/ (ohne Tools/ und Tests/) nach Zeichenketten, die der Code an eine Nachschlage-Funktion
übergibt (Objekte, Waffen, Befehlsknöpfe, Bilder, Fenster, Texte, Töne, Knochen, Modelle, Texturen, ...), und nach
Dateinamen mit einer Endung des Spielinhalts. Namen, die nur aus den INIs kommen, gehören nicht zur Schnittstelle (das ist
Inhalt, der auf Inhalt verweist).

    schnittstellen.py             schreibt docs/SCHNITTSTELLENNAMEN.md
    schnittstellen.py --pruefen   prüft, dass die Datei dem Code entspricht (Prüfgang); Rückgabewert 0 heißt gleich

Quelldateien werden als Latin-1 gelesen (CP1252), die Ausgabe ist UTF-8."""
import os
import re
import sys
from collections import defaultdict

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
CODE = os.path.join(ROOT, 'Code')
AUSGABE = os.path.join(ROOT, 'docs', 'SCHNITTSTELLENNAMEN.md')
AUSGENOMMEN = ('Tools', 'Tests')

# Kategorie, Muster vor der öffnenden Klammer, Erklärung. Das erste Argument muss eine Zeichenkette sein.
AUFRUFE = [
    ('objekte', r'ThingFactory->findTemplate|(?<![>\w])findTemplate(?=\s*\(\s*"[^"]*"\s*(?:,\s*(?:TRUE|FALSE))?\s*\))'),
    ('partikel', r'ParticleSystemManager->findTemplate|findParticleSystemTemplate'),
    ('anim2d', r'Anim2DCollection->findTemplate'),
    ('waffen', r'findWeaponTemplate'),
    ('ruestungen', r'findArmorTemplate'),
    ('upgrades', r'findUpgrade'),
    ('spezialkraefte', r'findSpecialPowerTemplate'),
    ('befehlsknoepfe', r'findCommandButton'),
    ('befehlssaetze', r'findCommandSet'),
    ('ocl', r'findObjectCreationList'),
    ('fx', r'findFXList'),
    ('wissenschaften', r'getScienceFromInternalName|findScienceInfo'),
    ('bilder', r'findImageByName'),
    ('fensterdateien', r'winCreateFromScript|winCreateLayout'),
    ('texte', r'TheGameText->fetch|(?<![>\w.])fetch'),
    ('toene', r'setEventName|AudioEventRTS\s+\w+|AudioEventRTS'),
    ('einheitentoene', r'getPerUnitSound'),
    ('knochen', r'Get_Bone_Index|getPristineBonePositions|getMultiLogicalBonePosition|getSingleLogicalBonePosition'),
    ('modelle', r'Create_Render_Obj'),
    ('texturen', r'Get_Texture|findTexture'),
]

KATEGORIEN = {
    'objekte': ('Objekte (`ThingTemplate`)', 'Objekte aus `Object`-Blöcken der INIs, die der Code selbst nachschlägt.'),
    'partikel': ('Partikelsysteme', '`ParticleSystem`-Blöcke aus `ParticleSystem.ini`.'),
    'anim2d': ('2D-Animationen', '`Animation2D`-Blöcke.'),
    'waffen': ('Waffen', '`Weapon`-Blöcke aus `Weapon.ini`.'),
    'ruestungen': ('Rüstungen', '`Armor`-Blöcke.'),
    'upgrades': ('Upgrades', '`Upgrade`-Blöcke aus `Upgrade.ini`.'),
    'spezialkraefte': ('Spezialkräfte', '`SpecialPower`-Blöcke.'),
    'befehlsknoepfe': ('Befehlsknöpfe', '`CommandButton`-Blöcke aus `CommandButton.ini`.'),
    'befehlssaetze': ('Befehlssätze', '`CommandSet`-Blöcke.'),
    'ocl': ('Objekterzeugungslisten', '`ObjectCreationList`-Blöcke.'),
    'fx': ('FX-Listen', '`FXList`-Blöcke.'),
    'wissenschaften': ('Wissenschaften', '`Science`-Blöcke.'),
    'bilder': ('Bilder (`MappedImage`)', 'Blöcke aus `Data/INI/MappedImages`, meist Teile der Oberfläche.'),
    'fenster': ('Fenster und Bedienelemente', 'Namen aus den Fensterdateien (`.wnd`), je Datei; der Code sucht sie über '
                '`NAMEKEY("Datei.wnd:Name")` oder eine Zeichenkette dieser Form.'),
    'fensterdateien': ('Fensterdateien', 'Fensterdateien, die der Code selbst lädt (`winCreateFromScript`, Dateinamen im Code).'),
    'namen': ('Weitere Namen', 'Andere Inhaltsnamen in `NAMEKEY` oder `nameToKey`: Seiten (`PlayerTemplate`), Fortbewegung, Spieler.'),
    'texte': ('Textkennungen', 'Kennungen der Spieltexte (`.csf`, `.str`), nachgeschlagen mit `TheGameText->fetch`.'),
    'toene': ('Tonereignisse', '`AudioEvent`-Blöcke (`SoundEffects.ini`, `Voice.ini`, `MiscAudio.ini`, ...).'),
    'einheitentoene': ('Einheitentöne', 'Schlüssel unter `UnitSpecificSounds` eines Objekts; fehlt einer, bleibt es still.'),
    'knochen': ('Knochen in Modellen', 'Knochennamen (bzw. Vorsilben mit laufender Nummer), die der Code in Modellen sucht.'),
    'modelle': ('Modelle', 'W3D-Modelle, die der Code selbst anlegt.'),
    'texturen': ('Texturen', 'Texturen, die der Code selbst lädt.'),
    'dateien': ('Dateien', 'Dateinamen mit einer Endung des Spielinhalts, die als Zeichenkette im Code stehen.'),
}
REIHENFOLGE = ['objekte', 'waffen', 'ruestungen', 'upgrades', 'spezialkraefte', 'wissenschaften', 'befehlsknoepfe',
               'befehlssaetze', 'ocl', 'fx', 'partikel', 'anim2d', 'knochen', 'modelle', 'texturen', 'toene',
               'einheitentoene', 'bilder', 'fensterdateien', 'fenster', 'texte', 'namen', 'dateien']

ENDUNGEN = r'ini|wnd|w3d|tga|dds|wav|mp3|bik|map|csf|str|big'
# Dateien, die das Spiel für den Benutzer schreibt oder liest (Profil, Protokolle); kein Spielinhalt.
BENUTZERDATEIEN = {'options.ini', 'settings.ini', 'skirmish.ini', 'network.ini', 'state.ini', 'solo.ini', 'mapcache.ini',
                   'skirmishstats.ini', 'rejoin.ini'}
# Namen, die die Engine selbst bereitstellt (Prototyp eines leeren Modells).
EINGEBAUT = {('modelle', 'NULL'), ('namen', 'Bogus')}
# Dateien, deren Zeichenketten Schlüssel der Einstellungen sind, keine Dateinamen.
OHNE_DATEINAMEN = {'Core/EngineCore/Engine/SettingsSchema.cpp'}


def ohne_kommentare(text):
    """Entfernt // und /* */ ausserhalb von Zeichenketten; Zeilenumbrüche bleiben, damit Zeilennummern stimmen."""
    aus = []
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if c == '/' and i + 1 < n and text[i + 1] == '/':
            j = text.find('\n', i)
            i = n if j < 0 else j
        elif c == '/' and i + 1 < n and text[i + 1] == '*':
            j = text.find('*/', i + 2)
            j = n if j < 0 else j + 2
            aus.append('\n' * text.count('\n', i, j))
            i = j
        elif c in '"\'':
            j = i + 1
            while j < n and text[j] != c and text[j] != '\n':
                j += 2 if text[j] == '\\' else 1
            aus.append(text[i:j + 1])
            i = j + 1
        else:
            aus.append(c)
            i += 1
    return ''.join(aus)


def ohne_auskommentierte_bloecke(text):
    """Blendet #if 0 ... #endif aus (mit Verschachtelung)."""
    aus, tiefe = [], 0
    for zeile in text.split('\n'):
        s = zeile.strip()
        if tiefe:
            if re.match(r'#\s*if', s):
                tiefe += 1
            elif re.match(r'#\s*endif', s):
                tiefe -= 1
            elif tiefe == 1 and re.match(r'#\s*(else|elif)', s):
                tiefe = 0
            aus.append('')
            continue
        if re.match(r'#\s*if\s+0\b', s):
            tiefe = 1
            aus.append('')
            continue
        aus.append(zeile)
    return '\n'.join(aus)


def quellen():
    for wurzel, verzeichnisse, dateien in os.walk(CODE):
        rel = os.path.relpath(wurzel, CODE)
        if rel.split(os.sep)[0] in AUSGENOMMEN:
            verzeichnisse[:] = []
            continue
        verzeichnisse.sort()
        for d in sorted(dateien):
            if d.endswith(('.cpp', '.h', '.inl')):
                yield os.path.join(wurzel, d)


def projektnamen():
    """Was das Projekt selbst liefert: Dateinamen unter Data/ und Textkennungen aus Data/Text."""
    dateien = set()
    for wurzel, _, ds in os.walk(os.path.join(ROOT, 'Data')):
        dateien |= {d.lower() for d in ds}
    texte = set()
    for d in os.listdir(os.path.join(ROOT, 'Data', 'Text')):
        if d.endswith('.str'):
            for zeile in open(os.path.join(ROOT, 'Data', 'Text', d), encoding='cp1252'):
                z = zeile.strip()
                if re.match(r'^[A-Za-z0-9_]+:[A-Za-z0-9_]+$', z):
                    texte.add(z.lower())
    return dateien, texte


def sammeln():
    funde = defaultdict(lambda: defaultdict(set))	# kategorie -> name -> {datei}
    fenster = defaultdict(lambda: defaultdict(set))	# wnd -> name -> {datei}
    aufrufe = [(k, re.compile(r'(?:%s)\s*\(\s*"((?:[^"\\]|\\.)*)"' % m)) for k, m in AUFRUFE]
    namekey = re.compile(r'(?:NAMEKEY|nameToKey)\s*\(\s*"((?:[^"\\]|\\.)*)"')
    # a window name in a string is a window name, whichever call takes it (also helpers around NAMEKEY)
    fenster_lit = re.compile(r'"([A-Za-z0-9_]+\.wnd):([A-Za-z0-9_]+)"', re.IGNORECASE)
    knochen_var = re.compile(r'AsciiString\s+\w*[Bb]one\w*\s*\(\s*"([^"]+)"')
    klassen = set()
    klasse = re.compile(r'\b(?:class|struct)\s+(\w+)')
    datei_lit = re.compile(r'"((?:[^"\\\n]|\\.)*\.(?:%s))"' % ENDUNGEN, re.IGNORECASE)
    for pfad in quellen():
        rel = os.path.relpath(pfad, CODE).replace(os.sep, '/')
        text = ohne_auskommentierte_bloecke(ohne_kommentare(open(pfad, encoding='latin-1').read()))
        klassen |= set(klasse.findall(text))
        for kat, rx in aufrufe:
            for m in rx.finditer(text):
                name = m.group(1)
                if name and (kat, name) not in EINGEBAUT:
                    funde[kat][name].add(rel)
        for m in knochen_var.finditer(text):
            funde['knochen'][m.group(1)].add(rel)
        for m in namekey.finditer(text):
            name = m.group(1)
            if ':' in name and name.split(':', 1)[0].lower().endswith('.wnd'):
                datei, element = name.split(':', 1)
                fenster[datei][element].add(rel)
            elif name:
                funde['namen'][name].add(rel)
        for m in fenster_lit.finditer(text):
            fenster[m.group(1)][m.group(2)].add(rel)
        for m in datei_lit.finditer(text):
            name = m.group(1).replace('\\\\', '\\').lstrip('\\')
            if re.search(r'[{}%*]', name) or name.startswith(('.', '_', '/')) or ' ' in name or rel in OHNE_DATEINAMEN:
                continue	# Formatvorlagen, Suchmuster und Endungen
            if name.lower().endswith('.wnd'):
                funde['fensterdateien'][name].add(rel)
            else:
                funde['dateien'][name].add(rel)
    # Namen von C++-Klassen (Module, ModuleTag_*-Helfer) sind Engine, kein Inhalt.
    for name in list(funde['namen']):
        if name in klassen or name.startswith('ModuleTag_'):
            del funde['namen'][name]
    # Texturen und Modelle stehen auch als Dateiname im Code; die eigene Kategorie genügt.
    for kat in ('texturen', 'modelle'):
        for name in funde[kat]:
            funde['dateien'].pop(name, None)
    return funde, fenster


def herkunft(kat, name, projekt_dateien, projekt_texte):
    basis = name.replace('\\', '/').split('/')[-1].lower()
    if kat == 'texte':
        return 'Projekt' if name.lower() in projekt_texte else 'EA'
    if basis in BENUTZERDATEIEN:
        return 'Benutzer'
    if name.startswith('ProjectData') or basis in projekt_dateien:
        return 'Projekt'
    return 'EA'


def fundstellen(dateien):
    return ', '.join('`%s`' % d for d in sorted(dateien))


def schreiben(funde, fenster):
    projekt_dateien, projekt_texte = projektnamen()
    z = []
    z.append('# Schnittstellennamen: was der Engine-Code fest vom Spielinhalt erwartet')
    z.append('')
    z.append('Erzeugt von `tools/inhalt/schnittstellen.py` aus dem Code unter `Code/` (ohne `Tools/` und `Tests/`); nicht von Hand ändern.')
    z.append('Der Prüfgang (`tools/gate.sh`) prüft, dass die Datei dem Code entspricht; nach einer Codeänderung, die einen Namen hinzufügt')
    z.append('oder entfernt, `python3 tools/inhalt/schnittstellen.py` laufen lassen. Zweck und Einordnung: `docs/FREIER_INHALT.md`')
    z.append('(Nr. 79, Stufe 1).')
    z.append('')
    z.append('Aufgeführt sind Namen, die als Zeichenkette im Code stehen und an eine Nachschlage-Funktion gehen. Ein Inhaltspaket, das')
    z.append('EAs Inhalt ersetzen will, muss sie liefern oder in Kauf nehmen, dass die Stelle ohne sie auskommt (je nach Stelle eine')
    z.append('Meldung, ein fehlender Effekt oder ein Absturz -- das sagt die Fundstelle). Namen, die nur in den INIs auf andere INI-Namen')
    z.append('verweisen, gehören nicht dazu: das ist Inhalt, der auf Inhalt verweist, und ein Paket bringt beides mit.')
    z.append('')
    z.append('**Grenzen:** Nicht erfasst sind Namen, die der Code erst zur Laufzeit zusammensetzt (etwa Knochen mit laufender Nummer:')
    z.append('erfasst ist nur die Vorsilbe), Zeichenketten, die über eine Variable an die Nachschlage-Funktion gehen, ohne als Knochen-')
    z.append('oder Dateiname erkennbar zu sein, und die Feldnamen der INIs (das ist das Format, `docs/FREIER_INHALT.md` Abschnitt 3).')
    z.append('Beide Spiele teilen den Code; ein Name, den nur eines braucht (etwa die Generäle von Zero Hour), steht ohne Kennzeichen da.')
    z.append('')
    z.append('**Herkunft:** `EA` -- kommt heute aus EAs Spieldaten; `Projekt` -- liefert das Projekt selbst (unter `Data/`, Textkennungen')
    z.append('in `Data/Text`); `Benutzer` -- Profil- und Einstellungsdateien des Spielers, kein Spielinhalt. Fundstellen relativ zu `Code/`.')
    z.append('')
    summe = defaultdict(int)
    zeilen_je = {}
    for kat in REIHENFOLGE:
        titel, erklaerung = KATEGORIEN[kat]
        teil = ['## %s' % titel, '', erklaerung, '']
        if kat == 'fenster':
            if not fenster:
                continue
            teil += ['| Fensterdatei | Herkunft | Namen | Fundstellen |', '|---|---|---|---|']
            anzahl = 0
            for datei in sorted(fenster, key=str.lower):
                namen = fenster[datei]
                anzahl += len(namen)
                h = herkunft('fensterdateien', datei, projekt_dateien, projekt_texte)
                summe[h] += len(namen)
                alle = set().union(*namen.values())
                teil.append('| `%s` | %s | %s | %s |' % (datei, h, ', '.join('`%s`' % n for n in sorted(namen, key=str.lower)),
                                                        fundstellen(alle)))
            zeilen_je[kat] = anzahl
        else:
            namen = funde.get(kat)
            if not namen:
                continue
            teil += ['| Name | Herkunft | Fundstellen |', '|---|---|---|']
            for name in sorted(namen, key=lambda s: (s.lower(), s)):
                h = herkunft(kat, name, projekt_dateien, projekt_texte)
                summe[h] += 1
                teil.append('| `%s` | %s | %s |' % (name.replace('|', '\\|'), h, fundstellen(namen[name])))
            zeilen_je[kat] = len(namen)
        teil.append('')
        z += teil
    kopf = ['## Übersicht', '', '| Bereich | Namen |', '|---|---|']
    for kat in REIHENFOLGE:
        if kat in zeilen_je:
            kopf.append('| %s | %d |' % (KATEGORIEN[kat][0], zeilen_je[kat]))
    kopf.append('| **zusammen** | **%d** (EA %d, Projekt %d, Benutzer %d) |' % (sum(zeilen_je.values()), summe['EA'],
                                                                          summe['Projekt'], summe['Benutzer']))
    kopf.append('')
    i = z.index('**Herkunft:** `EA` -- kommt heute aus EAs Spieldaten; `Projekt` -- liefert das Projekt selbst (unter `Data/`, Textkennungen') + 3
    z[i:i] = kopf
    return '\n'.join(z), sum(zeilen_je.values()), summe


def main():
    funde, fenster = sammeln()
    text, anzahl, summe = schreiben(funde, fenster)
    zusammen = '%d Namen (EA %d, Projekt %d, Benutzer %d)' % (anzahl, summe['EA'], summe['Projekt'], summe['Benutzer'])
    if '--pruefen' in sys.argv[1:]:
        if not os.path.isdir(os.path.dirname(AUSGABE)):
            # The public source tree has no docs/; the scan still has to run through.
            print('Schnittstellennamen gesammelt (ohne docs/, kein Vergleich): %s' % zusammen)
            return 0
        alt = open(AUSGABE, encoding='utf-8').read() if os.path.exists(AUSGABE) else ''
        if alt != text:
            import difflib
            for zeile in list(difflib.unified_diff(alt.splitlines(), text.splitlines(), 'Datei', 'Code', lineterm='', n=0))[:20]:
                print(zeile)
            print('docs/SCHNITTSTELLENNAMEN.md entspricht nicht dem Code: python3 tools/inhalt/schnittstellen.py')
            return 1
        print('Schnittstellennamen aktuell: %s' % zusammen)
        return 0
    open(AUSGABE, 'w', encoding='utf-8', newline='\n').write(text)
    print('docs/SCHNITTSTELLENNAMEN.md geschrieben: %s' % zusammen)
    return 0


if __name__ == '__main__':
    sys.exit(main())
