#!/usr/bin/env python3
# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
"""Bildvergleich fuer Render-Tests (DX11-Portierung).

Vergleicht zwei Screenshots (oder zwei Ordner mit gleichnamigen Screenshots) und gibt PSNR und
den Anteil abweichender Pixel aus. Optional wird ein Differenzbild geschrieben: das Referenzbild
in Graustufen, abgedunkelt, und jedes Pixel ueber der Schwelle rot markiert (Helligkeit nach
Groesse der Abweichung).

Nur Standardbibliothek. Gelesen werden unkomprimierte BMP-Dateien mit 24 oder 32 Bit (so wie der
Backbuffer-Screenshot des Spiels sie schreibt), bottom-up und top-down.

Aufruf:
    python tools/render/compare_shots.py referenz.bmp neu.bmp
    python tools/render/compare_shots.py ref_ordner neu_ordner --diff diff_ordner
    python tools/render/compare_shots.py a.bmp b.bmp --min-psnr 40 --max-diff-ratio 0.001
    python tools/render/compare_shots.py ref neu --ignore 0,0,30%,12%   # Bildbereich ausnehmen
    python tools/render/compare_shots.py --selftest

Rueckgabewert: 0 = alle Vergleiche innerhalb der Toleranz, 1 = mindestens einer ausserhalb,
2 = Fehler (Datei fehlt, Groessen verschieden, Format nicht lesbar).

Toleranz: Standard ist PSNR >= 40 dB und hoechstens 0,1 % Pixel mit einer Kanalabweichung ueber
8 (von 255). Die Werte sind vorlaeufig; P-0.4 legt sie nach zwei D3D9-Laeufen derselben Szene
endgueltig fest.

Bereiche ausnehmen (--ignore, mehrfach angebbar): X,Y,BREITE,HOEHE. Jede Zahl entweder in Pixeln
oder mit nachgestelltem % als Anteil der Bildbreite bzw. -hoehe -- Prozent ist vorzuziehen, weil
dieselbe Maske dann fuer jede Aufloesung gilt. Ausgenommene Pixel gehen weder in PSNR noch in die
Abweichungsquote ein und sind im Differenzbild blau hinterlegt.

Wozu: die Menue-Einblendung der Shell-Map erscheint nicht deterministisch (TODO Nr. 16) -- sie
haengt an Fensterposition, Mausbewegung und CPU-Last. Unter Last fehlte sie bei Frame 900, im
Wiederholungslauf war sie da; die 3D-Szene darunter ist pixelidentisch. Ohne Maske faellt der
ganze Vergleich deshalb gelegentlich durch, obwohl nichts kaputt ist. Mit --ignore laesst sich
genau dieser Bereich herausnehmen, statt die Toleranz fuer das ganze Bild aufzuweichen.
"""
import argparse
import json
import math
import os
import struct
import sys
import tempfile

DEFAULT_THRESHOLD = 8
DEFAULT_MIN_PSNR = 40.0
DEFAULT_MAX_DIFF_RATIO = 0.001


# --- Auszunehmende Bereiche ------------------------------------------------------------------------
class Rect:
    """Rechteck in Pixeln oder Prozent; die Aufloesung ist erst beim Vergleich bekannt."""

    def __init__(self, spec):
        parts = [p.strip() for p in spec.split(",")]
        if len(parts) != 4:
            raise ValueError(f"--ignore {spec}: vier Werte erwartet (X,Y,BREITE,HOEHE)")
        self.spec = spec
        self.values = []
        for p in parts:
            if p.endswith("%"):
                self.values.append((float(p[:-1]), True))
            else:
                self.values.append((float(p), False))
        if self.values[2][0] <= 0 or self.values[3][0] <= 0:
            raise ValueError(f"--ignore {spec}: Breite und Hoehe muessen groesser als null sein")

    def resolve(self, width, height):
        """Liefert (x0, y0, x1, y1), auf das Bild beschnitten; x1/y1 sind exklusiv."""
        ref = (width, height, width, height)
        out = []
        for (value, percent), base in zip(self.values, ref):
            out.append(int(round(value / 100.0 * base)) if percent else int(round(value)))
        x, y, w, h = out
        x0, y0 = max(0, x), max(0, y)
        x1, y1 = min(width, x + w), min(height, y + h)
        return (x0, y0, x1, y1) if x1 > x0 and y1 > y0 else None

    def __str__(self):
        return self.spec


def build_mask(width, height, rects):
    """Ein Byte je Pixel: 1 = ausgenommen. None, wenn nichts auszunehmen ist."""
    if not rects:
        return None, 0
    mask = bytearray(width * height)
    for r in rects:
        box = r.resolve(width, height)
        if box is None:
            continue
        x0, y0, x1, y1 = box
        span = bytes([1]) * (x1 - x0)
        for y in range(y0, y1):
            start = y * width + x0
            mask[start:start + (x1 - x0)] = span
    masked = sum(mask)
    return (mask, masked) if masked else (None, 0)


# --- BMP ------------------------------------------------------------------------------------------
class Image:
    """Bild als zusammenhaengende BGR-Bytes, Zeilen von oben nach unten."""

    def __init__(self, width, height, bgr):
        self.width = width
        self.height = height
        self.bgr = bgr  # bytes/bytearray, Laenge width*height*3

    def pixel(self, x, y):
        i = (y * self.width + x) * 3
        return self.bgr[i + 2], self.bgr[i + 1], self.bgr[i]


def read_bmp(path):
    with open(path, "rb") as f:
        data = f.read()
    if len(data) < 54 or data[:2] != b"BM":
        raise ValueError(f"{path}: keine BMP-Datei")
    pixel_offset = struct.unpack_from("<I", data, 10)[0]
    header_size = struct.unpack_from("<I", data, 14)[0]
    if header_size < 40:
        raise ValueError(f"{path}: BMP-Kopf zu alt ({header_size} Byte)")
    width, height, planes, bpp, compression = struct.unpack_from("<iiHHI", data, 18)
    if planes != 1 or bpp not in (24, 32):
        raise ValueError(f"{path}: nur 24/32 Bit werden unterstuetzt (hat {bpp} Bit)")
    # BI_RGB (0); BI_BITFIELDS (3) nur bei 32 Bit mit Standardmasken (BGRA) akzeptieren
    if compression not in (0, 3) or (compression == 3 and bpp != 32):
        raise ValueError(f"{path}: komprimierte BMP nicht unterstuetzt (compression={compression})")
    top_down = height < 0
    height = abs(height)
    if width <= 0 or height == 0:
        raise ValueError(f"{path}: ungueltige Groesse {width}x{height}")
    bytes_pp = bpp // 8
    row_size = ((width * bytes_pp + 3) // 4) * 4
    needed = pixel_offset + row_size * height
    if len(data) < needed:
        raise ValueError(f"{path}: Datei zu kurz ({len(data)} statt {needed} Byte)")
    out = bytearray(width * height * 3)
    for row in range(height):
        src_row = row if top_down else height - 1 - row
        start = pixel_offset + src_row * row_size
        line = data[start:start + width * bytes_pp]
        if bytes_pp == 3:
            out[row * width * 3:(row + 1) * width * 3] = line
        else:
            # BGRA -> BGR: Kanaele per Slice-Kopie umsortieren, Alpha weglassen
            bgr = bytearray(width * 3)
            bgr[0::3] = line[0::4]
            bgr[1::3] = line[1::4]
            bgr[2::3] = line[2::4]
            dst = row * width * 3
            out[dst:dst + width * 3] = bgr
    return Image(width, height, out)


def write_bmp(path, image):
    row_size = ((image.width * 3 + 3) // 4) * 4
    pad = b"\0" * (row_size - image.width * 3)
    pixel_bytes = row_size * image.height
    header = struct.pack("<2sIHHI", b"BM", 54 + pixel_bytes, 0, 0, 54)
    info = struct.pack("<IiiHHIIiiII", 40, image.width, image.height, 1, 24, 0, pixel_bytes, 2835, 2835, 0, 0)
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    with open(path, "wb") as f:
        f.write(header)
        f.write(info)
        for row in range(image.height - 1, -1, -1):  # bottom-up
            start = row * image.width * 3
            f.write(image.bgr[start:start + image.width * 3])
            f.write(pad)


# --- Vergleich ------------------------------------------------------------------------------------
BRIGHTNESS_WARN_PCT = 3.0   # Helligkeitswarnung ab dieser Abweichung der mittleren Luminanz


def compare_images(ref, new, threshold=DEFAULT_THRESHOLD, diff_path=None, ignore=None):
    if (ref.width, ref.height) != (new.width, new.height):
        raise ValueError(f"Groessen verschieden: {ref.width}x{ref.height} gegen {new.width}x{new.height}")
    a, b = ref.bgr, new.bgr
    n_pixels = ref.width * ref.height
    mask, n_masked = build_mask(ref.width, ref.height, ignore)
    n_compared = n_pixels - n_masked
    if n_compared == 0:
        raise ValueError("die ausgenommenen Bereiche decken das ganze Bild ab")
    sq_sum = 0
    max_diff = 0
    over = 0
    lum_a = 0      # Helligkeit (2026-09-24): Summe der Luminanz (BT.601-Gewichte) der verglichenen Pixel
    lum_b = 0
    diff_img = bytearray(n_pixels * 3) if diff_path else None
    # Kanalweise Differenzen; pro Pixel zaehlt die groesste Kanalabweichung
    for p in range(n_pixels):
        i = p * 3
        if mask is not None and mask[p]:
            if diff_img is not None:      # ausgenommen: blau hinterlegt
                gray = (a[i] * 29 + a[i + 1] * 150 + a[i + 2] * 77) >> 8
                diff_img[i] = min(255, 96 + gray // 2)
                diff_img[i + 1] = gray // 4
                diff_img[i + 2] = gray // 4
            continue
        lum_a += a[i] * 29 + a[i + 1] * 150 + a[i + 2] * 77
        lum_b += b[i] * 29 + b[i + 1] * 150 + b[i + 2] * 77
        d0 = a[i] - b[i]
        d1 = a[i + 1] - b[i + 1]
        d2 = a[i + 2] - b[i + 2]
        sq_sum += d0 * d0 + d1 * d1 + d2 * d2
        m = max(abs(d0), abs(d1), abs(d2))
        if m > max_diff:
            max_diff = m
        if m > threshold:
            over += 1
        if diff_img is not None:
            if m > threshold:
                diff_img[i] = 0
                diff_img[i + 1] = 0
                diff_img[i + 2] = min(255, 96 + m * 4)
            else:
                gray = (a[i] * 29 + a[i + 1] * 150 + a[i + 2] * 77) >> 8
                g = gray // 3
                diff_img[i] = diff_img[i + 1] = diff_img[i + 2] = g
    mse = sq_sum / (n_compared * 3)
    bright_ref = lum_a / (256.0 * n_compared)
    bright_new = lum_b / (256.0 * n_compared)
    bright_delta = 0.0 if bright_ref == bright_new else (bright_new - bright_ref) / max(bright_ref, 1.0) * 100.0
    psnr = math.inf if mse == 0 else 10.0 * math.log10(255.0 * 255.0 / mse)
    if diff_img is not None:
        write_bmp(diff_path, Image(ref.width, ref.height, diff_img))
    return {
        "width": ref.width,
        "height": ref.height,
        "psnr": psnr,
        "mse": mse,
        "max_channel_diff": max_diff,
        "pixels_over_threshold": over,
        "diff_ratio": over / n_compared,
        "pixels_compared": n_compared,
        "pixels_ignored": n_masked,
        "brightness_ref": bright_ref,        # mittlere Luminanz 0..255
        "brightness_new": bright_new,
        "brightness_delta_pct": bright_delta,
    }


def within_tolerance(result, min_psnr, max_diff_ratio):
    return result["psnr"] >= min_psnr and result["diff_ratio"] <= max_diff_ratio


def format_psnr(psnr):
    return "identisch" if math.isinf(psnr) else f"{psnr:.2f} dB"


def collect_pairs(ref, new):
    """Liefert (Name, Referenzpfad, Neupfad) fuer Datei- oder Ordnervergleich."""
    if os.path.isdir(ref) != os.path.isdir(new):
        raise ValueError("beide Argumente muessen Dateien oder beide Ordner sein")
    if not os.path.isdir(ref):
        return [(os.path.basename(new), ref, new)], [], []
    ref_names = {n for n in os.listdir(ref) if n.lower().endswith(".bmp")}
    new_names = {n for n in os.listdir(new) if n.lower().endswith(".bmp")}
    pairs = [(n, os.path.join(ref, n), os.path.join(new, n)) for n in sorted(ref_names & new_names)]
    return pairs, sorted(ref_names - new_names), sorted(new_names - ref_names)


def run_compare(args):
    try:
        pairs, only_ref, only_new = collect_pairs(args.reference, args.candidate)
    except (OSError, ValueError) as e:
        print(f"FEHLER: {e}")
        return 2
    ignore = getattr(args, "ignore", None) or []
    report = {"threshold": args.threshold, "min_psnr": args.min_psnr,
              "max_diff_ratio": args.max_diff_ratio, "results": [],
              "ignored_regions": [str(r) for r in ignore],
              "only_in_reference": only_ref, "only_in_candidate": only_new}
    if ignore:
        print("Ausgenommen: " + ", ".join(str(r) for r in ignore))
    worst = 0
    for name in only_ref:
        print(f"FEHLT    {name} (nur in der Referenz)")
        worst = 2
    for name in only_new:
        print(f"NEU      {name} (keine Referenz)")
    if not pairs and not only_ref:
        print("FEHLER: keine BMP-Dateien zum Vergleichen gefunden")
        return 2
    for name, ref_path, new_path in pairs:
        diff_path = None
        if args.diff:
            diff_path = args.diff if len(pairs) == 1 and args.diff.lower().endswith(".bmp") \
                else os.path.join(args.diff, os.path.splitext(name)[0] + "_diff.bmp")
        try:
            r = compare_images(read_bmp(ref_path), read_bmp(new_path), args.threshold, diff_path, ignore)
        except (OSError, ValueError) as e:
            print(f"FEHLER   {name}: {e}")
            report["results"].append({"name": name, "error": str(e)})
            worst = 2
            continue
        ok = within_tolerance(r, args.min_psnr, args.max_diff_ratio)
        status = "OK      " if ok else "ABWEICHT"
        print(f"{status} {name}: PSNR {format_psnr(r['psnr'])}, "
              f"{r['pixels_over_threshold']} Pixel ueber Schwelle ({r['diff_ratio'] * 100:.4f} %), "
              f"max. Kanalabweichung {r['max_channel_diff']}"
              + (f", {r['pixels_ignored']} Pixel ausgenommen" if r["pixels_ignored"] else "")
              + (f", Helligkeit {r['brightness_delta_pct']:+.1f} %" if r["brightness_delta_pct"] else "")
              + (f", Differenzbild {diff_path}" if diff_path else ""))
        # Nutzerwunsch 2026-09-24: Bilder, die insgesamt deutlich heller oder dunkler sind als die Referenz,
        # eigens melden -- auch wenn die Pixelschwelle sie als gewollte Aenderung durchlaesst. Nur Warnung.
        if abs(r["brightness_delta_pct"]) >= BRIGHTNESS_WARN_PCT:
            print(f"HELL     {name}: mittlere Helligkeit {r['brightness_ref']:.1f} -> {r['brightness_new']:.1f} "
                  f"({r['brightness_delta_pct']:+.1f} %, Warnschwelle {BRIGHTNESS_WARN_PCT:.0f} %)")
        r.update(name=name, ok=ok, diff_image=diff_path)
        if math.isinf(r["psnr"]):
            r["psnr"] = None  # JSON kennt kein Unendlich; None = identisch
        report["results"].append(r)
        if not ok:
            worst = max(worst, 1)
    if args.json:
        with open(args.json, "w", encoding="utf-8") as f:
            json.dump(report, f, indent=2, ensure_ascii=False)
    return worst


# --- Selbsttest -----------------------------------------------------------------------------------
def selftest():
    w, h = 37, 23  # ungerade Breite -> Zeilen-Padding wird mitgetestet
    base = bytearray()
    for y in range(h):
        for x in range(w):
            base += bytes(((x * 7) & 255, (y * 11) & 255, ((x + y) * 5) & 255))
    ref = Image(w, h, bytes(base))
    failures = []

    def check(cond, msg):
        if not cond:
            failures.append(msg)

    with tempfile.TemporaryDirectory() as tmp:
        p_ref = os.path.join(tmp, "ref.bmp")
        write_bmp(p_ref, ref)
        back = read_bmp(p_ref)
        check((back.width, back.height, bytes(back.bgr)) == (w, h, bytes(ref.bgr)), "24-Bit-Rundreise")

        # 32 Bit top-down von Hand schreiben und lesen
        p32 = os.path.join(tmp, "ref32.bmp")
        with open(p32, "wb") as f:
            size = w * h * 4
            f.write(struct.pack("<2sIHHI", b"BM", 54 + size, 0, 0, 54))
            f.write(struct.pack("<IiiHHIIiiII", 40, w, -h, 1, 32, 0, size, 0, 0, 0, 0))
            for p in range(w * h):
                f.write(ref.bgr[p * 3:p * 3 + 3] + b"\xff")
        check(bytes(read_bmp(p32).bgr) == bytes(ref.bgr), "32-Bit-top-down lesen")

        r = compare_images(ref, back)
        check(math.isinf(r["psnr"]) and r["pixels_over_threshold"] == 0, "identische Bilder")

        changed = bytearray(ref.bgr)
        for p in range(0, w * h, 50):  # jedes 50. Pixel stark veraendern
            changed[p * 3 + 2] = (changed[p * 3 + 2] + 100) & 255
        diff_path = os.path.join(tmp, "diff.bmp")
        r = compare_images(ref, Image(w, h, bytes(changed)), diff_path=diff_path)
        expected = len(range(0, w * h, 50))
        check(r["pixels_over_threshold"] == expected, f"abweichende Pixel {r['pixels_over_threshold']} statt {expected}")
        check(r["psnr"] < 40, "starke Abweichung unter 40 dB")
        check(os.path.isfile(diff_path) and read_bmp(diff_path).width == w, "Differenzbild geschrieben")

        noise = bytearray(ref.bgr)
        for i in range(0, len(noise), 3):
            noise[i] = min(255, noise[i] + 1)  # +1 im Blaukanal: unter Schwelle, hoher PSNR
        r = compare_images(ref, Image(w, h, bytes(noise)))
        check(r["pixels_over_threshold"] == 0 and r["psnr"] > 40, "Rauschen unter der Schwelle")

        try:
            compare_images(ref, Image(w + 1, h, bytes((w + 1) * h * 3)))
            check(False, "Groessenpruefung")
        except ValueError:
            pass

        # --- ausgenommene Bereiche ---------------------------------------------------------------
        check(Rect("0,0,10,5").resolve(w, h) == (0, 0, 10, 5), "Rechteck in Pixeln")
        check(Rect("0,0,100%,50%").resolve(40, 20) == (0, 0, 40, 10), "Rechteck in Prozent")
        check(Rect("30,20,100,100").resolve(w, h) == (30, 20, w, h), "Rechteck wird beschnitten")
        check(Rect("100,0,10,10").resolve(w, h) is None, "Rechteck ausserhalb des Bildes")
        try:
            Rect("1,2,3")
            check(False, "Rechteck mit zu wenig Werten")
        except ValueError:
            pass
        try:
            Rect("0,0,0,10")
            check(False, "Rechteck ohne Flaeche")
        except ValueError:
            pass

        # Ein Bild, das nur oben links abweicht: ohne Maske durchgefallen, mit Maske sauber.
        corner = bytearray(ref.bgr)
        for y in range(6):
            for x in range(12):
                corner[(y * w + x) * 3 + 2] = (corner[(y * w + x) * 3 + 2] + 120) & 255
        cand = Image(w, h, bytes(corner))
        r_plain = compare_images(ref, cand)
        check(r_plain["pixels_over_threshold"] == 72, f"ohne Maske 72 Pixel, nicht {r_plain['pixels_over_threshold']}")
        check(not within_tolerance(r_plain, DEFAULT_MIN_PSNR, DEFAULT_MAX_DIFF_RATIO), "ohne Maske ausserhalb der Toleranz")

        r_mask = compare_images(ref, cand, ignore=[Rect("0,0,12,6")])
        check(math.isinf(r_mask["psnr"]), "mit Maske identisch")
        check(r_mask["pixels_over_threshold"] == 0, "mit Maske keine Abweichung")
        check(r_mask["pixels_ignored"] == 72, f"72 Pixel ausgenommen, nicht {r_mask['pixels_ignored']}")
        check(r_mask["pixels_compared"] == w * h - 72, "verglichene Pixel")
        check(within_tolerance(r_mask, DEFAULT_MIN_PSNR, DEFAULT_MAX_DIFF_RATIO), "mit Maske innerhalb der Toleranz")

        # Eine zu kleine Maske deckt den Unterschied nicht zu -- die Quote zaehlt gegen die
        # verglichenen Pixel, nicht gegen alle.
        r_part = compare_images(ref, cand, ignore=[Rect("0,0,6,6")])
        check(r_part["pixels_over_threshold"] == 36, f"halbe Maske 36 Pixel, nicht {r_part['pixels_over_threshold']}")
        check(abs(r_part["diff_ratio"] - 36 / (w * h - 36)) < 1e-12, "Quote gegen die verglichenen Pixel")

        # Zwei Bereiche zusammen, mit Ueberlappung: doppelt gezaehlt wird nicht.
        r_two = compare_images(ref, cand, ignore=[Rect("0,0,12,3"), Rect("0,0,12,6")])
        check(r_two["pixels_ignored"] == 72, f"ueberlappende Bereiche einmal gezaehlt, nicht {r_two['pixels_ignored']}")

        # Differenzbild mit Maske: der ausgenommene Bereich ist blau, nicht rot.
        p_masked = os.path.join(tmp, "diff_masked.bmp")
        compare_images(ref, cand, diff_path=p_masked, ignore=[Rect("0,0,12,6")])
        dm = read_bmp(p_masked)
        r0, g0, b0 = dm.pixel(0, 0)
        check(b0 > r0 and b0 > g0, "ausgenommener Bereich im Differenzbild blau")

        try:
            compare_images(ref, cand, ignore=[Rect("0,0,100%,100%")])
            check(False, "ganz abgedeckte Maske")
        except ValueError:
            pass

    if failures:
        for m in failures:
            print(f"SELBSTTEST FEHLGESCHLAGEN: {m}")
        return 2
    print("Selbsttest ok")
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("reference", nargs="?", help="Referenzbild oder -ordner")
    ap.add_argument("candidate", nargs="?", help="neues Bild oder neuer Ordner")
    ap.add_argument("--diff", help="Differenzbild (.bmp) bzw. Ordner fuer Differenzbilder")
    ap.add_argument("--threshold", type=int, default=DEFAULT_THRESHOLD,
                    help=f"Kanalabweichung, ab der ein Pixel als abweichend zaehlt (Standard {DEFAULT_THRESHOLD})")
    ap.add_argument("--min-psnr", type=float, default=DEFAULT_MIN_PSNR,
                    help=f"Mindest-PSNR in dB (Standard {DEFAULT_MIN_PSNR})")
    ap.add_argument("--max-diff-ratio", type=float, default=DEFAULT_MAX_DIFF_RATIO,
                    help=f"hoechster Anteil abweichender Pixel, 0..1 (Standard {DEFAULT_MAX_DIFF_RATIO})")
    ap.add_argument("--ignore", action="append", type=Rect, metavar="X,Y,B,H", default=[],
                    help="Bildbereich vom Vergleich ausnehmen; mehrfach angebbar. Werte in Pixeln "
                         "oder mit %% als Anteil (z. B. 0,0,30%%,12%%)")
    ap.add_argument("--json", help="Ergebnis zusaetzlich als JSON-Datei schreiben")
    ap.add_argument("--selftest", action="store_true", help="eingebauten Selbsttest ausfuehren")
    args = ap.parse_args()
    if args.selftest:
        return selftest()
    if not args.reference or not args.candidate:
        ap.error("Referenz und Kandidat angeben (oder --selftest)")
    return run_compare(args)


if __name__ == "__main__":
    sys.exit(main())
