#!/usr/bin/env python3
# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
"""Tastatur-Livetest (TODO 68 (1), 2026-09-26): Tastendruecke als Fensternachrichten an das eigene Spielfenster.

Seit TODO 68 liest das Spiel die Tastatur aus WM_KEYDOWN/WM_KEYUP/WM_SYSKEY* (Win32Keyboard) statt ueber
DirectInput. Deshalb lassen sich Tasten hier per PostMessage an genau das Fenster des gestarteten Spiels schicken
-- kein globales SendInput, andere Fenster (und der Nutzer am Rechner) bekommen nichts ab. Belege sind
Fensterbilder (PrintWindow, auch wenn das Fenster verdeckt ist) und das Spielprotokoll.

Zwei Laeufe je Spiel, jeder mit -testablauf (Code/GameEngine/GameClient/TestScript.cpp), das die Menues
faehrt und an festen Stellen "log TASTE_..." schreibt; dieses Skript wartet auf die Zeile und handelt dann:

  menue    Hauptmenue (Bild), Mehrspieler-Untermenue (Bild: kein "Online"-Knopf mehr, TODO 69), LAN-Lobby:
           in das Chatfeld "Tast", Umlaute per WM_CHAR (der Weg fuer Texteingabe und IME, unveraendert),
           Ruecktaste (Tastencode), Enter (Tastencode) -- die Chatzeile muss im Bild erscheinen.
  partie   Skirmish als Beobachter (-skirmish, -testhilfen): Pfeiltaste rechts gedrueckt halten, die Kamera
           faehrt (Bilder unterscheiden sich); Fokusverlust (WM_KILLFOCUS) -- die Kamera muss stehen bleiben,
           obwohl kein Loslassen kam (keine festhaengende Taste). Dann Pfeil links druecken und loslassen.
           Alt+M (WM_SYSKEYDOWN, Alt-Modifikator): Testhilfen-Seitenleiste klappt auf.
           Die Kamerapruefung laeuft nur in Generals: Zero Hour belegt die Pfeiltasten mit der Einheitenwahl.

Aufruf (Git-Bash, Wurzel des Repositorys):
  python tools/abnahme/tastatur_test.py --exe build-x64/Generals/Code/Main/Debug/GZHNeo.exe --out tastatur-out/generals
Exit-Code 0 = alle Pruefungen bestanden. Vor jedem Start wartet das Skript auf die Spielstart-Warteschlange
(slot.py). Bilder und Protokoll landen in --out.
"""
import argparse
import ctypes
import ctypes.wintypes as wt
import os
import subprocess
import sys
import time

HIER = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HIER)
from slot import wait_free  # noqa: E402

try:
    from PIL import Image, ImageChops, ImageStat
except ImportError:  # pragma: no cover
    Image = None

user32 = ctypes.WinDLL("user32", use_last_error=True)
gdi32 = ctypes.WinDLL("gdi32", use_last_error=True)

WM_KEYDOWN, WM_KEYUP, WM_SYSKEYDOWN, WM_SYSKEYUP = 0x0100, 0x0101, 0x0104, 0x0105
WM_CHAR, WM_KILLFOCUS, WM_SETFOCUS = 0x0102, 0x0008, 0x0007
PW_RENDERFULLCONTENT = 2

# virtuelle Taste, Scancode (Satz 1), erweitert
TASTEN = {
    "RIGHT": (0x27, 0x4D, True), "LEFT": (0x25, 0x4B, True), "RETURN": (0x0D, 0x1C, False),
    "BACK": (0x08, 0x0E, False), "LMENU": (0x12, 0x38, False), "M": (0x4D, 0x32, False),
    "T": (0x54, 0x14, False), "A": (0x41, 0x1E, False), "S": (0x53, 0x1F, False),
    "X": (0x58, 0x2D, False),
}

# "Kamera steht": nach dem Loslassen laeuft die Kamera kurz aus (gemessen gut 1 Pixel der Minikarte in
# 1,5 s); eine festhaengende Taste faehrt dagegen mit rund 100 Pixeln je Sekunde weiter.
STEHT_PX = 5.0

FENSTER = ["-win", "-xres", "800", "-yres", "600", "-fixedTimeStep", "-mehrfach", "-ignoreAsserts", "-nologo"]

MENUE = """# tastatur_test.py: Menues, LAN-Lobby-Chat
hauptmenue
warte 120
log TASTE_HAUPTMENUE
warte 400
klickFalls MainMenu.wnd:ButtonMultiplayer
fenster MainMenu.wnd:ButtonNetwork
warte 60
log TASTE_MEHRSPIELER
warte 400
klick MainMenu.wnd:ButtonNetwork
fenster LanLobbyMenu.wnd:ButtonHost
warte 120
klick LanLobbyMenu.wnd:TextEntryChat
warte 30
log TASTE_LANLOBBY
warte 4000
log TASTE_LANLOBBY_ENDE
klick LanLobbyMenu.wnd:ButtonBack
hauptmenue
klickFalls MainMenu.wnd:ButtonMultiBack
hauptmenue
warte 60
beenden
"""

PARTIE = """# tastatur_test.py: Partie als Beobachter
partie
logik 90
log TASTE_PARTIE
logik 1500
log TASTE_PARTIE_ENDE
beenden
"""


class Fehler(Exception):
    pass


def fenster_von(pid, frist=120):
    gefunden = []

    @ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)
    def cb(hwnd, _):
        p = wt.DWORD()
        user32.GetWindowThreadProcessId(hwnd, ctypes.byref(p))
        if p.value == pid and user32.IsWindowVisible(hwnd):
            name = ctypes.create_unicode_buffer(64)
            user32.GetClassNameW(hwnd, name, 64)
            if name.value == "Game Window":
                gefunden.append(hwnd)
        return True

    ende = time.time() + frist
    while time.time() < ende:
        gefunden.clear()
        user32.EnumWindows(cb, 0)
        if gefunden:
            return gefunden[0]
        time.sleep(0.5)
    raise Fehler("Spielfenster nicht gefunden")


def bild(hwnd, pfad):
    """Fensterinhalt per PrintWindow (geht auch verdeckt), als PNG."""
    r = wt.RECT()
    user32.GetClientRect(hwnd, ctypes.byref(r))
    w, h = r.right - r.left, r.bottom - r.top
    hdc = user32.GetDC(hwnd)
    mdc = gdi32.CreateCompatibleDC(hdc)
    bmp = gdi32.CreateCompatibleBitmap(hdc, w, h)
    gdi32.SelectObject(mdc, bmp)
    user32.PrintWindow(hwnd, mdc, PW_RENDERFULLCONTENT | 1)  # 1 = PW_CLIENTONLY

    class BMIH(ctypes.Structure):
        _fields_ = [("biSize", wt.DWORD), ("biWidth", wt.LONG), ("biHeight", wt.LONG), ("biPlanes", wt.WORD),
                    ("biBitCount", wt.WORD), ("biCompression", wt.DWORD), ("biSizeImage", wt.DWORD),
                    ("biXPelsPerMeter", wt.LONG), ("biYPelsPerMeter", wt.LONG), ("biClrUsed", wt.DWORD),
                    ("biClrImportant", wt.DWORD)]
    bi = BMIH(ctypes.sizeof(BMIH), w, -h, 1, 32, 0, 0, 0, 0, 0, 0)
    buf = ctypes.create_string_buffer(w * h * 4)
    gdi32.GetDIBits(mdc, bmp, 0, h, buf, ctypes.byref(bi), 0)
    gdi32.DeleteObject(bmp)
    gdi32.DeleteDC(mdc)
    user32.ReleaseDC(hwnd, hdc)
    img = Image.frombuffer("RGBA", (w, h), buf, "raw", "BGRA", 0, 1).convert("RGB")
    img.save(pfad)
    return img


def unterschied(a, b, box=None):
    """mittlere Abweichung je Kanal (0..255) zweier Bilder, optional nur ein Ausschnitt"""
    if box:
        a, b = a.crop(box), b.crop(box)
    d = ImageChops.difference(a, b)
    return sum(ImageStat.Stat(d).mean) / 3.0


def kamera(img):
    """x-Mitte des gelben Sichtrahmens in der Minikarte (links unten, 800x600), in Pixeln"""
    xs = []
    for y in range(440, 595):
        for x in range(5, 180):
            r, g, b = img.getpixel((x, y))
            if r > 190 and g > 170 and b < 90:
                xs.append(x)
    if len(xs) < 10:
        raise Fehler("Sichtrahmen in der Minikarte nicht gefunden")
    return sum(xs) / len(xs)


def taste(hwnd, name, runter, alt=False):
    vk, sc, ext = TASTEN[name]
    lp = 1 | (sc << 16) | ((1 << 24) if ext else 0) | ((1 << 29) if alt else 0)
    if not runter:
        lp |= (1 << 30) | (1 << 31)
    if alt:
        msg = WM_SYSKEYDOWN if runter else WM_SYSKEYUP
    else:
        msg = WM_KEYDOWN if runter else WM_KEYUP
    if not user32.PostMessageW(hwnd, msg, vk, lp):
        raise Fehler("PostMessage fehlgeschlagen")


def tippe(hwnd, name, pause=0.08):
    taste(hwnd, name, True)
    time.sleep(pause)
    taste(hwnd, name, False)
    time.sleep(pause)


def zeichen(hwnd, text, pause=0.08):
    for ch in text:
        user32.PostMessageW(hwnd, WM_CHAR, ord(ch), 1)
        time.sleep(pause)


class Lauf:
    def __init__(self, exe, out, name, skript, extra):
        self.exe, self.out, self.name = os.path.abspath(exe), os.path.abspath(out), name
        self.skriptpfad = os.path.join(self.out, name + ".testablauf.txt")
        with open(self.skriptpfad, "w", encoding="utf-8") as f:
            f.write(skript)
        self.extra = extra
        self.proc = None
        self.log = None

    def start(self):
        wait_free()
        cmd = [self.exe] + FENSTER + ["-testablauf", self.skriptpfad] + self.extra
        self.proc = subprocess.Popen(cmd, cwd=os.path.dirname(self.exe))
        self.log = os.path.join(os.path.dirname(self.exe), f"m{self.proc.pid}_DebugLogFileD.txt")
        self.hwnd = fenster_von(self.proc.pid)
        return self.hwnd

    def warte_auf(self, marke, frist=600):
        ende = time.time() + frist
        while time.time() < ende:
            if self.proc.poll() is not None:
                raise Fehler(f"Spiel vorzeitig beendet (Exit {self.proc.returncode}) vor {marke}")
            try:
                with open(self.log, "r", errors="replace") as f:
                    if "LOG " + marke in f.read():	# Testablauf-Befehl "log": Zeile "... LOG <Text>"
                        return
            except OSError:
                pass
            time.sleep(0.25)
        raise Fehler(f"Zeitlimit beim Warten auf {marke}")

    def ende(self, frist=300):
        try:
            code = self.proc.wait(timeout=frist)
        except subprocess.TimeoutExpired:
            self.proc.kill()
            self.proc.wait()
            code = None
        ziel = os.path.join(self.out, self.name + ".log")
        if self.log and os.path.isfile(self.log):
            with open(self.log, "rb") as q, open(ziel, "wb") as z:
                z.write(q.read())
            vorsatz = f"m{self.proc.pid}_"
            exe_dir = os.path.dirname(self.exe)
            for f in os.listdir(exe_dir):
                if f.startswith(vorsatz):
                    try:
                        os.remove(os.path.join(exe_dir, f))
                    except OSError:
                        pass
        return code


def pruefe(ergebnisse, name, ok, text):
    ergebnisse.append((name, ok, text))
    print(("OK    " if ok else "FEHLER") + f" {name}: {text}", flush=True)


def lauf_menue(a, erg):
    lauf = Lauf(a.exe, a.out, "menue", MENUE, [])
    hwnd = lauf.start()
    try:
        lauf.warte_auf("TASTE_HAUPTMENUE")
        bild(hwnd, os.path.join(a.out, "menue_1_haupt.png"))
        lauf.warte_auf("TASTE_MEHRSPIELER")
        bild(hwnd, os.path.join(a.out, "menue_2_mehrspieler.png"))
        lauf.warte_auf("TASTE_LANLOBBY")
        time.sleep(1.0)
        vorher = bild(hwnd, os.path.join(a.out, "menue_3_lan_vorher.png"))
        # "Tast" als Tastendruecke (TranslateMessage macht WM_CHAR daraus), Umlaute direkt als WM_CHAR
        for n in ("T", "A", "S", "T"):
            tippe(hwnd, n)
        zeichen(hwnd, " äöüßÄ x")
        tippe(hwnd, "BACK")       # Ruecktaste: das "x" wieder weg (Tastencode KEY_BACKSPACE)
        zeichen(hwnd, "ok")
        time.sleep(0.5)
        getippt = bild(hwnd, os.path.join(a.out, "menue_4_lan_getippt.png"))
        tippe(hwnd, "RETURN")     # Enter: Chatzeile senden (Tastencode KEY_ENTER)
        time.sleep(1.5)
        nachher = bild(hwnd, os.path.join(a.out, "menue_5_lan_gesendet.png"))
        d1 = unterschied(vorher, getippt)
        d2 = unterschied(getippt, nachher)
        pruefe(erg, "Chat: Eingabefeld zeigt Text", d1 > 0.05, f"Bildabweichung {d1:.3f}")
        pruefe(erg, "Chat: Enter sendet (Chatliste/Feld aendert sich)", d2 > 0.05, f"Bildabweichung {d2:.3f}")
        lauf.warte_auf("TASTE_LANLOBBY_ENDE")
    finally:
        code = lauf.ende()
    pruefe(erg, "Menue-Lauf endet sauber", code == 0, f"Exit-Code {code}")
    with open(os.path.join(a.out, "menue.log"), "r", errors="replace") as f:
        log = f.read()
    pruefe(erg, "Protokoll: Tastatur ohne DirectInput", ("Windows keyboard messages" in log or "Windows-Tastaturnachrichten" in log),
           "Zeile 'Keyboard initialized (Windows keyboard messages'")


def lauf_partie(a, erg):
    extra = ["-skirmish", a.map, "-skirmishAI", "2", "-skirmishSeed", "1", "-testhilfen"]
    lauf = Lauf(a.exe, a.out, "partie", PARTIE, extra)
    hwnd = lauf.start()
    try:
        lauf.warte_auf("TASTE_PARTIE", frist=900)
        zh = "gzhneozh" in os.path.basename(a.exe).lower()
        if zh:
            # Zero Hour belegt die Pfeiltasten ohne Modifikator mit "naechste/vorige Einheit waehlen"
            # (CommandMap.ini der Sprachdaten), gescrollt wird dort nicht -- die Kamerapruefung gilt nur
            # fuer Generals. Gleich wie mit DirectInput (Datenbelegung).
            pruefe(erg, "Kamera per Pfeiltaste", True, "entfaellt in Zero Hour (Pfeile = Einheitenwahl)")
            time.sleep(2.0)
        else:
            # Kameraposition = Mitte des gelben Sichtrahmens in der Minikarte (unabhaengig von bewegten
            # Einheiten im Bild). Die Beobachter-Kamera beginnt in dieser Partie am rechten Kartenrand,
            # deshalb zuerst nach links.
            time.sleep(2.0)
            k0 = kamera(bild(hwnd, os.path.join(a.out, "partie_0_start.png")))
            taste(hwnd, "LEFT", True)
            time.sleep(0.3)
            k1 = kamera(bild(hwnd, os.path.join(a.out, "partie_1_links_gehalten.png")))
            time.sleep(0.3)
            k2 = kamera(bild(hwnd, os.path.join(a.out, "partie_2_links_gehalten.png")))
            pruefe(erg, "Pfeil links gehalten: Kamera faehrt", k1 < k0 - 2 and k2 < k1 - 2,
                   f"Minikarte x {k0:.1f} -> {k1:.1f} -> {k2:.1f}")
            # Fokusverlust ohne Loslassen: die Taste darf nicht festhaengen
            user32.PostMessageW(hwnd, WM_KILLFOCUS, 0, 0)
            time.sleep(0.5)
            k3 = kamera(bild(hwnd, os.path.join(a.out, "partie_3_fokus_weg.png")))
            time.sleep(1.5)
            k4 = kamera(bild(hwnd, os.path.join(a.out, "partie_4_fokus_weg.png")))
            pruefe(erg, "Fokusverlust: Kamera steht (nichts haengt fest)", abs(k4 - k3) < STEHT_PX,
                   f"Minikarte x {k3:.1f} -> {k4:.1f}")
            user32.PostMessageW(hwnd, WM_SETFOCUS, 0, 0)
            taste(hwnd, "LEFT", False)   # spaetes Loslassen: wird verworfen (Taste gilt nicht mehr als gedrueckt)
            time.sleep(0.5)
            # Pfeil rechts druecken und loslassen: Kamera faehrt und steht danach
            taste(hwnd, "RIGHT", True)
            time.sleep(0.3)
            taste(hwnd, "RIGHT", False)
            time.sleep(0.5)
            k5 = kamera(bild(hwnd, os.path.join(a.out, "partie_5_rechts_los.png")))
            time.sleep(1.5)
            k6 = kamera(bild(hwnd, os.path.join(a.out, "partie_6_rechts_los.png")))
            pruefe(erg, "Pfeil rechts: gefahren", k5 > k4 + 2, f"Minikarte x {k4:.1f} -> {k5:.1f}")
            pruefe(erg, "Pfeil rechts losgelassen: Kamera steht", abs(k6 - k5) < STEHT_PX,
                   f"Minikarte x {k5:.1f} -> {k6:.1f}")
        # Alt+M (WM_SYSKEYDOWN): Testhilfen-Seitenleiste am linken Rand
        links = (0, 60, 200, 420)
        b6 = bild(hwnd, os.path.join(a.out, "partie_6b_vor_alt_m.png"))
        taste(hwnd, "LMENU", True, alt=True)
        time.sleep(0.2)
        taste(hwnd, "M", True, alt=True)
        time.sleep(0.2)
        taste(hwnd, "M", False, alt=True)
        taste(hwnd, "LMENU", False, alt=False)
        time.sleep(1.5)
        b7 = bild(hwnd, os.path.join(a.out, "partie_7_alt_m.png"))
        pruefe(erg, "Alt+M: Testhilfen-Leiste klappt auf", unterschied(b6, b7, links) > 3.0,
               f"Abweichung links {unterschied(b6, b7, links):.2f}")
        lauf.warte_auf("TASTE_PARTIE_ENDE", frist=900)
    finally:
        code = lauf.ende()
    pruefe(erg, "Partie-Lauf endet sauber", code == 0, f"Exit-Code {code}")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--exe", required=True)
    ap.add_argument("--out", default="tastatur-out")
    ap.add_argument("--map", default="Alpine Assault")
    ap.add_argument("--nur", choices=("menue", "partie"))
    a = ap.parse_args()
    if Image is None:
        print("Pillow fehlt (pip install pillow)")
        return 2
    os.makedirs(a.out, exist_ok=True)
    erg = []
    for name, fn in (("menue", lauf_menue), ("partie", lauf_partie)):
        if a.nur and a.nur != name:
            continue
        try:
            fn(a, erg)
        except Fehler as e:
            pruefe(erg, name, False, str(e))
    fehler = [e for e in erg if not e[1]]
    print(f"TASTATUR_TEST_FERTIG {len(erg) - len(fehler)}/{len(erg)} bestanden")
    return 0 if not fehler else 1


if __name__ == "__main__":
    sys.exit(main())
