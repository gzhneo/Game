#!/usr/bin/env python3
# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
"""Erzeugt die Texturstufen HD und UHD aus der eigenen Installation.

Liest die Archive eines Asset-Verzeichnisses (nur lesend), bildet den wirksamen Texturbestand wie das
Spiel (erster Treffer gewinnt, Zero Hour vor den Generals-Basisarchiven), vergroessert jede geeignete
Textur um den Faktor der Stufe (HD 2x, UHD 4x je Achse, HD-2), komprimiert sie wieder (DXT1/DXT5 mit
voller Mip-Kette) und packt das Ergebnis in BIG-Archive unter

    <Asset-Verzeichnis>\\Texturstufen\\HD\\TexturesHD.big      bzw. ...\\UHD\\TexturesUHD*.big

(HD-12). Das Spiel laedt diese Archive mit Vorrang (ArchiveFileSystem::loadTextureTiers, Phase 2);
jede Textur, die hier fehlt, bleibt die naechstniedrigere Stufe.

Nichts davon verlaesst den Rechner: Die Texturen sind EAs Eigentum, die erzeugten Dateien sind
Bearbeitungen und gehoeren weder ins Repo noch in Releases.

Aufruf:
    python tools/assets/build_texture_tiers.py --game zh --tier hd
    python tools/assets/build_texture_tiers.py --game generals --tier uhd --jobs 8
    python tools/assets/build_texture_tiers.py --game zh --tier hd --only-restore   # nur die 471 ZH-Halbierten
    python tools/assets/build_texture_tiers.py --selftest                           # ohne EA-Daten (Pruefgang)
    python tools/assets/build_texture_tiers.py --game zh --tier hd --stage HD-KI2-BC7 --gelaende --bc7 alle \\
        --texconv <Werkzeuge>\\directxtex\\texconv.exe --out <Werkzeuge>\\stufen_bc7\\zh \\
        --ai-reuse <Asset-Verzeichnis>\\Texturstufen\\Arbeit\\HD-KI2\\ki --ai-license "..."

Verfahren (Abschnitt 5.2 des Plans):
  * ausgeschlossen: Gelaende (Art\\Terrain, Kachelraster fest auf 64 Pixel, Phase 5), Baumtexturen
    (W3DTreeDraw TextureName, ebenfalls 64er-Kacheln), Schattendecals (ShadowTexture: Groesse in Texeln
    bestimmt die Weltgroesse), Teamfarben ZHC*, Wasser-Kaustik caust*, Kartenvorschauen, Texturen unter
    16 Pixel Kantenlaenge. Wasser- und Himmelstextur aus Water.ini sind dabei: die Engine rechnet die Himmel-Kachelung jetzt in Texeln der Originalgroesse;
  * Zero Hour: die Texturen, die Zero Hour nur in halber Aufloesung hat, kommen fuer HD unveraendert aus
    ZH_Generals\\Textures.big (Abschnitt 2.3, HD-9), fuer UHD aus deren Vergroesserung;
  * klassisch (Vorgabe, HD-3): leichtes Glaetten der DXT-Bloecke, Lanczos, leichtes Nachschaerfen.
    Kachelbare Achsen (Kantenvergleich) werden mit Wiederholung aufgefuellt, sonst gespiegelt, damit
    keine Naehte entstehen. Alpha getrennt: DXT1-Alpha bleibt 1 Bit (Schwelle), die Mip-Stufen halten
    die Abdeckung; DXT5-Alpha weich. Farbe unter transparenten Pixeln wird vorher aufgefuellt;
  * KI optional (--ai-cmd): externes Programm mit Platzhaltern {in} {out} {scale}; nur fuer die Arten
    aus --ai-kinds (Vorgabe Modell), nie fuer Oberflaeche mit Text im Bild;
  * KI im Stapel (--ai-exe, erprobt 2026-09-23): realesrgan-ncnn-vulkan (github.com/xinntao/Real-ESRGAN,
    Release v0.2.5.0, BSD-3-Clause), Modell --ai-model (Vorgabe realesrgan-x4plus, rechnet 4x; fuer HD
    danach Lanczos auf 2x). Ein Aufruf fuer alle Texturen: Durchgang 1 legt die Eingaben ab (nur RGB,
    Farbe unter transparenten Pixeln aufgefuellt, 8 Pixel Rand durch Wiederholung an kachelbaren Kanten,
    sonst gespiegelt), dann laeuft das Programm einmal ueber den Ordner, Durchgang 2 schneidet den Rand
    ab und setzt das Alpha des klassischen Wegs ein. Faellt das KI-Ergebnis im Rueckvergleich durch,
    gilt das klassische. --stage legt eine eigene Stufe an (z. B. HD-KI), ohne HD zu ueberschreiben;
  * Rueckvergleich: Ergebnis auf SD-Groesse verkleinert gegen das Original (PSNR); unter --min-psnr
    bleibt die Textur SD und das Manifest vermerkt es;
  * Formate: DXT1-Quellen und TGA ohne Alpha nach DXT1, alles mit Alpha nach DXT5. Ausnahme Oberflaeche aus
    TGA: bleibt unkomprimiertes TGA wie das Original -- DXT verfaelscht die ein bis zwei Pixel breiten
    Leuchtlinien der Rahmen;
  * BC7 (TODO 58, 2026-09-26): --bc7 alle|alpha schreibt DDS mit DX10-Kopf (DXGI_FORMAT_BC7_UNORM, wie DXT
    ohne sRGB) statt DXT1/DXT5 -- "alpha" nur statt DXT5 (gleicher Speicher), "alle" auch statt DXT1 (doppelter
    Speicher). Kodiert wird mit texconv aus DirectXTex (MIT, github.com/microsoft/DirectXTex, --texconv), auf
    der Grafikkarte (DirectCompute), --bc7-cpu erzwingt die CPU (etwa 40-mal langsamer). Stapelbetrieb wie bei
    der KI: Durchgang 1 legt je Textur die unkomprimierte Mip-Kette als DDS ab (Mips aus diesem Werkzeug, also
    mit erhaltener Alpha-Abdeckung), ein texconv-Lauf kodiert alle, Durchgang 2 packt. Das Manifest nennt das
    Format je Textur und fuer den Satz ("texture_format"); je Textur steht dazu das PSNR von Stufe 0 gegen die
    unkomprimierte Vorlage, bei BC7 auch das der DXT-Fassung (psnr_vorlage, psnr_vorlage_dxt). Archive eines
    BC7-Satzes tragen "BC7" im Namen -- unter Direct3D 9 laedt die Engine sie nicht (TextureTiers.cpp);
  * --ai-reuse <ki-Ordner>: fertige KI-Ausgaben eines anderen Satzes (ki\\aus) nur lesen, ohne KI-Lauf -- so
    entsteht etwa HD-KI2 als BC7 aus denselben KI-Bildern wie die DXT-Fassung;
  * Oberflaeche je MappedImage: die Atlanten bestehen aus vielen kleinen Teilbildern (Rahmenstuecke von
    5x5 Pixeln), die wiederholt gekachelt werden. Jedes Teilbild wird fuer sich mit Randwiederholung
    vergroessert, sonst bluten die Nachbarn ueber die Kanten und jede Kachel bekommt eine Naht;
  * wiederaufnehmbar: je Textur ein Zwischenergebnis unter Texturstufen\\Arbeit\\<Stufe>; ein Neustart
    ueberspringt, was mit denselben Quellen und Parametern schon fertig ist; parallel ueber --jobs.

Braucht numpy und Pillow (ab 11.2, DDS-Kodierung). Der Selbsttest laeuft ohne beides (dann nur die
Teile ohne Bildverarbeitung) und ohne EA-Daten.
"""
import argparse
import hashlib
import io
import json
import os
import re
import struct
import sys
import tempfile
from concurrent.futures import ProcessPoolExecutor

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import texture_inventory as inv  # noqa: E402

try:
    import numpy as np
    from PIL import Image
    HAVE_IMAGING = True
except ImportError:  # Selbsttest ohne Bildverarbeitung
    np = None
    Image = None
    HAVE_IMAGING = False

TOOL_VERSION = "2"	# 2: Oberflaeche je MappedImage vergroessert und unkomprimiert (2026-09-23)
FACTORS = {"hd": 2, "uhd": 4}
FOLDERS = {"hd": "HD", "uhd": "UHD"}
MAX_ARCHIVE_BYTES = 1536 * 1024 * 1024	# Offsets im BIG-Format sind im Spiel vorzeichenbehaftet (Int)
MIN_EDGE = 16


# =================================================================================================
# BIG schreiben
# =================================================================================================
def write_big(path, entries):
    """entries: Liste (Name mit Backslash, Bytes). Sortiert ohne Gross-/Kleinschreibung, schreibt atomar."""
    entries = sorted(entries, key=lambda e: e[0].lower())
    directory = b"".join(struct.pack(">II", 0, 0) + name.encode("latin-1") + b"\0" for name, _ in entries)
    header_len = 16 + len(directory)
    offset = header_len
    table = []
    for name, data in entries:
        table.append(struct.pack(">II", offset, len(data)) + name.encode("latin-1") + b"\0")
        offset += len(data)
    total = offset
    tmp = path + ".tmp"
    with open(tmp, "wb") as f:
        f.write(b"BIGF" + struct.pack("<I", total) + struct.pack(">II", len(entries), header_len))
        f.write(b"".join(table))
        for _, data in entries:
            f.write(data)
    os.replace(tmp, path)


def split_archives(entries, limit=MAX_ARCHIVE_BYTES):
    """Teilt nach Namen sortierte Eintraege in Gruppen unter limit Bytes."""
    groups, current, size = [], [], 0
    for name, data in sorted(entries, key=lambda e: e[0].lower()):
        if current and size + len(data) > limit:
            groups.append(current)
            current, size = [], 0
        current.append((name, data))
        size += len(data)
    if current:
        groups.append(current)
    return groups


# =================================================================================================
# DDS
# =================================================================================================
DDSD_FLAGS = 0x1 | 0x2 | 0x4 | 0x1000 | 0x20000 | 0x80000	# CAPS HEIGHT WIDTH PIXELFORMAT MIPMAPCOUNT LINEARSIZE
DDSCAPS = 0x1000 | 0x8 | 0x400000								# TEXTURE COMPLEX MIPMAP


def block_bytes(fourcc):
    return 8 if fourcc == "DXT1" else 16


def dxt_level_size(width, height, fourcc):
    return max(1, (width + 3) // 4) * max(1, (height + 3) // 4) * block_bytes(fourcc)


def mip_dims(width, height):
    dims = [(width, height)]
    while width > 1 or height > 1:
        width, height = max(1, width // 2), max(1, height // 2)
        dims.append((width, height))
    return dims


def dds_header(width, height, mips, fourcc):
    pf = struct.pack("<II4sIIIII", 32, 0x4, fourcc.encode("ascii"), 0, 0, 0, 0, 0)
    head = struct.pack("<IIIIIII", 124, DDSD_FLAGS, height, width, dxt_level_size(width, height, fourcc), 0, mips)
    head += b"\0" * 44 + pf + struct.pack("<IIIII", DDSCAPS, 0, 0, 0, 0)
    assert len(head) == 124
    return b"DDS " + head


DXGI_BC7_UNORM = 98
DXGI_R8G8B8A8_UNORM = 28


def parse_dds(data):
    """(FourCC, Breite, Hoehe, Mip-Stufen, Offset der Daten) eines DDS. Mit DX10-Kopf (BC7): FourCC "BC7",
    die Daten beginnen 20 Byte spaeter."""
    if data[:4] != b"DDS " or struct.unpack("<I", data[4:8])[0] != 124:
        raise ValueError("kein DDS")
    height, width = struct.unpack("<II", data[12:20])
    mips = struct.unpack("<I", data[28:32])[0] or 1
    fourcc = data[84:88].decode("latin-1")
    if fourcc == "DX10":
        dxgi = struct.unpack("<I", data[128:132])[0]
        return ("BC7" if dxgi == DXGI_BC7_UNORM else f"DXGI{dxgi}"), width, height, mips, 148
    return fourcc, width, height, mips, 128


def dds_rgba(levels):
    """Unkomprimiertes DDS (Legacy-Kopf, 32 Bit RGBA = DXGI R8G8B8A8_UNORM) mit der ganzen Mip-Kette: Eingabe
    fuer texconv. Kein sRGB-Merker, texconv rechnet also nichts um (Helligkeit wie DXT)."""
    h, w = levels[0].shape[:2]
    flags = 0x1 | 0x2 | 0x4 | 0x1000 | 0x20000 | 0x8		# CAPS HEIGHT WIDTH PIXELFORMAT MIPMAPCOUNT PITCH
    pf = struct.pack("<II4sIIIII", 32, 0x41, b"\0\0\0\0", 32, 0x000000FF, 0x0000FF00, 0x00FF0000, 0xFF000000)
    head = struct.pack("<IIIIIII", 124, flags, h, w, w * 4, 0, len(levels))
    head += b"\0" * 44 + pf + struct.pack("<IIIII", DDSCAPS, 0, 0, 0, 0)
    return b"DDS " + head + b"".join(np.ascontiguousarray(l, dtype=np.uint8).tobytes() for l in levels)


# =================================================================================================
# Auswahl (ohne Bildverarbeitung)
# =================================================================================================
def ini_values(files, prefix, keys, only_in=None):
    """Werte von Schluesseln (klein) in INIs unter prefix; only_in: nur in Bloecken mit diesem Text."""
    found = set()
    pattern = re.compile(r"^\s*(" + "|".join(keys) + r")\s*=\s*([^\s;]+)", re.I | re.M)
    for name, (_rel, big) in files.items():
        if not (name.startswith(prefix) and name.endswith(".ini")):
            continue
        text = big.read(name).decode("latin-1", "replace")
        found.update(stem_of(m.group(2)) for m in pattern.finditer(text))
    return found


def stem_of(name):
    base = name.replace("/", "\\").rsplit("\\", 1)[-1].lower()
    return base.rsplit(".", 1)[0] if "." in base else base


def exclusion_sets(files):
    """Texturnamen (Stamm), deren Pixelgroesse das Spiel auswertet -- bleiben SD (siehe Kopf)."""
    return {
        TREE_REASON: ini_values(files, "data\\ini\\", ["TextureName"]),
        "Schattendecal (Texel = Weltgroesse)": ini_values(files, "data\\ini\\", ["ShadowTexture"]),
        # Wasser/Himmel (SkyTexture, WaterTexture aus Water.ini) seit 2026-09-26 nicht mehr ausgeschlossen: W3DWater
        # teilt SkyTexelsPerUnit durch die Breite der Originalgroesse (textureTierFactor).
    }


# Pfade, die ein Paketarchiv tragen darf (TextureTiers.cpp, isAllowedPackageEntry; Art\Terrain erst mit Phase 5)
TEXTURE_PATH = re.compile(r"^(art\\textures\\|data\\[^\\]+\\art\\textures\\)")


TERRAIN_PATH = re.compile(r"^art\\terrain\\")
TREE_REASON = "Baum (64er-Kacheln, Phase 5)"


def exclusion_reason(record, excl, terrain=False):
    """terrain: Gelaende- und Baumtexturen mit aufnehmen (braucht die Engine mit Kachel-Massstab, hd-ki2)."""
    name, kind = record["name"], record["kind"]
    stem = stem_of(name)
    if kind == "Gelaende":
        if not terrain:
            return "Gelaende (Kachelraster, Phase 5)"
        if not name.endswith(".tga") or record["format"] not in ("TGA24", "TGA32"):
            return f"Gelaende nur als TGA ({record['format']})"
        return None
    if terrain and stem in excl.get(TREE_REASON, set()) and name.endswith(".tga"):
        return None
    if kind == "Vorschau":
        return "Kartenvorschau"
    if not TEXTURE_PATH.match(name):
        return "Pfad ausserhalb Art\\Textures (Stufenarchive nehmen nur Texturpfade an)"
    if stem.startswith("zhc"):
        return "Teamfarbe ZHC* (Umfaerbung zur Laufzeit)"
    if stem.startswith("caust"):
        return "Wasser-Kaustik (unkomprimiert)"
    if max(record["width"], record["height"]) < MIN_EDGE or min(record["width"], record["height"]) < 4:
        return "zu klein"
    if record["format"] not in ("DXT1", "DXT3", "DXT5", "TGA24", "TGA32"):
        return f"Format {record['format']}"
    for reason, stems in excl.items():
        if stem in stems:
            return reason
    return None


def target_fourcc(src_format, has_alpha, bc7=None):
    """bc7: None/"aus" wie bisher; "alpha" BC7 statt DXT5; "alle" BC7 statt DXT1 und DXT5 (TODO 58)."""
    dxt = "DXT1" if (src_format == "DXT1" or not has_alpha) else "DXT5"
    if bc7 == "alle" or (bc7 == "alpha" and dxt == "DXT5"):
        return "BC7"
    return dxt


def job_output_name(job):
    """Oberflaeche aus TGA bleibt TGA (unkomprimiert), Gelaende- und Baumkacheln bleiben TGA, alles andere .dds."""
    if is_ui_tga(job) or job.get("tiles"):
        return job["name"]
    return output_name(job["name"])


def is_ui_tga(job):
    return job.get("kind") == "Oberflaeche" and job["name"].endswith(".tga")


def mapped_image_rects(files):
    """Textur (Dateiname klein) -> Liste (Left, Top, Right, Bottom, TextureWidth, TextureHeight) aus den
    MappedImage-INIs; Right/Bottom exklusiv wie in Image::parseImageCoords."""
    rects = {}
    block = re.compile(r"^\s*MappedImage\s+\S+(.*?)^\s*End\b", re.S | re.M | re.I)
    for name, (_rel, big) in files.items():
        if not (name.startswith("data\\ini\\mappedimages\\") and name.endswith(".ini")):
            continue
        text = big.read(name).decode("latin-1", "replace")
        for m in block.finditer(text):
            body = m.group(1)
            tex = re.search(r"^\s*Texture\s*=\s*(\S+)", body, re.M | re.I)
            tw = re.search(r"^\s*TextureWidth\s*=\s*(\d+)", body, re.M | re.I)
            th = re.search(r"^\s*TextureHeight\s*=\s*(\d+)", body, re.M | re.I)
            co = re.search(r"Left:\s*(-?\d+)\s+Top:\s*(-?\d+)\s+Right:\s*(-?\d+)\s+Bottom:\s*(-?\d+)", body, re.I)
            if tex and tw and th and co:
                rects.setdefault(tex.group(1).lower(), []).append(
                    tuple(int(v) for v in co.groups()) + (int(tw.group(1)), int(th.group(1))))
    return rects


def output_name(name):
    """Originalpfad, Endung .dds (der Lader versucht .dds vor .tga)."""
    base = name.rsplit(".", 1)[0]
    return base + ".dds"


def make_plan(root, game, tier, only_restore=False, terrain=False):
    """Liste der Auftraege und der Ausschluesse. Ein Auftrag: dict mit name, kind, Quelle, Faktor."""
    records, _per_archive, halved = inv.inventory(root, game)
    files, opened = inv.effective_files(root)
    excl = exclusion_sets(files)
    ui_rects = mapped_image_rects(files)
    halved_names = {h["name"] for h in halved}
    base_rel = next((r for r in opened if r.lower().endswith("zh_generals\\textures.big")), None)
    factor = FACTORS[tier]
    jobs, skipped = [], []
    for r in records:
        reason = exclusion_reason(r, excl, terrain)
        if reason:
            skipped.append({"name": r["name"], "reason": reason})
            continue
        restore = game == "zh" and r["name"] in halved_names and base_rel is not None
        if only_restore and not restore:
            continue
        job = {
            "name": r["name"], "kind": r["kind"], "format": r["format"], "width": r["width"], "height": r["height"],
            "archive": r["archive"], "source_archive": r["archive"], "factor": factor,
            "target_width": r["width"] * factor, "target_height": r["height"] * factor, "restore": False,
            "tier_factor": factor,
        }
        if terrain and (r["kind"] == "Gelaende" or stem_of(r["name"]) in excl.get(TREE_REASON, set())):
            job["tiles"] = True		# Kachel-TGA mit Massstab-Kennung, Engine liest 64*s-Kacheln
            if stem_of(r["name"]) in excl.get(TREE_REASON, set()):
                job["kind"] = "Baum"		# Baumtexturen liegen meist unter Art\\Terrain
        if r["kind"] == "Oberflaeche":
            fn = r["name"].rsplit("\\", 1)[-1]
            stem = fn.rsplit(".", 1)[0]
            found = ui_rects.get(fn) or ui_rects.get(stem + ".tga") or ui_rects.get(stem + ".dds") or []
            px = []
            for left, top, right, bottom, tw, th in found:
                sx, sy = r["width"] / float(tw or r["width"]), r["height"] / float(th or r["height"])
                px.append((int(round(left * sx)), int(round(top * sy)), int(round(right * sx)), int(round(bottom * sy))))
            job["rects"] = sorted(set(px))
        if restore:
            # Generals-Fassung ist doppelt so gross: fuer HD unveraendert, fuer UHD um factor/2 vergroessern
            job.update(source_archive=base_rel, restore=True, factor=factor // 2,
                       format=inv.texture_header(opened[base_rel].read_head(r["name"], 128), r["name"])[0])
        jobs.append(job)
    return jobs, skipped, [r["name"] for r in records]


# =================================================================================================
# Bildverarbeitung (numpy, Pillow)
# =================================================================================================
def decode(data, name):
    img = Image.open(io.BytesIO(data))
    img.load()
    return np.array(img.convert("RGBA"), dtype=np.uint8)


def resize_f(channel, width, height, resample=None):
    """float32-Kanal mit Pillow (Modus F) skalieren."""
    img = Image.fromarray(np.ascontiguousarray(channel, dtype=np.float32), mode="F")
    return np.array(img.resize((width, height), resample if resample is not None else Image.LANCZOS), dtype=np.float32)


def blur_f(channel, sigma):
    """Gauss-Weichzeichner (getrennt, Rand gespiegelt) fuer float32-Kanaele; Pillow kann das nicht in Modus F."""
    radius = max(1, int(3.0 * sigma + 0.5))
    x = np.arange(-radius, radius + 1, dtype=np.float32)
    k = np.exp(-(x * x) / (2.0 * sigma * sigma))
    k /= k.sum()
    out = np.asarray(channel, dtype=np.float32)
    for axis in (0, 1):
        pad = [(0, 0), (0, 0)]
        pad[axis] = (radius, radius)
        p = np.pad(out, pad, mode="symmetric")
        n = out.shape[axis]
        acc = np.zeros_like(out)
        for i, w in enumerate(k):
            acc += w * (p[i:i + n, :] if axis == 0 else p[:, i:i + n])
        out = acc
    return out


def is_tileable(rgb, axis):
    """Kante gegen Gegenkante: liegt der Sprung ueber den Rand im Rahmen der inneren Spruenge?"""
    a = rgb.astype(np.float32)
    if a.shape[axis] < 4:
        return False
    if axis == 1:
        edge = np.abs(a[:, 0] - a[:, -1]).mean()
        inner = np.abs(a[:, 1:] - a[:, :-1]).mean()
    else:
        edge = np.abs(a[0] - a[-1]).mean()
        inner = np.abs(a[1:] - a[:-1]).mean()
    return bool(edge <= 1.5 * inner + 2.0)


def bleed_color(rgba, iterations=8):
    """Farbe transparenter Pixel aus den Nachbarn auffuellen, damit beim Skalieren kein Schwarz einblutet."""
    rgb = rgba[..., :3].astype(np.float32)
    known = (rgba[..., 3] >= 8).astype(np.float32)
    if known.all() or not known.any():
        return rgb
    acc = rgb * known[..., None]
    w = known.copy()
    for _ in range(iterations):
        acc_p = np.pad(acc, ((1, 1), (1, 1), (0, 0)), mode="edge")
        w_p = np.pad(w, 1, mode="edge")
        s = sum(acc_p[1 + dy:acc_p.shape[0] - 1 + dy, 1 + dx:acc_p.shape[1] - 1 + dx] for dy in (-1, 0, 1) for dx in (-1, 0, 1))
        n = sum(w_p[1 + dy:w_p.shape[0] - 1 + dy, 1 + dx:w_p.shape[1] - 1 + dx] for dy in (-1, 0, 1) for dx in (-1, 0, 1))
        fill = (w == 0) & (n > 0)
        acc[fill] = s[fill] / n[fill][:, None]
        w[fill] = 1.0
    out = rgb.copy()
    unknown = known == 0
    out[unknown] = np.where(w[unknown][:, None] > 0, acc[unknown], rgb[unknown])
    return out


def pad_axes(arr, pad, wrap_x, wrap_y):
    widths_y = ((pad, pad), (0, 0)) + (((0, 0),) if arr.ndim == 3 else ())
    widths_x = ((0, 0), (pad, pad)) + (((0, 0),) if arr.ndim == 3 else ())
    arr = np.pad(arr, widths_y, mode="wrap" if wrap_y else "symmetric")
    return np.pad(arr, widths_x, mode="wrap" if wrap_x else "symmetric")


def upscale_classic(rgba, factor, alpha_mode, params, deblock):
    """Vergroessert RGBA um factor: Glaetten der DXT-Bloecke, Lanczos mit Rand, Nachschaerfen."""
    h, w = rgba.shape[:2]
    rgb = bleed_color(rgba) if alpha_mode != "none" else rgba[..., :3].astype(np.float32)
    wrap_x, wrap_y = is_tileable(rgba[..., :3], 1), is_tileable(rgba[..., :3], 0)
    pad = min(4, w, h)
    out = np.empty((h * factor, w * factor, 4), dtype=np.float32)
    for c in range(3):
        ch = rgb[..., c]
        if deblock > 0:
            ch = ch * (1.0 - deblock) + blur_f(ch, 0.6) * deblock
        p = pad_axes(ch, pad, wrap_x, wrap_y)
        big = resize_f(p, p.shape[1] * factor, p.shape[0] * factor)
        big = big[pad * factor:pad * factor + h * factor, pad * factor:pad * factor + w * factor]
        if params["sharpen"] > 0:
            big = big + params["sharpen"] * (big - blur_f(big, 0.5 * factor))
        out[..., c] = big
    a = rgba[..., 3].astype(np.float32)
    if alpha_mode == "none":
        out[..., 3] = 255.0
    else:
        p = pad_axes(a, pad, wrap_x, wrap_y)
        big = resize_f(p, p.shape[1] * factor, p.shape[0] * factor)
        big = big[pad * factor:pad * factor + h * factor, pad * factor:pad * factor + w * factor]
        out[..., 3] = np.where(big >= 128.0, 255.0, 0.0) if alpha_mode == "binary" else big
    return np.clip(np.rint(out), 0, 255).astype(np.uint8), (wrap_x, wrap_y)


def upscale_ai(rgba, factor, params):
    """Externes Programm (z. B. Real-ESRGAN) mit {in} {out} {scale}; Alpha klassisch."""
    import subprocess
    with tempfile.TemporaryDirectory() as tmp:
        src, dst = os.path.join(tmp, "in.png"), os.path.join(tmp, "out.png")
        Image.fromarray(rgba[..., :3], "RGB").save(src)
        cmd = params["ai_cmd"].format(**{"in": src, "out": dst, "scale": factor})
        subprocess.run(cmd, shell=True, check=True, capture_output=True)
        rgb = np.array(Image.open(dst).convert("RGB"), dtype=np.uint8)
    h, w = rgba.shape[0] * factor, rgba.shape[1] * factor
    if rgb.shape[:2] != (h, w):
        rgb = np.array(Image.fromarray(rgb, "RGB").resize((w, h), Image.LANCZOS), dtype=np.uint8)
    return rgb


AI_PAD = 8	# Rand fuer den KI-Hochskalierer (Pixel der Quelle), gegen Naehte und Randfehler


def ai_prepare(rgba, alpha_mode):
    """Eingabe fuer den KI-Hochskalierer: RGB, Farbe unter transparenten Pixeln aufgefuellt, mit Rand
    (Wiederholung an kachelbaren Kanten, sonst gespiegelt). Liefert (uint8-Bild, Rand)."""
    rgb = bleed_color(rgba) if alpha_mode != "none" else rgba[..., :3].astype(np.float32)
    wrap_x, wrap_y = is_tileable(rgba[..., :3], 1), is_tileable(rgba[..., :3], 0)
    pad = min(AI_PAD, rgba.shape[0], rgba.shape[1])
    img = np.clip(np.rint(rgb), 0, 255).astype(np.uint8)
    return pad_axes(img, pad, wrap_x, wrap_y), pad


def seamless(big, pad, width, height, wrap):
    """Kachelbare Achsen nahtlos machen: der Rand hinter der rechten (unteren) Kante zeigt dieselben Quellpixel wie
    der Anfang links (oben). Links wird zum Rand hin auf diese Fortsetzung uebergeblendet, dann passt die linke Kante
    zur rechten. Noetig, weil Diffusion an beiden Kanten verschiedene Details erfindet; GAN-Ergebnisse aendert es
    kaum. big: gepolstertes Bild (pad in Zielpixeln)."""
    out = big.astype(np.float32)
    if pad <= 0:
        return big
    ramp = (np.arange(pad, dtype=np.float32) + 0.5) / pad
    if wrap[0]:
        a = out[:, pad:2 * pad].copy()
        b = out[:, pad + width:2 * pad + width]
        out[:, pad:2 * pad] = b * (1 - ramp)[None, :, None] + a * ramp[None, :, None]
    if wrap[1]:
        a = out[pad:2 * pad].copy()
        b = out[pad + height:2 * pad + height]
        out[pad:2 * pad] = b * (1 - ramp)[:, None, None] + a * ramp[:, None, None]
    return np.clip(np.rint(out), 0, 255).astype(np.uint8)


def ai_finish(big_rgb, pad, scale, width, height, factor, wrap=(False, False)):
    """Rand abschneiden und auf die Zielgroesse bringen (4x-Modell fuer HD: Lanczos auf 2x)."""
    if any(wrap) and big_rgb.shape[0] >= (height + 2 * pad) * scale and big_rgb.shape[1] >= (width + 2 * pad) * scale:
        big_rgb = seamless(big_rgb, pad * scale, width * scale, height * scale, wrap)
    y0, x0 = pad * scale, pad * scale
    crop = big_rgb[y0:y0 + height * scale, x0:x0 + width * scale]
    if crop.shape[0] != height * factor or crop.shape[1] != width * factor:
        crop = np.array(Image.fromarray(np.ascontiguousarray(crop), "RGB").resize((width * factor, height * factor),
                                                                                    Image.LANCZOS), dtype=np.uint8)
    return crop


def coverage(alpha, threshold=128):
    return float((alpha >= threshold).mean())


ALPHA_REF = 96  # Alphatest-Schwelle der Engine (0x60, shader.cpp)


def build_mips(rgba, fourcc, alpha_mode):
    """Volle Mip-Kette bis 1x1; bei 1-Bit-Alpha mit erhaltener Abdeckung. "weich": Alpha bleibt weich (Kanten unter einem
    Texel genau), je Stufe so verschoben, dass der Anteil ueber der Alphatest-Schwelle ALPHA_REF dem der Stufe 0 entspricht."""
    levels = [rgba]
    target_cov = coverage(rgba[..., 3]) if alpha_mode == "binary" else None
    if alpha_mode == "weich":
        target_cov = float((rgba[..., 3] >= ALPHA_REF).mean())
    cur = rgba.astype(np.float32)
    for (w, h) in mip_dims(rgba.shape[1], rgba.shape[0])[1:]:
        img = np.empty((h, w, 4), dtype=np.float32)
        for c in range(4):
            img[..., c] = resize_f(cur[..., c], w, h, Image.BOX)
        cur = img
        lvl = np.clip(np.rint(img), 0, 255).astype(np.uint8)
        if alpha_mode == "binary":
            a = img[..., 3]
            # Schwelle so waehlen, dass der Anteil sichtbarer Pixel dem der Stufe 0 entspricht
            if 0.0 < target_cov < 1.0:
                t = float(np.quantile(a, 1.0 - target_cov))
                t = min(max(t, 1.0), 254.0)
            else:
                t = 128.0
            lvl[..., 3] = np.where(a >= t, 255, 0).astype(np.uint8)
        elif alpha_mode == "weich":
            lvl[..., 3] = alpha_weich(img[..., 3], target_cov)
        elif alpha_mode == "none":
            lvl[..., 3] = 255
        levels.append(lvl)
    return levels


def alpha_weich(a, target_cov):
    """Weiches Alpha (0..255, float) so verschieben, dass der Anteil >= ALPHA_REF gleich target_cov ist."""
    if 0.0 < target_cov < 1.0:
        t = float(np.quantile(a, 1.0 - target_cov))
        a = a - t + ALPHA_REF
    return np.clip(np.rint(a), 0, 255).astype(np.uint8)


def encode_level(level, fourcc):
    """Eine Stufe mit Pillow nach DXT1/DXT5 (nur die Blockdaten)."""
    h, w = level.shape[:2]
    # Pillow kodiert nur ganze 4x4-Bloecke sauber: kleine Stufen auf 4 auffuellen
    pw, ph = max(4, w), max(4, h)
    if (pw, ph) != (w, h):
        padded = np.zeros((ph, pw, 4), dtype=np.uint8)
        padded[:h, :w] = level
        padded[h:, :w] = level[-1:, :, :]
        padded[:, w:] = padded[:, w - 1:w]
        level = padded
    mode_img = Image.fromarray(level, "RGBA") if fourcc == "DXT5" or (level[..., 3] < 255).any() \
        else Image.fromarray(np.ascontiguousarray(level[..., :3]), "RGB")
    buf = io.BytesIO()
    mode_img.save(buf, format="DDS", pixel_format=fourcc)
    data = buf.getvalue()
    size = dxt_level_size(pw, ph, fourcc)
    body = data[128:128 + size]
    if len(body) != size:
        raise RuntimeError(f"DDS-Kodierung: {len(body)} statt {size} Bytes")
    return body


def encode_dds(levels, fourcc):
    h, w = levels[0].shape[:2]
    out = [dds_header(w, h, len(levels), fourcc)]
    for lvl in levels:
        out.append(encode_level(lvl, fourcc))
    return b"".join(out)


def psnr(a, b):
    diff = a.astype(np.float64) - b.astype(np.float64)
    mse = float((diff * diff).mean())
    return 99.0 if mse <= 1e-9 else 10.0 * np.log10(255.0 * 255.0 / mse)


RESTORE_MIN_COLOR_PSNR = 22.0
RESTORE_MIN_ALPHA_PSNR = 25.0


def restore_mismatch(base_rgba, zh_rgba):
    """None, wenn die Generals-Fassung (doppelte Groesse) dasselbe Motiv wie die ZH-Fassung zeigt, sonst Grund."""
    h, w = zh_rgba.shape[:2]
    b = base_rgba.astype(np.float32)
    if b.shape[0] != 2 * h or b.shape[1] != 2 * w:
        return "Groesse passt nicht"
    small = np.stack([resize_f(b[..., c], w, h, Image.BOX) for c in range(4)], axis=-1)
    color = psnr(small[..., :3], zh_rgba[..., :3])
    alpha = psnr(small[..., 3], zh_rgba[..., 3])
    if color < RESTORE_MIN_COLOR_PSNR:
        return f"Farbe {color:.1f} dB"
    if alpha < RESTORE_MIN_ALPHA_PSNR:
        return f"Alpha {alpha:.1f} dB"
    return None


def upscale_rects(rgba, big, factor, alpha_mode, params, rects):
    """Jedes Teilbild eines Oberflaechen-Atlas fuer sich vergroessern, Rand durch Wiederholung (Kanten
    nicht mit den Nachbarn im Atlas vermischen). Pixel ausserhalb aller Teilbilder bleiben wie in big."""
    h, w = rgba.shape[:2]
    rgb_all = bleed_color(rgba) if alpha_mode != "none" else rgba[..., :3].astype(np.float32)
    src = np.concatenate([rgb_all, rgba[..., 3:4].astype(np.float32)], axis=2)
    out = big.astype(np.float32)
    for left, top, right, bottom in rects:
        left, top = max(0, left), max(0, top)
        right, bottom = min(w, right), min(h, bottom)
        if right - left < 1 or bottom - top < 1:
            continue
        reg = src[top:bottom, left:right]
        pad = 4
        p = np.pad(reg, ((pad, pad), (pad, pad), (0, 0)), mode="edge")
        rh, rw = bottom - top, right - left
        up = np.empty((rh * factor, rw * factor, 4), dtype=np.float32)
        for c in range(4):
            ch = resize_f(p[..., c], p.shape[1] * factor, p.shape[0] * factor)
            ch = ch[pad * factor:pad * factor + rh * factor, pad * factor:pad * factor + rw * factor]
            if c < 3 and params.get("sharpen", 0) > 0 and min(rh, rw) >= 4:
                ch = ch + params["sharpen"] * (ch - blur_f(ch, 0.5 * factor))
            up[..., c] = ch
        if alpha_mode == "none":
            up[..., 3] = 255.0
        elif alpha_mode == "binary":
            up[..., 3] = np.where(up[..., 3] >= 128.0, 255.0, 0.0)
        out[top * factor:bottom * factor, left * factor:right * factor] = up
    return np.clip(np.rint(out), 0, 255).astype(np.uint8)


TILE_SCALE_ID = b"ZHTS"	# Kennung im TGA-Bildkennungsfeld: Kacheln sind 64*s Pixel gross (WorldHeightMap::readTiles)


def encode_tga(rgba, alpha_mode, tile_scale=0):
    """Unkomprimiertes TGA wie die EA-Originale (Typ 2, Ursprung unten links), 32 Bit mit Alpha, sonst 24.
    tile_scale > 0: Gelaende-/Baumkacheln, Kennung "ZHTS<s>" im Bildkennungsfeld, damit die Engine eine HD-Datei
    (Kacheln 64*s) von einer SD-Datei mit mehr Kacheln unterscheidet."""
    buf = io.BytesIO()
    kw = {"id_section": TILE_SCALE_ID + str(tile_scale).encode("ascii")} if tile_scale else {}
    if alpha_mode == "none":
        Image.fromarray(np.ascontiguousarray(rgba[..., :3]), "RGB").save(buf, format="TGA", **kw)
    else:
        Image.fromarray(rgba, "RGBA").save(buf, format="TGA", **kw)
    return buf.getvalue()


class Bc7Pending(Exception):
    """Durchgang 1 des BC7-Stapels: die Eingabe liegt bereit, texconv laeuft danach fuer alle."""


def bc7_key(raw):
    return hashlib.sha1(raw).hexdigest()[:24]


def texconv_cmd(params, out_dir, inputs=None, flist=None):
    cmd = [params["texconv"], "-f", "BC7_UNORM", "-y", "-nologo", "-o", out_dir]
    if params.get("bc7_cpu"):
        cmd.append("-nogpu")
    if flist:
        cmd += ["-flist", flist]
    return cmd + list(inputs or [])


def encode_bc7(levels, params):
    """BC7-DDS (DX10-Kopf) aus der fertigen Mip-Kette: aus dem Stapel (bc7_dir\\aus), sonst einzeln mit texconv.
    Im Vorbereitungsdurchgang wird nur die Eingabe abgelegt (Bc7Pending)."""
    import subprocess
    raw = dds_rgba(levels)
    key = bc7_key(raw)
    out = os.path.join(params["bc7_dir"], "aus", key + ".dds")
    if os.path.exists(out):
        with open(out, "rb") as f:
            data = f.read()
        try:
            if parse_dds(data)[0] == "BC7" and parse_dds(data)[3] == len(levels):
                return data
        except ValueError:
            pass
        os.remove(out)	# halb geschrieben oder falsch: neu kodieren
    if params.get("bc7_prepare"):
        ein = os.path.join(params["bc7_dir"], "ein")
        os.makedirs(ein, exist_ok=True)
        with open(os.path.join(ein, key + ".dds"), "wb") as f:
            f.write(raw)
        raise Bc7Pending(key)
    # Einzeln (Nachzuegler, etwa ein KI-Ergebnis, das im Rueckvergleich erst mit BC7 durchfaellt)
    with tempfile.TemporaryDirectory() as tmp:
        src = os.path.join(tmp, key + ".dds")
        with open(src, "wb") as f:
            f.write(raw)
        os.makedirs(os.path.join(tmp, "o"))		# texconv legt den Zielordner nicht an
        subprocess.run(texconv_cmd(params, os.path.join(tmp, "o"), [src]), check=True, capture_output=True)
        with open(os.path.join(tmp, "o", key + ".dds"), "rb") as f:
            data = f.read()
    os.makedirs(os.path.dirname(out), exist_ok=True)
    with open(out + ".tmp", "wb") as f:
        f.write(data)
    os.replace(out + ".tmp", out)
    return data


def encode_output(job, big, fourcc, alpha_mode, params=None):
    """(Daten, Format) je nach Art: Oberflaeche und Kacheln als TGA, sonst DDS mit Mips (DXT oder BC7)."""
    if job.get("tiles"):
        return encode_tga(big, alpha_mode, job["factor"] if not job.get("restore") else job["tier_factor"]), \
            ("TGA32" if alpha_mode != "none" else "TGA24")
    if is_ui_tga(job):
        return encode_tga(big, alpha_mode), ("TGA32" if alpha_mode != "none" else "TGA24")
    if fourcc == "BC7":
        return encode_bc7(build_mips(big, fourcc, alpha_mode), params), fourcc
    return encode_dds(build_mips(big, fourcc, alpha_mode), fourcc), fourcc


def psnr_to_template(data, big):
    """PSNR (Farbe, sichtbare Pixel) der kodierten Stufe 0 gegen die unkomprimierte Vorlage."""
    dec = decode(data, "x.dds")
    mask = big[..., 3] >= 8
    if dec.shape != big.shape or not mask.any():
        return None
    return round(psnr(dec[..., :3][mask], big[..., :3][mask]), 2)


MATCH_MAX_GAIN = 2.0	# bounds the contrast change, so a nearly flat result cannot be blown up into noise


def match_colors(big, sd_rgba, max_gain=MATCH_MAX_GAIN):
    """Per colour channel a*x+b, so that the result averaged down to SD size has the mean and spread of the SD
    texture (visible pixels only). Detail above the SD resolution stays; alpha is not touched.
    Returns the new RGBA image and [(gain, offset)] per channel, or None if nothing is visible."""
    h, w = sd_rgba.shape[:2]
    mask = sd_rgba[..., 3] >= 8
    out = big.copy()
    if not mask.any():
        return out, None
    rgb = big[..., :3].astype(np.float32)
    coeffs = []
    for c in range(3):
        small = resize_f(rgb[..., c], w, h, Image.BOX)[mask]
        ref = sd_rgba[..., c].astype(np.float32)[mask]
        m, sdev = float(small.mean()), float(small.std())
        mo, so = float(ref.mean()), float(ref.std())
        gain = so / sdev if sdev > 0.5 else 1.0
        gain = min(max(gain, 1.0 / max_gain), max_gain)
        offset = mo - m * gain
        out[..., c] = np.clip(np.rint(rgb[..., c] * gain + offset), 0, 255).astype(np.uint8)
        coeffs.append((round(gain, 4), round(offset, 2)))
    return out, coeffs


def write_json_private(path, obj):
    """Writes through a new file: a hard-linked file shared with another work folder stays as it was."""
    os.makedirs(os.path.dirname(path) or ".", exist_ok=True)
    tmp = path + ".tmp"
    with open(tmp, "w", encoding="ascii") as f:
        json.dump(obj, f)
    os.replace(tmp, path)


def compare_to_sd(result_dds, sd_rgba):
    """Ergebnis dekodieren, auf SD-Groesse mitteln, Farbe gegen das Original (nur sichtbare Pixel).
    result_dds darf auch schon ein dekodiertes Bild (numpy) sein."""
    hd = (result_dds if isinstance(result_dds, np.ndarray) else decode(result_dds, "x.dds")).astype(np.float32)
    h, w = sd_rgba.shape[:2]
    small = np.stack([resize_f(hd[..., c], w, h, Image.BOX) for c in range(3)], axis=-1)
    mask = sd_rgba[..., 3] >= 8
    if not mask.any():
        return 99.0
    return psnr(small[mask], sd_rgba[..., :3][mask])


# =================================================================================================
# Ein Auftrag (laeuft in einem Arbeitsprozess)
# =================================================================================================
def params_hash(params):
    keys = ("sharpen", "deblock", "min_psnr", "ai_cmd", "ai_kinds", "ai_model", "ai_scale", "ai_min_psnr",
            "ai_refine", "terrain")
    extra = {"bc7": params["bc7"]} if params.get("bc7") else {}	# TODO 58: nur mit BC7, alte Staende bleiben gueltig
    if params.get("ai_match_color"):
        extra["ai_match_color"] = True	# only when set: work folders of earlier runs stay valid
    blob = json.dumps({k: params.get(k) for k in keys} | {"tool": TOOL_VERSION} | extra, sort_keys=True)
    return hashlib.sha1(blob.encode("ascii")).hexdigest()[:16]


def process_job(job, root, work, params):
    """Liefert das Manifest-Stueck; das Ergebnis liegt in work als .dds (oder fehlt bei Rueckfall)."""
    out_path = os.path.join(work, job_output_name(job))
    meta_path = out_path + ".json"
    src_big = inv.BigArchive(os.path.join(root, job["source_archive"]))
    src = src_big.read(job["name"])
    src_sha = hashlib.sha1(src).hexdigest()
    ph = params_hash(params)
    if os.path.exists(meta_path):
        with open(meta_path, "r", encoding="ascii") as f:
            meta = json.load(f)
        if meta.get("source_sha1") == src_sha and meta.get("params") == ph and meta["status"] != "fehler" and \
                (meta["status"] != "ok" or os.path.exists(out_path)) and \
                not (job.get("no_ai") and meta.get("method") == "ki"):
            return meta
    meta = {"name": job["name"], "kind": job["kind"], "source_archive": job["source_archive"],
            "source_sha1": src_sha, "params": ph, "restore": job["restore"]}
    try:
        if job["restore"]:
            # Nur dasselbe Motiv zurueckholen: Generals-Fassung auf ZH-Groesse gemittelt gegen ZH, Farbe und
            # Alpha. 17 der 236 DXT5-Paare haben einen ganz anderen Alphakanal (Phase 1) -- dort gilt ZH.
            zh_rgba = decode(inv.BigArchive(os.path.join(root, job["archive"])).read(job["name"]), job["name"])
            reject = restore_mismatch(decode(src, job["name"]), zh_rgba)
            if reject:
                meta.update(restore=False, restore_rejected=reject, source_archive=job["archive"])
                job = dict(job, restore=False, source_archive=job["archive"], factor=job["tier_factor"],
                           format=inv.texture_header(inv.BigArchive(os.path.join(root, job["archive"])).read_head(job["name"], 128), job["name"])[0])
                src = inv.BigArchive(os.path.join(root, job["archive"])).read(job["name"])
        if job["restore"] and job["factor"] == 1 and job["name"].endswith(".dds"):
            data = src	# Generals-Fassung unveraendert (HD-9)
            meta["method"] = "Generals-Fassung"
            meta["psnr"] = None
        else:
            rgba = decode(src, job["name"])
            alpha = rgba[..., 3]
            if job["format"] == "DXT1":
                alpha_mode = "binary" if (alpha < 255).any() else "none"
            else:
                alpha_mode = "none" if (alpha == 255).all() else "smooth"
            fourcc = target_fourcc(job["format"], alpha_mode != "none", params.get("bc7"))
            use_ai = (params.get("ai_cmd") or params.get("ai_exe")) and job["kind"] in params["ai_kinds"] \
                and not job.get("no_ai")
            deblock = params["deblock"] if job["format"].startswith("DXT") else 0.0
            big, wrap = upscale_classic(rgba, job["factor"], alpha_mode, params, deblock)
            if job.get("rects"):
                big = upscale_rects(rgba, big, job["factor"], alpha_mode, params, job["rects"])
            classic_rgb = big[..., :3].copy()
            if use_ai and params.get("ai_exe"):
                # Stapelbetrieb: Ergebnis des gemeinsamen Programmlaufs, Schluessel = Quell-SHA1
                key = hashlib.sha1(src).hexdigest()[:24]
                ai_png = os.path.join(params["ai_dir"], "aus", key + ".png")
                ai_img = None
                if os.path.exists(ai_png):
                    try:	# halb geschriebene Datei (Abbruch) gilt als fehlend und wird neu gerechnet
                        ai_img = Image.open(ai_png)
                        ai_img.load()
                    except Exception:
                        ai_img = None
                        os.remove(ai_png)
                if ai_img is None:
                    if params.get("ai_prepare"):
                        inp, _pad = ai_prepare(rgba, alpha_mode)
                        os.makedirs(os.path.join(params["ai_dir"], "ein"), exist_ok=True)
                        Image.fromarray(inp, "RGB").save(os.path.join(params["ai_dir"], "ein", key + ".png"))
                        return {"name": job["name"], "status": "ki-vorbereitet", "ai_key": key, "kind": job["kind"]}
                    use_ai = False
                    meta["ai_missing"] = True
                else:
                    ai_rgb = np.array(ai_img.convert("RGB"), dtype=np.uint8)
                    pad = min(AI_PAD, rgba.shape[0], rgba.shape[1])
                    big[..., :3] = ai_finish(ai_rgb, pad, params["ai_scale"], rgba.shape[1], rgba.shape[0], job["factor"],
                                             wrap)
            elif use_ai:
                big[..., :3] = upscale_ai(rgba, job["factor"], params)
            sd = None
            if use_ai and params.get("ai_match_color"):
                # The refinement shifts colours towards grey; match them to the SD texture before the comparison
                sd = decode(inv.BigArchive(os.path.join(root, job["archive"])).read(job["name"]), job["name"])
                meta["ai_psnr_raw"] = round(compare_to_sd(big, sd), 2)
                big, meta["ai_color"] = match_colors(big, sd)
            try:
                data, fourcc = encode_output(job, big, fourcc, alpha_mode, params)	# TGA: Mips erzeugt die Engine beim Laden
            except Bc7Pending:
                if not use_ai:
                    return {"name": job["name"], "status": "bc7-vorbereitet"}
                data = None	# KI-Ergebnis: Rueckvergleich unkomprimiert, die klassische Fassung ggf. auch vorbereiten
            # Rueckvergleich gegen die SD-Datei des Spiels (bei der Rueckholung also gegen ZH)
            if sd is None:
                sd = decode(inv.BigArchive(os.path.join(root, job["archive"])).read(job["name"]), job["name"])
            q = compare_to_sd(data if data is not None and data[:4] == b"DDS " else big, sd)
            if use_ai and q < params.get("ai_min_psnr", params["min_psnr"]):
                # KI-Ergebnis faellt im Rueckvergleich durch: das klassische gilt
                meta["ai_psnr"] = round(q, 2)
                big[..., :3] = classic_rgb
                try:
                    data, fourcc = encode_output(job, big, target_fourcc(job["format"], alpha_mode != "none",
                                                                        params.get("bc7")), alpha_mode, params)
                except Bc7Pending:
                    return {"name": job["name"], "status": "bc7-vorbereitet"}
                q = compare_to_sd(data if data[:4] == b"DDS " else big, sd)
                use_ai = False
                meta["ai_rejected"] = True
            elif data is None:
                return {"name": job["name"], "status": "bc7-vorbereitet"}
            if data[:4] == b"DDS ":
                # TODO 58: Stufe 0 gegen die unkomprimierte Vorlage; bei BC7 auch die DXT-Fassung zum Vergleich
                meta["psnr_vorlage"] = psnr_to_template(data, big)
                if fourcc == "BC7" and params.get("bc7_compare", True):
                    dxt = target_fourcc(job["format"], alpha_mode != "none")
                    meta["psnr_vorlage_dxt"] = psnr_to_template(encode_dds([big], dxt), big)
                    meta["dxt_format"] = dxt
            meta.update(method="ki" if use_ai else "klassisch", format=fourcc, alpha=alpha_mode,
                        wrap=list(wrap), psnr=round(q, 2))
            limit = params.get("ai_min_psnr", params["min_psnr"]) if use_ai else params["min_psnr"]
            if q < limit:
                meta["status"] = "rueckfall"
                meta["reason"] = f"PSNR {q:.1f} dB unter {limit}"
                _write_meta(meta_path, meta)
                if os.path.exists(out_path):
                    os.remove(out_path)
                return meta
        os.makedirs(os.path.dirname(out_path), exist_ok=True)
        with open(out_path + ".tmp", "wb") as f:
            f.write(data)
        os.replace(out_path + ".tmp", out_path)
        if data[:4] == b"DDS ":
            fourcc, w, h, mips, _ = parse_dds(data)
        else:
            w, h, mips = struct.unpack("<HH", data[12:16]) + (1,)
        meta.update(status="ok", output=job_output_name(job), width=w, height=h, mips=mips,
                    output_sha1=hashlib.sha1(data).hexdigest(), bytes=len(data))
    except Exception as e:  # eine kaputte Quelle darf den Lauf nicht abbrechen
        meta.update(status="fehler", reason=f"{type(e).__name__}: {e}")
    _write_meta(meta_path, meta)
    return meta


def _write_meta(path, meta):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="ascii") as f:
        json.dump(meta, f, sort_keys=True)


def _worker(args):
    return process_job(*args)


# =================================================================================================
# Mauszeiger (.ani, Phase 4)
# =================================================================================================
# Windows laedt Zeiger nur aus Dateien; die Stufe legt sie deshalb lose unter Texturstufen\<Stufe>\Cursors
# ab (Win32Mouse.cpp, loadTierCursor). Jedes Bild eines .ani ist eine .cur-Datei (ICONDIR + DIB mit
# Farb- und UND-Maske); vergroessert wird wie bei den Texturen, geschrieben als 32-Bit-DIB mit Alpha.
def riff_chunks(data, pos, end):
    while pos + 8 <= end:
        cid = data[pos:pos + 4]
        size = struct.unpack("<I", data[pos + 4:pos + 8])[0]
        yield cid, data[pos + 8:pos + 8 + size]
        pos += 8 + size + (size & 1)


def riff_chunk(cid, body):
    return cid + struct.pack("<I", len(body)) + body + (b"\0" if len(body) & 1 else b"")


def decode_cur(cur):
    """Erstes Bild einer .cur/.ico: (RGBA, Hotspot x, y)."""
    _res, _typ, count = struct.unpack("<HHH", cur[:6])
    w, h, _cc, _r, hx, hy, size, offset = struct.unpack("<BBBBHHII", cur[6:22])
    img = cur[offset:offset + size]
    if img[:8] == b"\x89PNG\r\n\x1a\n":
        rgba = np.array(Image.open(io.BytesIO(img)).convert("RGBA"), dtype=np.uint8)
        return rgba, hx, hy
    hs, width, height2, _planes, bpp, _comp = struct.unpack("<IiiHHI", img[:20])
    height = height2 // 2
    colors = struct.unpack("<I", img[32:36])[0] or ((1 << bpp) if bpp <= 8 else 0)
    pal_off = hs
    palette = [tuple(img[pal_off + 4 * i:pal_off + 4 * i + 4]) for i in range(colors)]
    xor_off = pal_off + 4 * colors
    stride = ((width * bpp + 31) // 32) * 4
    and_off = xor_off + stride * height
    and_stride = ((width + 31) // 32) * 4
    rgba = np.zeros((height, width, 4), dtype=np.uint8)
    has_alpha = False
    for y in range(height):
        row = img[xor_off + (height - 1 - y) * stride:xor_off + (height - y) * stride]
        mrow = img[and_off + (height - 1 - y) * and_stride:and_off + (height - y) * and_stride]
        for x in range(width):
            if bpp == 32:
                b, g, r, a = row[4 * x:4 * x + 4]
                has_alpha |= a != 0
            elif bpp == 24:
                b, g, r = row[3 * x:3 * x + 3]
                a = 255
            else:
                bit = x * bpp
                idx = (row[bit // 8] >> (8 - bpp - bit % 8)) & ((1 << bpp) - 1)
                b, g, r, _ = palette[idx]
                a = 255
            masked = (mrow[x // 8] >> (7 - x % 8)) & 1 if mrow else 0
            rgba[y, x] = (r, g, b, a if bpp == 32 else (0 if masked else 255))
    if bpp == 32 and not has_alpha:	# 32 Bit ohne Alpha: UND-Maske gilt
        for y in range(height):
            mrow = img[and_off + (height - 1 - y) * and_stride:and_off + (height - y) * and_stride]
            for x in range(width):
                rgba[y, x, 3] = 0 if (mrow[x // 8] >> (7 - x % 8)) & 1 else 255
    return rgba, hx, hy


def encode_cur(rgba, hx, hy):
    """.cur mit einem 32-Bit-Bild (BGRA, Alpha) und passender UND-Maske."""
    h, w = rgba.shape[:2]
    xor = rgba[::-1, :, [2, 1, 0, 3]].tobytes()
    and_stride = ((w + 31) // 32) * 4
    mask = bytearray(and_stride * h)
    for y in range(h):
        src = rgba[h - 1 - y, :, 3]
        for x in range(w):
            if src[x] < 128:
                mask[y * and_stride + x // 8] |= 0x80 >> (x % 8)
    dib = struct.pack("<IiiHHIIiiII", 40, w, 2 * h, 1, 32, 0, len(xor) + len(mask), 0, 0, 0, 0) + xor + bytes(mask)
    head = struct.pack("<HHH", 0, 2, 1) + struct.pack("<BBBBHHII", w & 0xFF, h & 0xFF, 0, 0, hx, hy, len(dib), 22)
    return head + dib


def upscale_ani(data, factor, params):
    """Alle Bilder eines .ani vergroessern; alles andere (anih, rate, seq, INFO) bleibt."""
    if data[:4] != b"RIFF" or data[8:12] != b"ACON":
        raise ValueError("kein .ani")
    out = []
    for cid, body in riff_chunks(data, 12, len(data)):
        if cid == b"LIST" and body[:4] == b"fram":
            frames = []
            for fid, fbody in riff_chunks(body, 4, len(body)):
                if fid == b"icon":
                    rgba, hx, hy = decode_cur(fbody)
                    alpha_mode = "binary" if set(np.unique(rgba[..., 3])) <= {0, 255} else "smooth"
                    big, _ = upscale_classic(rgba, factor, alpha_mode, params, 0.0)
                    fbody = encode_cur(big, hx * factor, hy * factor)
                frames.append(riff_chunk(fid, fbody))
            body = b"fram" + b"".join(frames)
        out.append(riff_chunk(cid, body))
    payload = b"ACON" + b"".join(out)
    return b"RIFF" + struct.pack("<I", len(payload)) + payload


def build_cursors(root, tier_dir, factor, params):
    src_dir = os.path.join(root, "Data", "Cursors")
    if not os.path.isdir(src_dir):
        return 0, []
    dst_dir = os.path.join(tier_dir, "Cursors")
    os.makedirs(dst_dir, exist_ok=True)
    done, failed = 0, []
    for fn in sorted(os.listdir(src_dir)):
        if not fn.lower().endswith(".ani"):
            continue
        try:
            with open(os.path.join(src_dir, fn), "rb") as f:
                data = upscale_ani(f.read(), factor, params)
            with open(os.path.join(dst_dir, fn), "wb") as f:
                f.write(data)
            done += 1
        except Exception as e:  # ein unlesbarer Zeiger bleibt SD
            failed.append(f"{fn}: {type(e).__name__}: {e}")
    return done, failed


# =================================================================================================
# Ablauf
# =================================================================================================
def sample_per_kind(plan, count):
    """count Texturen je Art, gleichmaessig ueber die sortierte Liste verteilt (Prototyp, Phase 1)."""
    from collections import defaultdict
    by_kind = defaultdict(list)
    for j in plan:
        by_kind[j["kind"]].append(j)
    out = []
    for kind in sorted(by_kind):
        items = by_kind[kind]
        step = max(1, len(items) // count)
        out.extend(items[::step][:count])
    return out


def run_ai_batch(params, jobs_n, kinds=None):
    """Einmal realesrgan-ncnn-vulkan ueber alle vorbereiteten Eingaben; Ergebnisse nach ai_dir\\aus. Mit
    --ai-refine-python danach die Verfeinerung per Diffusion (tools/assets/ki_verfeinern.py), wiederaufnehmbar:
    GAN-Ergebnisse liegen in ai_dir\\gan, fertige in ai_dir\\aus."""
    import shutil, subprocess, time
    ein, aus = os.path.join(params["ai_dir"], "ein"), os.path.join(params["ai_dir"], "aus")
    refine = params.get("ai_refine_python")
    if refine:
        final = aus
        aus = os.path.join(params["ai_dir"], "gan")
    os.makedirs(aus, exist_ok=True)
    todo = [f for f in os.listdir(ein)] if os.path.isdir(ein) else []
    # Fortsetzen: Eingaben, deren GAN-Ergebnis schon vorliegt (abgebrochener Lauf mit Verfeinerung), nicht
    # noch einmal durch ncnn schicken -- die Verfeinerung setzt dann je Textur fort.
    have = [f for f in todo if os.path.exists(os.path.join(aus, f))]
    for f in have:
        os.remove(os.path.join(ein, f))
    if have:
        print(f"KI: {len(have)} GAN-Ergebnisse schon vorhanden, uebersprungen", flush=True)
    todo = [f for f in todo if f not in set(have)]
    if not todo and not refine:
        return 0, 0.0
    if not todo:
        shutil.rmtree(ein, ignore_errors=True)	# only inputs whose GAN result was already there
        return _refine(params, aus, final, kinds, 0, 0.0)
    exe = params["ai_exe"]
    cmd = [exe, "-i", ein, "-o", aus, "-n", params["ai_model"], "-s", str(params["ai_scale"]), "-f", "png",
           "-m", os.path.join(os.path.dirname(exe), "models"), "-j", "4:2:4"]
    print(f"KI: {len(todo)} Texturen mit {params['ai_model']} ({params['ai_scale']}x)", flush=True)
    t = time.time()
    r = subprocess.run(cmd, cwd=os.path.dirname(exe), capture_output=True, text=True, errors="replace")
    secs = time.time() - t
    done = sum(1 for f in todo if os.path.exists(os.path.join(aus, f)))
    print(f"KI: {done} von {len(todo)} fertig in {secs:.0f} s (Exit {r.returncode})", flush=True)
    if done < len(todo):
        print(r.stderr[-2000:], flush=True)
    shutil.rmtree(ein, ignore_errors=True)	# Eingaben sind aus den Quellen jederzeit wieder erzeugbar
    if refine:
        return _refine(params, aus, final, kinds, done, secs)
    return done, secs


def _refine(params, aus, final, kinds, done, secs):
    """Verfeinerung per Diffusion ueber ai_dir\\gan nach ai_dir\\aus (ki_verfeinern.py ueberspringt Fertiges)."""
    import subprocess, time
    refine = params["ai_refine_python"]
    if True:
        strengths = {k + ".png": params["ai_refine"].get(kind, 0.0) for k, kind in (kinds or {}).items()}
        sp = os.path.join(params["ai_dir"], "staerken.json")
        write_json_private(sp, strengths)
        cmd = [refine, os.path.join(os.path.dirname(os.path.abspath(__file__)), "ki_verfeinern.py"),
               "--ein", aus, "--aus", final, "--staerken", sp, "--staerke", "0"]
        print("KI-Verfeinerung:", " ".join(cmd), flush=True)
        t = time.time()
        r = subprocess.run(cmd)
        secs += time.time() - t
        print(f"KI-Verfeinerung: Exit {r.returncode}, {time.time() - t:.0f} s", flush=True)
    return done, secs


def run_bc7_batch(params):
    """Ein texconv-Lauf ueber alle vorbereiteten Eingaben (bc7_dir\\ein -> bc7_dir\\aus); Eingaben danach loeschen."""
    import shutil, subprocess, time
    ein, aus = os.path.join(params["bc7_dir"], "ein"), os.path.join(params["bc7_dir"], "aus")
    todo = sorted(f for f in os.listdir(ein)) if os.path.isdir(ein) else []
    if not todo:
        return {"inputs": 0, "seconds": 0.0, "megapixel": 0.0}
    os.makedirs(aus, exist_ok=True)
    flist = os.path.join(params["bc7_dir"], "liste.txt")
    with open(flist, "w", encoding="utf-8") as f:
        f.write("\n".join(os.path.join(ein, n) for n in todo) + "\n")
    mpx = 0.0
    for n in todo:
        with open(os.path.join(ein, n), "rb") as f:
            head = f.read(20)
        h, w = struct.unpack("<II", head[12:20])
        mpx += w * h * 4.0 / 3.0 / 1e6
    print(f"BC7: {len(todo)} Texturen, {mpx:.0f} Megapixel samt Mips, texconv {'CPU' if params.get('bc7_cpu') else 'GPU'}",
          flush=True)
    t = time.time()
    r = subprocess.run(texconv_cmd(params, aus, flist=flist), capture_output=True, text=True, errors="replace")
    secs = time.time() - t
    done = sum(1 for n in todo if os.path.exists(os.path.join(aus, n)))
    print(f"BC7: {done} von {len(todo)} fertig in {secs:.0f} s (Exit {r.returncode}, {mpx / max(secs, 0.001):.1f} MP/s)",
          flush=True)
    if done < len(todo):
        print(r.stdout[-2000:], r.stderr[-2000:], flush=True)
    shutil.rmtree(ein, ignore_errors=True)	# Eingaben entstehen jederzeit neu
    return {"inputs": len(todo), "done": done, "seconds": round(secs, 1), "megapixel": round(mpx, 1),
            "device": "CPU" if params.get("bc7_cpu") else "GPU"}


def build(root, game, tier, params, jobs_n, limit=None, only_restore=False, names=None, out=None, per_kind=None,
          stage=None, terrain=False):
    if not HAVE_IMAGING:
        raise SystemExit("numpy und Pillow fehlen (pip install numpy pillow)")
    import time
    t_start = time.time()
    out = out or root
    stage = stage or FOLDERS[tier]
    tier_dir = os.path.join(out, "Texturstufen", stage)
    work = os.path.join(out, "Texturstufen", "Arbeit", stage)
    if params.get("ai_reuse"):
        params = dict(params, ai_dir=params["ai_reuse"])	# fremde KI-Ausgaben, nur lesen (kein KI-Lauf)
    elif params.get("ai_exe"):
        params = dict(params, ai_dir=os.path.join(work, "ki"))
    if params.get("bc7"):
        params = dict(params, bc7_dir=os.path.join(work, "bc7"))
    os.makedirs(tier_dir, exist_ok=True)
    os.makedirs(work, exist_ok=True)
    plan, skipped, all_names = make_plan(root, game, tier, only_restore, terrain)
    if names:
        plan = [j for j in plan if any(n in j["name"] for n in names)]
    if per_kind:
        plan = sample_per_kind([j for j in plan if not j["restore"]], per_kind)
    if limit:
        plan = plan[:limit]
    print(f"{game} {tier}: {len(plan)} Texturen, {len(skipped)} ausgeschlossen, {jobs_n} Prozesse", flush=True)
    ai_stats = None
    if params.get("ai_exe") and not params.get("ai_reuse"):
        # Durchgang 1: alles ohne KI fertig rechnen, KI-Eingaben ablegen; dann ein Programmlauf
        import time
        t = time.time()
        prep = dict(params, ai_prepare=True)
        kinds = {}
        with ProcessPoolExecutor(max_workers=jobs_n) as pool:
            for m in pool.map(_worker, [(j, root, work, prep) for j in plan], chunksize=4):
                if m.get("status") == "ki-vorbereitet":
                    kinds[m["ai_key"]] = m["kind"]
        n_prep = len(kinds)
        # Verfeinerung braucht die Arten auch fuer Bilder, deren GAN-Ergebnis schon vorliegt (Wiederaufnahme)
        kp = os.path.join(work, "ki", "arten.json")
        if os.path.exists(kp):
            with open(kp, "r", encoding="ascii") as f:
                old = json.load(f)
            old.update(kinds)
            kinds = old
        write_json_private(kp, kinds)
        print(f"Durchgang 1: {time.time() - t:.0f} s, {n_prep} KI-Eingaben", flush=True)
        done, secs = run_ai_batch(params, jobs_n, kinds)
        ai_stats = {"inputs": n_prep, "done": done, "seconds": round(secs, 1)}
    bc7_stats = None
    if params.get("bc7"):
        # BC7 Durchgang 1: unkomprimierte Mip-Ketten ablegen, dann ein texconv-Lauf fuer alle
        t = time.time()
        prep = dict(params, bc7_prepare=True)
        with ProcessPoolExecutor(max_workers=jobs_n) as pool:
            n_prep = sum(1 for m in pool.map(_worker, [(j, root, work, prep) for j in plan], chunksize=4)
                         if m.get("status") == "bc7-vorbereitet")
        print(f"BC7 Durchgang 1: {time.time() - t:.0f} s, {n_prep} Eingaben", flush=True)
        bc7_stats = run_bc7_batch(params)
        bc7_stats["prepare_seconds"] = round(time.time() - t - bc7_stats["seconds"], 1)
    results = []
    args = [(j, root, work, params) for j in plan]
    with ProcessPoolExecutor(max_workers=jobs_n) as pool:
        for i, meta in enumerate(pool.map(_worker, args, chunksize=4)):
            results.append(meta)
            if (i + 1) % 250 == 0 or i + 1 == len(args):
                print(f"  {i + 1}/{len(args)}", flush=True)

    if params.get("ai_exe") or params.get("ai_cmd"):
        # Varianten einer Grundtextur (_d, _s, _n ...) alle mit demselben Verfahren, sonst springt das Aussehen
        # beim Zustandswechsel: faellt eine Variante von der KI zurueck, rechnet die ganze Gruppe klassisch.
        mixed = {variant_group(m["name"]) for m in results if m.get("ai_rejected") or m.get("ai_missing")}
        mixed &= {variant_group(m["name"]) for m in results if m.get("method") == "ki"}
        if mixed:
            redo = [dict(j, no_ai=True) for j in plan if variant_group(j["name"]) in mixed]
            print(f"  {len(redo)} Varianten klassisch nachgerechnet ({len(mixed)} Gruppen)", flush=True)
            if params.get("bc7"):
                # BC7: auch die klassischen Fassungen erst gesammelt kodieren (sonst einzeln je Textur)
                prep = dict(params, bc7_prepare=True)
                with ProcessPoolExecutor(max_workers=jobs_n) as pool:
                    list(pool.map(_worker, [(j, root, work, prep) for j in redo], chunksize=4))
                more = run_bc7_batch(params)
                for k in ("inputs", "seconds", "megapixel"):
                    bc7_stats[k] = round(bc7_stats.get(k, 0) + more.get(k, 0), 1)
            with ProcessPoolExecutor(max_workers=jobs_n) as pool:
                again = {m["name"]: m for m in pool.map(_worker, [(j, root, work, params) for j in redo], chunksize=4)}
            results = [dict(again[m["name"]], ai_group_classic=True) if m["name"] in again else m for m in results]
    apply_variant_fallback(results)
    # alle Texturnamen des Bestands, nicht nur die Auswahl (--per-kind/--names/--limit): sonst fehlte
    # die lokalisierte Fassung in der Pruefung (so im Prototyp aufgefallen, Phase 1)
    apply_localized_fallback(results, all_names)

    # Packen
    entries = []
    for meta in results:
        if meta.get("status") == "ok":
            with open(os.path.join(work, meta["output"]), "rb") as f:
                entries.append((archive_path(meta), f.read()))
    groups = split_archives(entries)
    prefix = "Textures" + stage.replace("-", "")
    if params.get("bc7") and "BC7" not in prefix.upper():
        prefix += "BC7"	# TextureTiers.cpp erkennt BC7-Saetze am Namen (unter Direct3D 9 nicht laden)
    for old in os.listdir(tier_dir):
        if old.lower().startswith(prefix.lower()) and old.lower().endswith(".big"):
            os.remove(os.path.join(tier_dir, old))
    written = []
    for i, group in enumerate(groups):
        name = f"{prefix}.big" if len(groups) == 1 else f"{prefix}{i + 1}.big"
        write_big(os.path.join(tier_dir, name), group)
        written.append({"archive": name, "files": len(group), "bytes": sum(len(d) for _, d in group)})

    cursors, cursor_failed = (0, []) if (only_restore or per_kind or limit or names) else \
        build_cursors(root, tier_dir, FACTORS[tier], params)

    manifest = {
        "tool": "tools/assets/build_texture_tiers.py", "version": TOOL_VERSION, "game": game, "tier": FOLDERS[tier],
        "stage": stage, "factor": FACTORS[tier],
        "params": {k: params[k] for k in sorted(params) if k not in ("ai_cmd", "ai_exe", "ai_dir", "bc7_dir")},
        "ai_cmd": params.get("ai_cmd") or None, "ai_exe": params.get("ai_exe") or None,
        "ai_license": params.get("ai_license") or None, "ai_batch": ai_stats,
        "texture_format": ({"alle": "BC7 (auch statt DXT1)", "alpha": "BC7 statt DXT5, DXT1 bleibt"}[params["bc7"]]
                           if params.get("bc7") else "DXT1/DXT5"),
        "bc7_batch": bc7_stats, "texconv": params.get("texconv"), "seconds": round(time.time() - t_start, 1),
        "archives": written,
        "cursors": {"files": cursors, "failed": cursor_failed},
        "textures": sorted(results, key=lambda m: m["name"]),
        "skipped": sorted(skipped, key=lambda s: s["name"]),
    }
    with open(os.path.join(tier_dir, "manifest.json"), "w", encoding="ascii") as f:
        json.dump(manifest, f, indent=1, sort_keys=True)
    summarize(manifest)
    return manifest


VARIANT_SUFFIX = re.compile(r"_[dsne]{1,3}$")


def variant_group(name):
    """Grundname ohne Zustands-Endung (_d beschaedigt, _s Schnee, _n Nacht, Kombinationen)."""
    stem = name.rsplit(".", 1)[0]
    return VARIANT_SUFFIX.sub("", stem)


def apply_variant_fallback(results):
    """Faellt eine Variante auf SD zurueck, bleiben alle Varianten derselben Grundtextur SD -- sonst
    springt das Aussehen beim Zustandswechsel (Abschnitt 5.2, Varianten)."""
    failed = {variant_group(m["name"]) for m in results if m.get("status") in ("rueckfall", "fehler")}
    for m in results:
        if m.get("status") == "ok" and variant_group(m["name"]) in failed:
            m["status"] = "rueckfall"
            m["reason"] = "Variante derselben Grundtextur faellt zurueck"


LOCALIZED = re.compile(r"^data\\[^\\]+\\art\\textures\\")


def apply_localized_fallback(results, all_names):
    """GameFileClass::Set_Name sucht .dds vor .tga, und je Endung erst Data\\<Sprache>\\Art\\Textures, dann
    Art\\Textures. Eine HD-Fassung Art\\Textures\\X.dds wuerde deshalb eine lokalisierte SD-Datei
    Data\\<Sprache>\\Art\\Textures\\X.tga verdecken (Text im Bild in der falschen Sprache). Bleibt eine
    lokalisierte Fassung SD, bleibt auch die allgemeine SD."""
    ok = {m["name"] for m in results if m.get("status") == "ok"}
    blocked = {stem_of(n) for n in all_names if LOCALIZED.match(n) and n not in ok}
    for m in results:
        if m.get("status") == "ok" and not LOCALIZED.match(m["name"]) and stem_of(m["name"]) in blocked:
            m["status"] = "rueckfall"
            m["reason"] = "lokalisierte Fassung bleibt SD"


def archive_path(meta):
    """Pfad im Archiv in der Schreibweise des Originals (Backslash)."""
    return meta["output"].replace("/", "\\")


def summarize(manifest):
    from collections import Counter
    st = Counter(m.get("status") for m in manifest["textures"])
    by_kind = Counter((m["kind"], m.get("status")) for m in manifest["textures"])
    print(f"Ergebnis: {dict(st)}")
    for (kind, status), n in sorted(by_kind.items()):
        print(f"  {kind:12s} {status:10s} {n}")
    reasons = Counter(s["reason"] for s in manifest["skipped"])
    for r, n in reasons.most_common():
        print(f"  ausgeschlossen: {r}: {n}")
    fmt = Counter(m.get("format") for m in manifest["textures"] if m.get("status") == "ok")
    print(f"  Formate: {dict(fmt)}")
    for a in manifest["archives"]:
        print(f"  {a['archive']}: {a['files']} Dateien, {a['bytes'] / 1048576:.0f} MB")
    c = manifest.get("cursors", {})
    if c.get("files") or c.get("failed"):
        print(f"  Mauszeiger: {c['files']}, fehlgeschlagen {len(c['failed'])}")
    bad = [m for m in manifest["textures"] if m.get("status") == "fehler"]
    for m in bad[:10]:
        print(f"  FEHLER {m['name']}: {m.get('reason')}")


# =================================================================================================
# Selbsttest (ohne EA-Daten)
# =================================================================================================
def selftest():
    fails = []

    def check(cond, what):
        if not cond:
            fails.append(what)
            print("  FEHLGESCHLAGEN:", what)

    # BIG: schreiben, mit dem Leser des Bestandswerkzeugs lesen
    with tempfile.TemporaryDirectory() as tmp:
        p = os.path.join(tmp, "t.big")
        entries = [("Art\\Textures\\b.dds", b"B" * 10), ("Art\\Textures\\A.dds", b"AA"), ("Data\\German\\Art\\Textures\\c.dds", b"")]
        write_big(p, entries)
        big = inv.BigArchive(p)
        check(sorted(big.entries) == sorted(n.lower() for n, _ in entries), "BIG: Namen")
        check(big.read("art\\textures\\b.dds") == b"B" * 10, "BIG: Inhalt")
        check(big.read("ART/TEXTURES/A.DDS") == b"AA", "BIG: Gross-/Kleinschreibung")
        check(big.read("data\\german\\art\\textures\\c.dds") == b"", "BIG: leere Datei")
        raw = open(p, "rb").read()
        check(struct.unpack("<I", raw[4:8])[0] == len(raw), "BIG: Gesamtgroesse")
        first = min(v[0] for v in big.entries.values())
        check(struct.unpack(">I", raw[12:16])[0] == first, "BIG: Ende des Verzeichnisses")
    groups = split_archives([("a", b"x" * 6), ("b", b"x" * 6), ("c", b"x" * 6)], limit=13)
    check([len(g) for g in groups] == [2, 1], "BIG: Aufteilung nach Groesse")

    # DDS-Kopf und Stufengroessen
    head = dds_header(256, 64, 9, "DXT5")
    check(len(head) == 128, "DDS: Kopflaenge")
    check(parse_dds(head) == ("DXT5", 256, 64, 9, 128), "DDS: Kopf lesen")
    check(inv.texture_header(head, "x.dds") == ("DXT5", 256, 64, 9), "DDS: Kopf mit texture_inventory")
    # BC7 (TODO 58): DX10-Kopf lesen, Zielformat je Richtlinie
    dx10 = head[:84] + b"DX10" + head[88:] + struct.pack("<IIIII", DXGI_BC7_UNORM, 3, 0, 1, 0)
    check(parse_dds(dx10) == ("BC7", 256, 64, 9, 148), "DDS: DX10-Kopf BC7")
    check(target_fourcc("DXT1", False, "alpha") == "DXT1" and target_fourcc("DXT5", True, "alpha") == "BC7"
          and target_fourcc("DXT1", False, "alle") == "BC7" and target_fourcc("TGA32", True, None) == "DXT5",
          "BC7: Zielformat je Richtlinie")
    check(dxt_level_size(256, 64, "DXT1") == 64 * 16 * 8, "DDS: DXT1-Stufe")
    check(dxt_level_size(2, 1, "DXT5") == 16, "DDS: kleinste Stufe")
    check(mip_dims(256, 64)[-1] == (1, 1) and len(mip_dims(256, 64)) == 9, "DDS: Mip-Kette")

    # Auswahl
    excl = {"Baum": {"tree01"}, "Schatten": {"shadowi"}}
    rec = {"name": "art\\textures\\tree01.tga", "kind": "Modell", "width": 64, "height": 64, "format": "TGA32"}
    check(exclusion_reason(rec, excl) == "Baum", "Auswahl: Baum")
    check(exclusion_reason(dict(rec, name="art\\textures\\zhca_tank.dds", format="DXT5"), excl).startswith("Teamfarbe"), "Auswahl: ZHC")
    check(exclusion_reason(dict(rec, name="art\\terrain\\grass.tga", kind="Gelaende"), excl).startswith("Gelaende"), "Auswahl: Gelaende")
    check(exclusion_reason(dict(rec, name="art\\textures\\ok.dds", format="DXT1"), excl) is None, "Auswahl: normale Textur")
    check(exclusion_reason(dict(rec, name="data\\german\\art\\textures\\ok.tga", format="TGA32"), excl) is None, "Auswahl: lokalisiert")
    check(exclusion_reason(dict(rec, name="data\\german\\art\\w3d\\x.tga", format="TGA32"), excl).startswith("Pfad"), "Auswahl: Pfad")
    check(exclusion_reason(dict(rec, name="art\\textures\\tiny.dds", width=8, height=8, format="DXT1"), excl) == "zu klein", "Auswahl: zu klein")
    check(output_name("art\\textures\\ui.tga") == "art\\textures\\ui.dds", "Auswahl: Ausgabename")
    check(target_fourcc("DXT1", True) == "DXT1" and target_fourcc("TGA32", True) == "DXT5"
          and target_fourcc("TGA24", False) == "DXT1", "Auswahl: Zielformat")
    check(stem_of("Art/Textures/ShadowI.TGA") == "shadowi" and stem_of("TreeA") == "treea", "Auswahl: Stamm")
    res = [{"name": "art\\textures\\abc_d.dds", "status": "rueckfall"}, {"name": "art\\textures\\abc.dds", "status": "ok"},
           {"name": "art\\textures\\abc_ns.dds", "status": "ok"}, {"name": "art\\textures\\abx.dds", "status": "ok"}]
    apply_variant_fallback(res)
    check([r["status"] for r in res] == ["rueckfall", "rueckfall", "rueckfall", "ok"], "Auswahl: Varianten fallen gemeinsam zurueck")
    res = [{"name": "art\\textures\\menu.tga", "status": "ok"}, {"name": "art\\textures\\other.dds", "status": "ok"}]
    apply_localized_fallback(res, ["art\\textures\\menu.tga", "data\\german\\art\\textures\\menu.tga", "art\\textures\\other.dds"])
    check([r["status"] for r in res] == ["rueckfall", "ok"], "Auswahl: lokalisierte SD-Fassung schuetzt")

    if not HAVE_IMAGING:
        print("  (numpy/Pillow fehlen: Bildverarbeitung nicht geprueft)")
    else:
        params = {"sharpen": 0.3, "deblock": 0.3, "min_psnr": 28.0, "ai_cmd": None, "ai_kinds": ["Modell"]}
        # kachelbares Muster: Sinus mit ganzer Periode; nicht kachelbar: Rampe
        y, x = np.mgrid[0:64, 0:64].astype(np.float32)
        tile = np.zeros((64, 64, 4), np.uint8)
        tile[..., 0] = (127 + 120 * np.sin(2 * np.pi * x / 32)).astype(np.uint8)
        tile[..., 1] = (127 + 120 * np.cos(2 * np.pi * y / 16)).astype(np.uint8)
        tile[..., 2] = 90
        tile[..., 3] = 255
        ramp = tile.copy()
        ramp[..., 0] = (x * 4).astype(np.uint8)
        check(is_tileable(tile[..., :3], 1) and is_tileable(tile[..., :3], 0), "Bild: kachelbar erkannt")
        check(not is_tileable(ramp[..., :3], 1), "Bild: Rampe nicht kachelbar")
        big, wrap = upscale_classic(tile, 2, "none", params, 0.0)
        check(big.shape == (128, 128, 4), "Bild: Groesse 2x")
        # Naht: Sprung ueber den Rand nicht groesser als innen
        check(is_tileable(big[..., :3], 1) and is_tileable(big[..., :3], 0), "Bild: kachelbar nach Vergroesserung")
        levels = build_mips(big, "DXT1", "none")
        check(len(levels) == 8 and levels[-1].shape[:2] == (1, 1), "Bild: Mip-Kette 128 bis 1")
        dds = encode_dds(levels, "DXT1")
        check(parse_dds(dds)[:4] == ("DXT1", 128, 128, 8), "Bild: DDS-Kopf")
        check(len(dds) == 128 + sum(dxt_level_size(w, h, "DXT1") for w, h in mip_dims(128, 128)), "Bild: DDS-Laenge")
        q = compare_to_sd(dds, tile)
        check(q > 30.0, f"Bild: Rueckvergleich {q:.1f} dB")
        check(encode_dds(build_mips(upscale_classic(tile, 2, "none", params, 0.0)[0], "DXT1", "none"), "DXT1") == dds,
              "Bild: reproduzierbar")
        # BC7-Eingabe fuer texconv: unkomprimiertes RGBA-DDS mit Mips, verlustfrei lesbar
        raw = dds_rgba(levels)
        check(len(raw) == 128 + sum(w * h * 4 for w, h in mip_dims(128, 128)), "BC7: Laenge der RGBA-Eingabe")
        back = np.array(Image.open(io.BytesIO(raw)).convert("RGBA"))
        check((back == levels[0]).all(), "BC7: RGBA-Eingabe verlustfrei (Kanalreihenfolge)")
        check(bc7_key(raw) == bc7_key(dds_rgba(levels)), "BC7: Schluessel reproduzierbar")
        # 1-Bit-Alpha: Abdeckung in allen Stufen erhalten
        cut = tile.copy()
        cut[..., 3] = np.where((x - 32) ** 2 + (y - 32) ** 2 < 20 ** 2, 255, 0).astype(np.uint8)
        cut[cut[..., 3] == 0, :3] = 0
        big, _ = upscale_classic(cut, 2, "binary", params, 0.3)
        check(set(np.unique(big[..., 3])) <= {0, 255}, "Alpha: binaer")
        lv = build_mips(big, "DXT1", "binary")
        c0 = coverage(lv[0][..., 3])
        check(all(abs(coverage(l[..., 3]) - c0) < 0.08 for l in lv[:5]), "Alpha: Abdeckung der Mips")
        check(big[64, 64, 0] > 0, "Alpha: keine schwarze Farbe im sichtbaren Bereich")
        dds = encode_dds(lv, "DXT1")
        dec = decode(dds, "x.dds")
        check(abs(coverage(dec[..., 3]) - c0) < 0.02, "Alpha: DXT1 mit Durchstanz-Alpha")
        # weiches Alpha nach DXT5
        soft = tile.copy()
        soft[..., 3] = (x * 4).astype(np.uint8)
        big, _ = upscale_classic(soft, 4, "smooth", params, 0.0)
        check(big.shape == (256, 256, 4), "Bild: Groesse 4x")
        dds = encode_dds(build_mips(big, "DXT5", "smooth"), "DXT5")
        check(compare_to_sd(dds, soft) > 28.0, "Bild: 4x Rueckvergleich")
        # Nahtlos: verschiedene Details an beiden Kanten werden links auf die Fortsetzung uebergeblendet
        padded = np.zeros((40, 40, 3), np.uint8)
        padded[:, :] = 100
        padded[:, 36:40] = 200		# Rand rechts (Fortsetzung) hell
        fixed = seamless(padded, 4, 32, 32, (True, False))
        check(fixed[10, 4, 0] > 180 and fixed[10, 7, 0] < 120, "KI: nahtlos (linke Kante folgt der Fortsetzung)")
        tga = encode_tga(np.zeros((128, 128, 4), np.uint8) + 255, "none", 2)
        check(tga[0] == 5 and tga[18:23] == b"ZHTS2" and tga[16] == 24, "Gelaende: TGA-Kennung ZHTS2")
        check(Image.open(io.BytesIO(tga)).size == (128, 128), "Gelaende: TGA mit Kennung lesbar")
        # Oberflaeche: Teilbilder fuer sich (kein Ausbluten der Nachbarn), TGA unkomprimiert
        atlas = np.zeros((16, 16, 4), np.uint8)
        atlas[..., 3] = 255
        atlas[0:5, 0:5, :3] = (0, 255, 255)		# Teilbild 1 cyan
        atlas[0:5, 5:10, :3] = (255, 0, 0)		# Nachbar rot
        big0, _ = upscale_classic(atlas, 2, "none", params, 0.0)
        bigr = upscale_rects(atlas, big0, 2, "none", params, [(0, 0, 5, 5), (5, 0, 10, 5)])
        check(bigr[0:10, 8:10, 0].max() <= 2 and bigr[0:10, 0:10, 1].min() >= 250, "Oberflaeche: Teilbild ohne Ausbluten")
        check(big0[0:10, 8:10, 0].max() > 20, "Oberflaeche: ohne Teilbilder blutet der Nachbar (Gegenprobe)")
        tga = encode_tga(bigr, "none")
        check(tga[2] == 2 and tga[16] == 24 and (tga[17] & 0x20) == 0, "Oberflaeche: TGA Typ 2, 24 Bit, unten links")
        back = np.array(Image.open(io.BytesIO(tga)).convert("RGB"))
        check((back == bigr[..., :3]).all(), "Oberflaeche: TGA verlustfrei")
        check(job_output_name({"name": "art\\textures\\ui.tga", "kind": "Oberflaeche"}) == "art\\textures\\ui.tga"
              and job_output_name({"name": "art\\textures\\m.tga", "kind": "Modell"}) == "art\\textures\\m.dds",
              "Oberflaeche: Ausgabename")
        # KI-Weg ohne Programm: Rand anlegen, "4x" per naechstem Nachbarn simulieren, Rand abschneiden, auf 2x
        inp, pad = ai_prepare(tile, "none")
        check(inp.shape == (64 + 2 * pad, 64 + 2 * pad, 3) and pad == AI_PAD, "KI: Rand")
        check((inp[pad:-pad, pad:-pad] == tile[..., :3]).all(), "KI: Mitte unveraendert")
        check((inp[:pad, pad:-pad] == tile[-pad:, :, :3]).all(), "KI: Wiederholung an kachelbarer Kante")
        fake = np.repeat(np.repeat(inp, 4, axis=0), 4, axis=1)
        fin = ai_finish(fake, pad, 4, 64, 64, 2)
        check(fin.shape == (128, 128, 3), "KI: Zuschnitt und Lanczos auf 2x")
        check(abs(fin.astype(float).mean() - tile[..., :3].astype(float).mean()) < 2.0, "KI: Helligkeit erhalten")
        # Colour matching after the refinement: mean and spread of the SD texture, alpha untouched, no clipping
        rng = np.random.default_rng(7)
        sd = np.zeros((32, 32, 4), np.uint8)
        sd[..., :3] = np.clip(rng.normal((150, 110, 70), (35, 25, 20), (32, 32, 3)), 0, 255).astype(np.uint8)
        sd[..., 3] = 255
        sd[:8, :, 3] = 0		# transparent rows with colour that must not count
        sd[:8, :, :3] = 255
        up = np.repeat(np.repeat(sd, 2, axis=0), 2, axis=1)
        grey = up.copy()
        grey[..., :3] = np.clip(up[..., :3].astype(np.float32) * 0.8 + 0.2 * 128 - 6 +
                                rng.normal(0, 3, up[..., :3].shape), 0, 255).astype(np.uint8)
        grey[..., 3] = (np.arange(64)[None, :] * 4).astype(np.uint8)	# alpha of the result differs on purpose
        fixed, coeffs = match_colors(grey, sd)
        vis = sd[..., 3] >= 8
        small = np.stack([resize_f(fixed[..., c].astype(np.float32), 32, 32, Image.BOX) for c in range(3)], -1)
        ok_mean = all(abs(small[..., c][vis].mean() - sd[..., c][vis].mean()) < 1.0 for c in range(3))
        ok_std = all(abs(small[..., c][vis].std() - sd[..., c][vis].std()) < 1.0 for c in range(3))
        before = np.stack([resize_f(grey[..., c].astype(np.float32), 32, 32, Image.BOX) for c in range(3)], -1)
        check(ok_mean and ok_std and coeffs and all(g > 1.0 for g, _ in coeffs),
              "Farbe: Mittel und Streuung wie SD nach der Angleichung "
              f"(vorher Rot {before[..., 0][vis].mean():.1f}/{before[..., 0][vis].std():.1f}, "
              f"nachher {small[..., 0][vis].mean():.1f}/{small[..., 0][vis].std():.1f}, "
              f"SD {sd[..., 0][vis].mean():.1f}/{sd[..., 0][vis].std():.1f})")
        check((fixed[..., 3] == grey[..., 3]).all(), "Farbe: Alpha unberuehrt")
        vis_up = np.repeat(np.repeat(vis, 2, axis=0), 2, axis=1)
        clipped = float(((fixed[..., :3] == 0) | (fixed[..., :3] == 255))[vis_up].mean())
        clipped_sd = float(((sd[..., :3] == 0) | (sd[..., :3] == 255))[vis].mean())
        check(clipped <= clipped_sd + 0.005, f"Farbe: kein Abschneiden ({clipped:.4f} gegen SD {clipped_sd:.4f})")
        flat = up.copy()
        flat[..., :3] = 90
        same, coeffs = match_colors(flat, sd)
        check(coeffs and all(g == 1.0 for g, _ in coeffs) and (same[..., :3] == same[0, 0, :3]).all(),
              "Farbe: einfarbiges Ergebnis wird nicht zu Rauschen aufgeblasen")
        weak = up.copy()
        weak[..., :3] = (up[..., :3].astype(np.int32) // 16 + 120).astype(np.uint8)
        _, coeffs = match_colors(weak, sd)
        check(coeffs and all(g == MATCH_MAX_GAIN for g, _ in coeffs), "Farbe: Kontrast hoechstens verdoppelt")
        hidden = sd.copy()
        hidden[..., 3] = 0
        check(match_colors(up, hidden)[1] is None, "Farbe: ohne sichtbare Pixel unveraendert")
        # arten.json in a work folder may be hard-linked to another set: writing must not change the other one
        with tempfile.TemporaryDirectory() as tmp:
            a_p, b_p = os.path.join(tmp, "a.json"), os.path.join(tmp, "b.json")
            with open(a_p, "w") as f:
                f.write('{"x": "Modell"}')
            linked = True
            try:
                os.link(a_p, b_p)
            except OSError:
                linked = False
            if linked:
                write_json_private(b_p, {"y": "Baum"})
                check(open(a_p).read() == '{"x": "Modell"}' and json.load(open(b_p)) == {"y": "Baum"},
                      "Arbeitsordner: Hardlink-Partner bleibt beim Schreiben unveraendert")
        check(params_hash({"bc7": None}) != params_hash({"bc7": None, "ai_match_color": True})
              and params_hash({}) == params_hash({"ai_match_color": False}),
              "Farbe: Zwischenstaende nur mit Angleichung neu")
        # Mauszeiger: .ani mit einem 32x32-Bild (4 Bit Palette, UND-Maske) -> 64x64, Hotspot verdoppelt
        pal = b"".join(struct.pack("<BBBB", 16 * i, 255 - 16 * i, 128, 0) for i in range(16))
        xor = bytes(((x // 2) % 16) << 4 | ((x // 2) % 16) for x in range(16)) * 32
        andm = (b"\xff\xff\x00\x00") * 16 + (b"\x00\x00\x00\x00") * 16
        dib = struct.pack("<IiiHHIIiiII", 40, 32, 64, 1, 4, 0, 0, 0, 0, 16, 0) + pal + xor + andm
        cur = struct.pack("<HHH", 0, 2, 1) + struct.pack("<BBBBHHII", 32, 32, 0, 0, 5, 7, len(dib), 22) + dib
        anih = struct.pack("<9I", 36, 1, 1, 0, 0, 0, 0, 10, 1)
        payload = b"ACON" + riff_chunk(b"anih", anih) + riff_chunk(b"LIST", b"fram" + riff_chunk(b"icon", cur))
        ani = b"RIFF" + struct.pack("<I", len(payload)) + payload
        rgba, hx, hy = decode_cur(cur)
        check(rgba.shape == (32, 32, 4) and (hx, hy) == (5, 7), "Zeiger: Bild lesen")
        check((rgba[0:16, :, 3] == 255).all() and (rgba[16:, :16, 3] == 0).all(), "Zeiger: UND-Maske")
        big_ani = upscale_ani(ani, 2, params)
        frames = [b for cid, body in riff_chunks(big_ani, 12, len(big_ani)) if cid == b"LIST"
                  for fid, b in riff_chunks(body, 4, len(body)) if fid == b"icon"]
        check(len(frames) == 1, "Zeiger: ein Bild")
        rgba2, hx2, hy2 = decode_cur(frames[0])
        check(rgba2.shape == (64, 64, 4) and (hx2, hy2) == (10, 14), "Zeiger: 2x mit Hotspot")
        check(abs(coverage(rgba2[..., 3]) - coverage(rgba[..., 3])) < 0.02, "Zeiger: Abdeckung")
        check(struct.unpack("<I", big_ani[4:8])[0] == len(big_ani) - 8, "Zeiger: RIFF-Laenge")

    print(f"Selbsttest: {'bestanden' if not fails else str(len(fails)) + ' Fehler'}")
    return 0 if not fails else 1


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--game", choices=("generals", "zh"), default="generals")
    ap.add_argument("--tier", choices=("hd", "uhd"), default="hd")
    ap.add_argument("--dir", help="Asset-Verzeichnis (Standard je Spiel)")
    ap.add_argument("--out", help="Ziel statt des Asset-Verzeichnisses (darunter Texturstufen\\<Stufe>), fuer Tests")
    ap.add_argument("--jobs", type=int, default=max(1, (os.cpu_count() or 2) - 1))
    ap.add_argument("--sharpen", type=float, default=0.3, help="Nachschaerfen (0 = aus)")
    ap.add_argument("--deblock", type=float, default=0.15, help="DXT-Bloecke vor dem Vergroessern glaetten (0 = aus)")
    ap.add_argument("--min-psnr", type=float, default=25.0, help="Rueckvergleich: darunter bleibt die Textur SD")
    ap.add_argument("--ai-cmd", help="KI-Hochskalierer, Platzhalter {in} {out} {scale} (HD-3: optional)")
    ap.add_argument("--ai-license", help="Lizenz des KI-Modells (fuer das Manifest)")
    ap.add_argument("--ai-kinds", default="Modell", help="Arten fuer die KI, kommagetrennt (nie Oberflaeche)")
    ap.add_argument("--ai-exe", help="realesrgan-ncnn-vulkan.exe (Stapelbetrieb, ein Aufruf fuer alle Texturen)")
    ap.add_argument("--ai-model", default="realesrgan-x4plus", help="Modell fuer --ai-exe")
    ap.add_argument("--ai-scale", type=int, default=4, help="Faktor des Modells (danach Lanczos auf die Stufe)")
    ap.add_argument("--ai-min-psnr", type=float, default=20.0,
                    help="Rueckvergleich fuer KI-Ergebnisse (KI entfernt Rauschen und Bloecke, PSNR faellt dadurch "
                         "niedriger aus); darunter gilt das klassische Ergebnis")
    ap.add_argument("--ai-match-color", action="store_true",
                    help="KI-Ergebnis (nach der Verfeinerung) je Kanal an Mittel und Streuung der SD-Textur angleichen; "
                         "der Rueckvergleich rechnet danach")
    ap.add_argument("--gelaende", action="store_true",
                    help="Gelaende- und Baumkacheln aufnehmen (TGA mit Kennung ZHTS<s>; braucht die Engine aus hd-ki2)")
    ap.add_argument("--ai-refine-python", help="Python mit PyTorch/diffusers fuer tools/assets/ki_verfeinern.py")
    ap.add_argument("--ai-refine", default="Modell=0.3,Gelaende=0.4,Baum=0.3",
                    help="Staerke der Diffusions-Verfeinerung je Art (0 = keine)")
    ap.add_argument("--stage", help="eigener Stufenordner unter Texturstufen (z. B. HD-KI), Vorgabe HD bzw. UHD")
    ap.add_argument("--bc7", choices=("aus", "alpha", "alle"), default="aus",
                    help="BC7 (TODO 58): alpha = statt DXT5, alle = auch statt DXT1 (doppelter Speicher)")
    ap.add_argument("--texconv", help="texconv.exe aus DirectXTex (MIT) fuer --bc7")
    ap.add_argument("--bc7-cpu", action="store_true", help="BC7 auf der CPU statt der Grafikkarte (etwa 40-mal langsamer)")
    ap.add_argument("--ai-reuse", help="fertige KI-Ausgaben (Ordner mit aus\\) eines anderen Satzes nur lesen, ohne KI-Lauf")
    ap.add_argument("--only-restore", action="store_true", help="nur die ZH-Halbierten aus ZH_Generals (Abschnitt 2.3)")
    ap.add_argument("--limit", type=int, help="nur die ersten N Texturen (Test)")
    ap.add_argument("--per-kind", type=int, help="nur N Texturen je Art, ohne Rueckholung (Prototyp)")
    ap.add_argument("--names", help="nur Texturen, deren Pfad einen dieser Teile enthaelt (kommagetrennt)")
    ap.add_argument("--selftest", action="store_true", help="Selbsttest ohne EA-Daten")
    a = ap.parse_args()
    if a.selftest:
        return selftest()
    root = a.dir or inv.default_dir(a.game)
    if not os.path.isdir(root):
        raise SystemExit(f"Asset-Verzeichnis fehlt: {root}")
    if (a.ai_cmd or a.ai_exe) and not a.ai_license:
        raise SystemExit("--ai-cmd/--ai-exe braucht --ai-license (HD-3: die Lizenz des Modells steht im Manifest)")
    params = {"sharpen": a.sharpen, "deblock": a.deblock, "min_psnr": a.min_psnr, "ai_cmd": a.ai_cmd,
              "ai_license": a.ai_license, "ai_kinds": [k for k in a.ai_kinds.split(",") if k and k != "Oberflaeche"]}
    if a.ai_exe:
        if a.ai_scale < FACTORS[a.tier]:
            raise SystemExit("--ai-scale kleiner als der Faktor der Stufe")
        params.update(ai_exe=os.path.abspath(a.ai_exe), ai_model=a.ai_model, ai_scale=a.ai_scale,
                      ai_min_psnr=a.ai_min_psnr)
        if a.ai_refine_python:
            params.update(ai_refine_python=a.ai_refine_python,
                          ai_refine={k: float(v) for k, v in (p.split("=") for p in a.ai_refine.split(",") if p)})
    if a.bc7 != "aus":
        if not a.texconv or not os.path.isfile(a.texconv):
            raise SystemExit("--bc7 braucht --texconv <texconv.exe> (DirectXTex, MIT)")
        params.update(bc7=a.bc7, texconv=os.path.abspath(a.texconv), bc7_cpu=a.bc7_cpu)
    if a.ai_reuse:
        if not os.path.isdir(os.path.join(a.ai_reuse, "aus")):
            raise SystemExit(f"--ai-reuse: {a.ai_reuse}\\aus fehlt")
        if not a.ai_license:
            raise SystemExit("--ai-reuse braucht --ai-license (Lizenz der wiederverwendeten KI-Ausgaben)")
        params.update(ai_reuse=os.path.abspath(a.ai_reuse), ai_exe="(wiederverwendet)", ai_model=a.ai_model,
                      ai_scale=a.ai_scale, ai_min_psnr=a.ai_min_psnr)
    if a.ai_match_color:
        if not (a.ai_exe or a.ai_cmd or a.ai_reuse):
            raise SystemExit("--ai-match-color braucht --ai-exe, --ai-cmd oder --ai-reuse")
        params["ai_match_color"] = True
    if a.gelaende:
        params["terrain"] = True
        for k in ("Gelaende", "Baum"):
            if (a.ai_exe or a.ai_reuse) and k not in params["ai_kinds"]:
                params["ai_kinds"].append(k)
    build(root, a.game, a.tier, params, a.jobs, a.limit, a.only_restore,
          [n.lower() for n in a.names.split(",")] if a.names else None, a.out, a.per_kind, a.stage, a.gelaende)
    return 0


if __name__ == "__main__":
    sys.exit(main())
