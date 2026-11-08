#!/usr/bin/env python3
# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
"""Bildreihe des Optionsmenues zur Sichtung (nur Windows).

Je Exe und Aufloesung zwei Laeufe im Fenster mit eigenem Benutzerordner (die Einstellungen des Nutzers bleiben
unberuehrt), Bildschirmfotos ueber den Testablauf-Befehl foto:
  menue   Hauptmenue; Optionen aus dem Hauptmenue (das Hauptmenue dahinter muss ausgeblendet sein) mit allen Seiten;
          Detailstufe aendern und Annehmen (Dialog); Hauptmenue danach (die Knoepfe muessen wieder da sein)
  partie  Gefecht auf --karte; Spielmenue; Optionen aus dem Spielmenue; zurueck ins Spielmenue
  skirmish  Skirmish-Menue, jede Spalte einmal aufgeklappt; Kartenauswahl
  lan     LAN-Lobby; eroeffnetes Spiel, jede Spalte, Schonzeit und Zoom einmal aufgeklappt; Kartenauswahl

Die Bilder kommen als PNG nach <out>/<exe>_<Sprache>_<Breite>x<Hoehe>/NN_<was>.png, dazu je Lauf eine Uebersicht
uebersicht.png und ergebnis.txt (Exit-Code, Testablauf OK/ERROR, Zahl der Bilder).

Aufruf (aus der Wurzel des Repos):
  python tools/abnahme/optionen_bilder.py --exe build-x64/GeneralsMD/Code/Main/Release/GZHNeoZH.exe \\
      --exe build-x64/Generals/Code/Main/Release/GZHNeo.exe --aufloesung 800x600 --aufloesung 1920x1080
  --sprache english braucht Spieldaten mit englischen Archiven neben der Exe.
  Ohne Ton (-noaudio). --vollbild startet mit -fullscreen statt -win (belegt den Bildschirm; nur nach Rueckfrage), die
  Ordner bekommen dann den Zusatz _vollbild.
Exit-Code 0, wenn alle Laeufe den Testablauf ohne Fehler beendet und alle Bilder geliefert haben.
"""
import argparse
import os
import shutil
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

SEITEN = ['Display', 'Details', 'Effects', 'Sound', 'Controls', 'Game']


def ablauf_menue():
    z, namen = [], []

    def foto(name):
        z.extend(['warte 40', 'foto', 'warte 10'])
        namen.append(name)
    z += ['hauptmenue']
    foto('hauptmenue')
    z += ['klick MainMenu.wnd:ButtonOptions', 'fenster OptionsMenu.wnd:ButtonAccept']
    foto('optionen_start')
    for s in SEITEN:
        z += [f'klick OptionsMenu.wnd:ButtonTab{s}']
        foto(f'optionen_{s.lower()}')
    # Detailstufe Niedrig (Eintrag 3) und Annehmen: Dialog "Detailstufe geaendert" bzw. Neustart
    z += ['klick OptionsMenu.wnd:ButtonTabDisplay', 'auswahl OptionsMenu.wnd:ComboBoxDetail 3', 'warte 10',
          'klick OptionsMenu.wnd:ButtonAccept', 'warte 60']
    foto('dialog')
    z += ['klickFalls MessageBox.wnd:ButtonOk 200', 'klickFalls MessageBox.wnd:ButtonYes 60', 'hauptmenue']
    foto('hauptmenue_danach')
    z += ['beenden']
    return z, namen


def ablauf_partie():
    z, namen = [], []

    def foto(name):
        z.extend(['warte 40', 'foto', 'warte 10'])
        namen.append(name)
    z += ['partie', 'warte 150']
    foto('partie')
    z += ['spielmenue', 'fenster QuitMenu.wnd:ButtonOptions']
    foto('spielmenue')
    z += ['klick QuitMenu.wnd:ButtonOptions', 'fenster OptionsMenu.wnd:ButtonAccept']
    foto('optionen_aus_spielmenue')
    z += ['klick OptionsMenu.wnd:ButtonBack', 'fenster QuitMenu.wnd:ButtonReturn']
    foto('spielmenue_danach')
    z += ['beenden']
    return z, namen


def ablauf_skirmish():
    z, namen = [], []

    def foto(name):
        z.extend(['warte 40', 'foto', 'warte 10'])
        namen.append(name)
    w = 'SkirmishGameOptionsMenu.wnd:'
    z += ['hauptmenue', 'klickFalls MainMenu.wnd:ButtonSinglePlayer 60', 'klick MainMenu.wnd:ButtonSkirmish 900',
          f'fenster {w}ButtonStart 900', f'auswahl {w}ComboBoxPlayer1 2']
    foto('skirmish')
    # jede Spalte einmal aufgeklappt: die Listen zeigen alle Eintraege, auch die langen
    for combo in ['ComboBoxPlayer1', 'ComboBoxColor0', 'ComboBoxPlayerTemplate0', 'ComboBoxTeam0', 'ComboBoxGracePeriod']:
        z += [f'klickFalls {w}{combo} 60']
        foto(f'skirmish_{combo[8:].lower()}')
        z += [f'klickFalls {w}{combo} 60']
    z += [f'klick {w}ButtonSelectMap', 'fenster SkirmishMapSelectMenu.wnd:ButtonOK 900']
    foto('kartenauswahl')
    z += ['klick SkirmishMapSelectMenu.wnd:ButtonBack', f'fenster {w}ButtonStart 900', f'klick {w}ButtonBack', 'hauptmenue',
          'beenden']
    return z, namen


def ablauf_lan():
    z, namen = [], []

    def foto(name):
        z.extend(['warte 40', 'foto', 'warte 10'])
        namen.append(name)
    w = 'LanGameOptionsMenu.wnd:'
    z += ['hauptmenue', 'klickFalls MainMenu.wnd:ButtonMultiplayer 60', 'klick MainMenu.wnd:ButtonNetwork 900',
          'fenster LanLobbyMenu.wnd:ButtonHost 900']
    foto('lan_lobby')
    z += ['klick LanLobbyMenu.wnd:ButtonHost', f'fenster {w}ButtonStart 900']
    foto('lan_spiel')
    # jede Spalte und beide eigenen Auswahlfelder (Schonzeit, Zoom) einmal aufgeklappt
    for combo in ['ComboBoxPlayer1', 'ComboBoxColor0', 'ComboBoxPlayerTemplate0', 'ComboBoxTeam0', 'ComboBoxGracePeriod',
                  'ComboBoxMaxZoom']:
        z += [f'klickFalls {w}{combo} 60']
        foto(f'lan_{combo[8:].lower()}')
        z += [f'klickFalls {w}{combo} 60']
    z += [f'klick {w}ButtonSelectMap', 'fenster LanMapSelectMenu.wnd:ButtonOK 900']
    foto('lan_kartenauswahl')
    z += ['klick LanMapSelectMenu.wnd:ButtonBack', f'fenster {w}ButtonStart 900', f'klick {w}ButtonBack',
          'klickFalls MessageBox.wnd:ButtonYes 60', 'fenster LanLobbyMenu.wnd:ButtonBack 900', 'klick LanLobbyMenu.wnd:ButtonBack',
          'hauptmenue', 'beenden']
    return z, namen


ABLAEUFE = {'menue': ablauf_menue, 'partie': ablauf_partie, 'skirmish': ablauf_skirmish, 'lan': ablauf_lan}


def sprache_der_daten(exe, sprache):
    """Sprachname, wie ihn die Archive neben der Exe tragen: german kann bei Generals german2 sein ("Generaele").
    Eine Sprache ohne Archiv bricht den Start ab, statt zurueckzufallen."""
    namen = {n.lower()[:-4] for n in os.listdir(os.path.dirname(exe)) if n.lower().endswith('.big')}
    for kandidat in (sprache, sprache + '2'):
        if kandidat in namen or kandidat + 'zh' in namen:
            return kandidat
    sys.exit(f'keine Sprachdateien fuer {sprache} neben {exe}')


def lauf(exe, sprache, breite, hoehe, art, karte, ziel, warte_slot, vollbild=False):
    exe = os.path.abspath(exe)
    sprache = sprache_der_daten(exe, sprache)
    benutzer = os.path.join(ziel, '_benutzer_' + art)
    if os.path.isdir(benutzer):
        shutil.rmtree(benutzer)
    os.makedirs(benutzer)
    with open(os.path.join(benutzer, 'Settings.ini'), 'w', encoding='utf-8', newline='\r\n') as f:
        f.write(f'[General]\nLanguage = {sprache}\n')
    zeilen, namen = ABLAEUFE[art]()
    skript = os.path.join(ziel, f'ablauf_{art}.txt')
    with open(skript, 'w', encoding='latin-1', newline='\r\n') as f:
        f.write('\n'.join(zeilen) + '\n')
    log = os.path.join(ziel, f'runner_{art}.txt')
    # -fixedTimeStep: das Hauptmenue blendet sich sonst erst nach einer Mausbewegung ein, die Bilder zeigten nur die
    # Menuekarte ohne Oberflaeche
    args = [exe, '-fullscreen' if vollbild else '-win', '-xres', str(breite), '-yres', str(hoehe), '-nologo', '-noaudio', '-fixedTimeStep', '-mehrfach', '-ignoreAsserts',
            '-benutzerdaten', benutzer, '-testablauf', skript, '-crcLog', log]
    if art == 'partie':
        args += ['-skirmish', karte]
    warte_slot()
    t0 = time.time()
    p = subprocess.run(args, cwd=os.path.dirname(exe), timeout=900)
    dauer = time.time() - t0
    bilder = sorted(f for f in os.listdir(benutzer) if f.lower().startswith('sshot') and f.lower().endswith('.bmp'))
    ok_zeilen, fehler = 0, []
    if os.path.isfile(log):
        with open(log, encoding='latin-1') as f:
            for z in f:
                if 'TESTABLAUF OK' in z:
                    ok_zeilen += 1
                if 'TESTABLAUF ERROR' in z:
                    fehler.append(z.strip())
    pngs = []
    try:
        from PIL import Image
        for i, (b, name) in enumerate(zip(bilder, namen)):
            png = os.path.join(ziel, f'{i + 1:02d}_{art}_{name}.png')
            Image.open(os.path.join(benutzer, b)).save(png)
            pngs.append(png)
    except ImportError:
        for i, (b, name) in enumerate(zip(bilder, namen)):
            ziel_bmp = os.path.join(ziel, f'{i + 1:02d}_{art}_{name}.bmp')
            shutil.copy2(os.path.join(benutzer, b), ziel_bmp)
            pngs.append(ziel_bmp)
    gut = p.returncode == 0 and not fehler and len(bilder) == len(namen)
    return gut, (f'{art}: Exit {p.returncode}, {dauer:.0f} s, Bilder {len(bilder)}/{len(namen)}, '
                 f'Testablauf OK-Zeilen {ok_zeilen}' + (f', FEHLER: {fehler[0]}' if fehler else '')), pngs


def uebersicht(pngs, datei):
    try:
        from PIL import Image, ImageDraw
    except ImportError:
        return
    bilder = [Image.open(p).convert('RGB') for p in pngs]
    if not bilder:
        return
    b = 640
    klein = [im.resize((b, int(im.height * b / im.width))) for im in bilder]
    spalten = 3
    h = max(k.height for k in klein) + 22
    blatt = Image.new('RGB', (spalten * b, ((len(klein) + spalten - 1) // spalten) * h), (32, 32, 32))
    zeichnen = ImageDraw.Draw(blatt)
    for i, (k, p) in enumerate(zip(klein, pngs)):
        x, y = (i % spalten) * b, (i // spalten) * h
        blatt.paste(k, (x, y + 22))
        zeichnen.text((x + 4, y + 4), os.path.splitext(os.path.basename(p))[0], fill=(255, 255, 255))
    blatt.save(datei)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--exe', action='append', required=True)
    ap.add_argument('--aufloesung', action='append', default=None, help='BREITExHOEHE, mehrfach (Vorgabe 800x600)')
    ap.add_argument('--sprache', default='german')
    ap.add_argument('--karte', default='Alpine Assault', help='Karte fuer die Partie (-skirmish)')
    ap.add_argument('--out', default=os.path.join('render-out', 'optionen_bilder'))
    ap.add_argument('--nur', action='append', choices=list(ABLAEUFE), help='nur diese Laeufe (mehrfach)')
    ap.add_argument('--vollbild', action='store_true', help='-fullscreen statt -win')
    a = ap.parse_args()
    from slot import wait_free
    gesamt = True
    zeilen = []
    for exe in a.exe:
        for auf in a.aufloesung or ['800x600']:
            breite, hoehe = (int(x) for x in auf.lower().split('x'))
            ziel = os.path.abspath(os.path.join(a.out, f'{os.path.splitext(os.path.basename(exe))[0]}_{a.sprache}_{auf}' + ('_vollbild' if a.vollbild else '')))
            os.makedirs(ziel, exist_ok=True)
            alle = []
            for art in (a.nur or list(ABLAEUFE)):
                gut, text, pngs = lauf(exe, a.sprache, breite, hoehe, art, a.karte, ziel, wait_free, a.vollbild)
                alle += pngs
                gesamt &= gut
                zeile = f'{"OK    " if gut else "FEHLER"} {os.path.basename(exe)} {a.sprache} {auf} {text}'
                print(zeile, flush=True)
                zeilen.append(zeile)
            uebersicht(alle, os.path.join(ziel, 'uebersicht.png'))
            with open(os.path.join(ziel, 'ergebnis.txt'), 'w', encoding='utf-8') as f:
                f.write('\n'.join(z for z in zeilen if os.path.basename(exe) in z and auf in z) + '\n')
    return 0 if gesamt else 1


if __name__ == '__main__':
    sys.exit(main())
