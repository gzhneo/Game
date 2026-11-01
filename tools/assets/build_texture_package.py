#!/usr/bin/env python3
# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
"""Baut das Texturpaket aus der eigenen Installation.

Ein Paket statt drei Stufen: jede Textur liegt einmal in ihrer hoechsten Aufloesung mit voller Mip-Kette im
Paket, dazu ein Manifest mit der Originalgroesse (SD) jeder Textur. Das Spiel waehlt die Stufe wie bisher, laedt
das Paket mit Vorrang und ueberspringt beim Laden nur die oberen Mip-Stufen (Engine::TexturePackage,
ArchiveFileSystem::loadTextureTiers, TextureLoader). Ergebnis:

    <Ziel>\\Texturstufen\\Paket\\TexturesPaket.big (ab 1,5 GB geteilt: TexturesPaket2.big, ...)

jedes Archiv mit texturpaket.txt an der Wurzel:

    TEXTURPAKET 1 <hoechste Stufe SD|HD|UHD>
    <Pfad im Archiv> <Breite SD> <Hoehe SD>

Quelle der Fassungen sind die vorhandenen Stufenarchive (Texturstufen\\HD und \\UHD, mit --hd/--uhd andere Ordner,
etwa HD-KI2); je Textur gewinnt die hoechste Stufe. Die Originalgroesse kommt aus dem wirksamen Bestand der
EA-Archive (erster Treffer gewinnt, wie im Spiel; texture_inventory.py). Liest alles nur.

Regeln:
  * nur Texturen (.dds/.tga) unter Art\\Textures, Art\\Terrain und Data\\<Sprache>\\Art\\Textures;
  * Teamfarben: ZHCD bleibt draussen (Palette in der obersten Zeile, exakte Pixel; die Engine reduziert sie nie);
    ZHCA (Alpha-Maske, im Pixel-Shader eingefaerbt) kommt wie jede andere Textur hinein;
  * DDS ausserhalb von Art\\Terrain nur als DXT1 bis DXT5 oder BC7 (sonst liest die Engine sie nicht);
  * eine Fassung, die nicht groesser als das Original ist oder deren Seitenverhaeltnis abweicht, bleibt draussen
    (das Original gilt);
  * DDS mit unvollstaendiger Mip-Kette (DXT1/DXT5) bekommt eine volle Kette (Pillow, Box-Filter); BC7 ohne volle
    Kette bleibt, wie es ist, und wird gemeldet -- die Engine laedt sie dann in ihrer Dateigroesse;
  * TGA (Gelaende, Baeume, Oberflaeche, Teamfarben-freie Modelle) bleibt TGA: die Engine halbiert Kacheln und
    Texturen beim Laden (TerrainTileImage, TextureLoader).

Nichts davon verlaesst den Rechner: die Texturen sind EAs Eigentum.

Aufruf:
    python tools/assets/build_texture_package.py --dir <Asset-Verzeichnis>
    python tools/assets/build_texture_package.py --dir <Asset-Verzeichnis> --hd Texturstufen\\HD-KI2 --uhd Texturstufen\\UHD-KI2
    python tools/assets/build_texture_package.py --selftest
"""
import argparse
import io
import os
import struct
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import texture_inventory as inv  # noqa: E402
import build_texture_tiers as tiers  # noqa: E402

MANIFEST = "texturpaket.txt"
PACKAGE_DIR = os.path.join("Texturstufen", "Paket")
ARCHIVE_STEM = "TexturesPaket"
TIER_NAMES = ("SD", "HD", "UHD")


def allowed_path(name):
    """Textur unter den Pfaden, die die Engine im Paket annimmt (TextureTiers.cpp, isAllowedTierEntry)."""
    if not inv.is_texture(name):
        return False
    if name.startswith("art\\textures\\") or name.startswith("art\\terrain\\"):
        return True
    return name.startswith("data\\") and "\\art\\textures\\" in name


def stem_path(name):
    return name.rsplit(".", 1)[0]


def tier_files(folder):
    """Name -> (Archiv, BigArchive) der Archive eines Stufenordners; das Spiel laedt sie nach Namen sortiert mit
    Vorrang, also gewinnt das letzte."""
    result = {}
    if not os.path.isdir(folder):
        return result
    for rel in sorted((f for f in os.listdir(folder) if f.lower().endswith(".big")), key=str.lower):
        big = inv.BigArchive(os.path.join(folder, rel))
        for name in big.entries:
            result[name] = (rel, big)
    return result


def header(data, name):
    return inv.texture_header(data[:128], name)


def supported_dds(head):
    """Formate, die der DDS-Leser der Modelltexturen kennt (ddsfile.cpp): DXT1 bis DXT5 und BC7. Gelaende liest
    DirectXTex (TerrainTileImage) und nimmt dazu unkomprimiertes RGBA."""
    try:
        fourcc = tiers.parse_dds(head)[0]
    except (ValueError, struct.error):
        return False
    return fourcc in ("DXT1", "DXT2", "DXT3", "DXT4", "DXT5", "BC7")


def full_chain(width, height):
    return len(tiers.mip_dims(width, height))


def complete_mips(data, name, report):
    """DDS mit voller Mip-Kette; DXT1/DXT5 werden ergaenzt, alles andere bleibt und wird gemeldet."""
    fmt, w, h, mips = header(data, name)
    if not name.endswith(".dds") or mips >= full_chain(w, h):
        return data
    fourcc, _, _, _, _ = tiers.parse_dds(data)
    if fourcc not in ("DXT1", "DXT5") or not tiers.HAVE_IMAGING:
        report.setdefault("kette_unvollstaendig", []).append(name)
        return data
    rgba = tiers.decode(data, name)
    alpha = "none" if fourcc == "DXT1" and (rgba[..., 3] == 255).all() else ("binary" if fourcc == "DXT1" else "soft")
    levels = tiers.build_mips(rgba, fourcc, alpha)
    report.setdefault("kette_ergaenzt", []).append(name)
    return tiers.encode_dds(levels, fourcc)


def plan(sd_files, tier_sets):
    """Je Textur die hoechste Fassung: Liste (Pfad im Archiv, Daten-Lieferant, SD-Breite, SD-Hoehe, Stufe) und Bericht."""
    sd_by_stem = {}
    for name in sd_files:
        if allowed_path(name):
            sd_by_stem.setdefault(stem_path(name), name)
    chosen = {}
    for tier in (1, 2):
        for name, src in tier_sets.get(tier, {}).items():
            if allowed_path(name):
                chosen[stem_path(name)] = (name, src, tier)
    entries, report = [], {}
    for stem in sorted(chosen):
        name, (rel, big), tier = chosen[stem]
        base = stem.rsplit("\\", 1)[-1]
        if base.startswith("zhcd"):
            report.setdefault("teamfarbe", []).append(name)
            continue
        sd_name = sd_by_stem.get(stem)
        if sd_name is None:
            report.setdefault("ohne_original", []).append(name)
            continue
        _, sw, sh, _ = header(sd_files[sd_name][1].read_head(sd_name, 148), sd_name)
        head = big.read_head(name, 148)
        _, w, h, _ = header(head, name)
        if name.endswith(".dds") and not name.startswith("art\\terrain\\") and not supported_dds(head):
            report.setdefault("format", []).append(name)
            continue
        if sw <= 0 or sh <= 0 or w <= sw or h <= sh or w * sh != h * sw:
            report.setdefault("nicht_groesser", []).append(f"{name} {w}x{h} (SD {sw}x{sh})")
            continue
        entries.append((name, big, sw, sh, tier))
    return entries, report


def manifest_text(highest, lines):
    return ("TEXTURPAKET 1 %s\r\n" % TIER_NAMES[highest] + "".join("%s %d %d\r\n" % l for l in lines)).encode("latin-1")


def build(root, hd_dir, uhd_dir, out_root, limit=None):
    sd_files, _ = inv.effective_files(root)
    tier_sets = {1: tier_files(hd_dir), 2: tier_files(uhd_dir)}
    entries, report = plan(sd_files, tier_sets)
    if limit:
        entries = entries[:limit]
    if not entries:
        print("Keine Fassung groesser als das Original gefunden -- kein Paket geschrieben.")
        return 1, report
    highest = max(e[4] for e in entries)
    files, lines = [], []
    for name, big, sw, sh, tier in entries:
        data = complete_mips(big.read(name), name, report)
        files.append((name, data))
        lines.append((name, sw, sh))
    out_dir = os.path.join(out_root, PACKAGE_DIR)
    os.makedirs(out_dir, exist_ok=True)
    for old in os.listdir(out_dir):
        if old.lower().startswith(ARCHIVE_STEM.lower()) and old.lower().endswith(".big"):
            os.remove(os.path.join(out_dir, old))
    groups = tiers.split_archives(files)
    by_name = {n: (sw, sh) for n, sw, sh in lines}
    for i, group in enumerate(groups):
        own = [(n, by_name[n][0], by_name[n][1]) for n, _ in group]
        archive = os.path.join(out_dir, ARCHIVE_STEM + (str(i + 1) if i else "") + ".big")
        tiers.write_big(archive, group + [(MANIFEST, manifest_text(highest, own))])
    per_tier = {t: sum(1 for e in entries if e[4] == t) for t in (1, 2)}
    total = sum(len(d) for _, d in files)
    print(f"Paket: {len(entries)} Texturen (HD {per_tier[1]}, UHD {per_tier[2]}), hoechste Stufe {TIER_NAMES[highest]}, "
          f"{total / 1048576:.1f} MB in {len(groups)} Archiv(en) unter {out_dir}")
    for key, items in sorted(report.items()):
        print(f"  {key}: {len(items)}" + (f" (etwa {items[0]})" if items else ""))
    return 0, report


# =================================================================================================
# Selbsttest ohne EA-Daten
# =================================================================================================
def _tga(width, height, ident=b""):
    head = struct.pack("<BBBHHBHHHHBB", len(ident), 0, 2, 0, 0, 0, 0, 0, width, height, 32, 8)
    return head + ident + b"\x40" * (width * height * 4)


def _dds(width, height, mips, fourcc="DXT1"):
    dims = tiers.mip_dims(width, height)[:mips]
    return tiers.dds_header(width, height, mips, fourcc) + b"".join(
        b"\x55" * tiers.dxt_level_size(w, h, fourcc) for w, h in dims)


def _rgba_dds(width, height):
    pf = struct.pack("<II4sIIIII", 32, 0x41, b"\0\0\0\0", 32, 0xFF, 0xFF00, 0xFF0000, 0xFF000000)
    head = struct.pack("<IIIIIII", 124, 0x100F, height, width, width * 4, 0, 1) + b"\0" * 44 + pf + struct.pack("<IIIII", 0x1000, 0, 0, 0, 0)
    return b"DDS " + head + b"\0" * (width * height * 4)


def selftest():
    fails = []

    def check(cond, what):
        if not cond:
            fails.append(what)
            print("  FEHLGESCHLAGEN:", what)

    with tempfile.TemporaryDirectory() as tmp:
        root = os.path.join(tmp, "assets")
        os.makedirs(os.path.join(root, "Texturstufen", "HD"))
        os.makedirs(os.path.join(root, "Texturstufen", "UHD"))
        tiers.write_big(os.path.join(root, "Textures.big"), [
            ("Art\\Textures\\Panzer.dds", _dds(64, 32, 7)),
            ("Art\\Textures\\Leiste.tga", _tga(32, 16)),
            ("Art\\Textures\\ZHCA_Infanterie.tga", _tga(16, 16)),
            ("Art\\Textures\\ZHCD_Fahne.tga", _tga(16, 16)),
            ("Art\\Textures\\Klein.dds", _dds(16, 16, 5)),
            ("Art\\Textures\\Schief.dds", _dds(32, 32, 6)),
            ("Art\\Textures\\Roh.tga", _tga(32, 32)),
            ("Art\\Terrain\\Gras.tga", _tga(128, 128)),
            ("Data\\INI\\Object.ini", b"; keine Textur\r\n"),
        ])
        tiers.write_big(os.path.join(root, "Texturstufen", "HD", "TexturesHD.big"), [
            ("Art\\Textures\\Panzer.dds", _dds(128, 64, 8)),
            ("Art\\Textures\\Leiste.tga", _tga(64, 32)),
            ("Art\\Textures\\ZHCA_Infanterie.tga", _tga(32, 32)),
            ("Art\\Textures\\ZHCD_Fahne.tga", _tga(32, 32)),
            ("Art\\Textures\\Klein.dds", _dds(16, 16, 5)),		# nicht groesser
            ("Art\\Textures\\Schief.dds", _dds(64, 32, 7)),		# anderes Seitenverhaeltnis
            ("Art\\Textures\\Neu.dds", _dds(64, 64, 7)),			# ohne Original
            ("Art\\Textures\\Roh.dds", _rgba_dds(64, 64)),			# unkomprimiert
            ("Art\\Terrain\\Gras.tga", _tga(256, 256, b"ZHTS2")),
        ])
        tiers.write_big(os.path.join(root, "Texturstufen", "UHD", "TexturesUHD.big"), [
            ("Art\\Textures\\Panzer.dds", _dds(256, 128, 3)),		# Kette unvollstaendig
            ("Art\\Terrain\\Gras.tga", _tga(512, 512, b"ZHTS4")),
        ])
        out = os.path.join(tmp, "out")
        rc, report = build(root, os.path.join(root, "Texturstufen", "HD"), os.path.join(root, "Texturstufen", "UHD"), out)
        check(rc == 0, "Bau: Rueckgabewert")
        archive = os.path.join(out, PACKAGE_DIR, ARCHIVE_STEM + ".big")
        check(os.path.isfile(archive), "Bau: Archiv geschrieben")
        big = inv.BigArchive(archive)
        names = sorted(big.entries)
        check(names == ["art\\terrain\\gras.tga", "art\\textures\\leiste.tga", "art\\textures\\panzer.dds",
                         "art\\textures\\zhca_infanterie.tga", MANIFEST],
              "Auswahl: %s" % names)
        text = big.read(MANIFEST).decode("latin-1").split("\r\n")
        check(text[0] == "TEXTURPAKET 1 UHD", "Manifest: Kopf %r" % text[0])
        check("art\\textures\\panzer.dds 64 32" in text and "art\\terrain\\gras.tga 128 128" in text
              and "art\\textures\\leiste.tga 32 16" in text, "Manifest: Originalgroessen %r" % text)
        check(header(big.read("art\\terrain\\gras.tga"), "x.tga")[1] == 512, "Gelaende: UHD-Fassung gewinnt")
        check(header(big.read("art\\textures\\leiste.tga"), "x.tga")[1] == 64, "Oberflaeche: HD-Fassung, TGA bleibt TGA")
        check(report.get("teamfarbe") == ["art\\textures\\zhcd_fahne.tga"], "Teamfarbe mit Palette ausgeschlossen")
        check("art\\textures\\zhca_infanterie.tga 16 16" in text, "Teamfarbe mit Maske im Paket")
        check(len(report.get("nicht_groesser", [])) == 2, "nicht groesser / Seitenverhaeltnis: %s" % report.get("nicht_groesser"))
        check(report.get("ohne_original") == ["art\\textures\\neu.dds"], "ohne Original")
        check(report.get("format") == ["art\\textures\\roh.dds"], "unkomprimiertes DDS ausgeschlossen")
        panzer = big.read("art\\textures\\panzer.dds")
        _, w, h, mips = header(panzer, "x.dds")
        if tiers.HAVE_IMAGING:
            check((w, h, mips) == (256, 128, 9), "Mip-Kette ergaenzt: %dx%d, %d Stufen" % (w, h, mips))
            check(len(panzer) == 128 + sum(tiers.dxt_level_size(a, b, "DXT1") for a, b in tiers.mip_dims(256, 128)),
                  "Mip-Kette: Dateigroesse")
            check(report.get("kette_ergaenzt") == ["art\\textures\\panzer.dds"], "Bericht: Kette ergaenzt")
        else:
            print("  (ohne numpy/Pillow: Mip-Ergaenzung nicht geprueft)")
        # leerer Bestand: kein Paket
        empty = os.path.join(tmp, "leer")
        os.makedirs(empty)
        tiers.write_big(os.path.join(empty, "Textures.big"), [("Art\\Textures\\a.dds", _dds(8, 8, 4))])
        rc, _ = build(empty, os.path.join(empty, "x"), os.path.join(empty, "y"), os.path.join(tmp, "out2"))
        check(rc == 1 and not os.path.exists(os.path.join(tmp, "out2", PACKAGE_DIR)), "ohne Stufen: kein Paket")

    print("Selbsttest: " + ("bestanden" if not fails else f"{len(fails)} Fehler"))
    return 0 if not fails else 1


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--dir", help="Asset-Verzeichnis (EA-Archive und Texturstufen)")
    ap.add_argument("--hd", default=os.path.join("Texturstufen", "HD"), help="Stufenordner HD, relativ zu --dir")
    ap.add_argument("--uhd", default=os.path.join("Texturstufen", "UHD"), help="Stufenordner UHD, relativ zu --dir")
    ap.add_argument("--out", help="Ziel statt des Asset-Verzeichnisses (darunter Texturstufen\\Paket)")
    ap.add_argument("--limit", type=int, help="nur die ersten N Texturen (Test)")
    ap.add_argument("--selftest", action="store_true", help="Selbsttest ohne EA-Daten")
    a = ap.parse_args()
    if a.selftest:
        return selftest()
    if not a.dir:
        ap.error("--dir fehlt")
    rc, _ = build(a.dir, os.path.join(a.dir, a.hd), os.path.join(a.dir, a.uhd), a.out or a.dir, a.limit)
    return rc


if __name__ == "__main__":
    sys.exit(main())
