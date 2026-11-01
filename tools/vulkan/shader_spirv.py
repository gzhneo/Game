#!/usr/bin/env python3
# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
"""Shader nach SPIR-V uebersetzen und pruefen (Vulkan-Backend).

Uebersetzt jede Shader-Variante, die das D3D11-Backend zur Laufzeit baut, mit DXC nach SPIR-V
(Vulkan 1.3) und prueft das Ergebnis mit spirv-val und die Bindungsnummern (Bereich je
Registerklasse). Der Variantenkatalog unten bildet die Makros nach, mit denen das Backend
`Compile_FFP_Shader` aufruft; jede Datei aus Data/Shaders/sm4, die dort nicht vorkommt, wird
einmal ohne Makros uebersetzt.

DXC: unter Windows dxc.exe (vcpkg directx-dxc, Variable DIRECTX_DXC_TOOL), unter Linux dieselbe
dxc.exe aus dem NuGet-Paket Microsoft.Direct3D.DXC unter Wine (der Sitzungsstart-Hook legt sie nach
/opt/dxc-<Version>). Gesucht wird in dieser Reihenfolge: --dxc, Umgebungsvariable DXC, /opt/dxc-*/dxc.exe,
dxc bzw. dxc.exe im Suchpfad.

Bindungen wie zur Laufzeit (vulkandxc.cpp, vulkanspirv.h): Konstantenpuffer (b) behalten ihre Nummer,
Texturen (t) liegen ab 16, Sampler (s) ab 32, alles in Satz 0; die Geraeteschicht legt beim Laden jede
Stufe in ihren eigenen Satz. Die Konstantenpuffer behalten das D3D-Layout (-fvk-use-dx-layout), damit die
C++-Spiegel in rendercb.h fuer beide APIs gelten; SV_Position.w im Pixel-Shader wie unter Direct3D
(-fvk-use-dx-position-w). Sprachstand HLSL 2018 (-HV 2018) wie bei
d3dcompiler_47: ab HLSL 2021 sind ?: und && auf Vektoren nicht mehr komponentenweise erlaubt
(post.ps.hlsl, terraintess.hs.hlsl); eine Umschreibung mit lerp oder select aenderte unter D3D11
die letzten Bits des Bildes.

Aufruf:
    python3 tools/vulkan/shader_spirv.py                 alle Varianten, Ausgabe in einem Temp-Ordner
    python3 tools/vulkan/shader_spirv.py --out ORDNER    SPIR-V-Dateien behalten (fuer modulpruefung)
    python3 tools/vulkan/shader_spirv.py --liste         nur den Katalog ausgeben
    python3 tools/vulkan/shader_spirv.py --nur ffp.vs    nur Varianten, deren Name den Text enthaelt
    python3 tools/vulkan/shader_spirv.py --bindungen     zusaetzlich die belegten Bindungsnummern

Rueckgabewert: 0 = alles uebersetzt und gueltig, 1 = mindestens eine Variante nicht,
2 = Werkzeug fehlt oder Aufruffehler.
"""

import argparse
import concurrent.futures
import glob
import os
import shutil
import subprocess
import sys
import tempfile
import time

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
SHADER_DIR = os.path.join(ROOT, "Data", "Shaders", "sm4")

# Binding shifts per register class, see the module text.
SHIFT_T = 16
SHIFT_S = 32
SHIFT_U = 48


def _stage(name):
    for suffix, stage in ((".vs.hlsl", "vs"), (".ps.hlsl", "ps"), (".hs.hlsl", "hs"), (".ds.hlsl", "ds")):
        if name.endswith(suffix):
            return stage
    return None


def katalog():
    """Liste von (Datei, Makros) -- Makros als Tupel von (Name, Wert), wie das Backend sie setzt."""
    v = []

    def add(datei, *makros):
        v.append((datei, tuple(makros)))

    # Uber vertex shader: RenderCore::Get_FFP_Vertex_Shader (bits: normal, diffuse, specular,
    # texture coordinate sets 0..4, instanced, pixel light) and the tessellation input RenderCore::Tt_Vertex_Shader.
    for light in (False, True):
        for instanced in (False, True):
            for bits in range(8):
                if light and not bits & 1:
                    continue  # PIXEL_LIGHT only with a normal (RenderCore::Prepare_Draw)
                for tc in range(5):
                    m = []
                    if instanced:
                        m.append(("INSTANCED", "1"))
                    if light:
                        m.append(("PIXEL_LIGHT", "1"))
                    if bits & 1:
                        m.append(("HAS_NORMAL", "1"))
                    if bits & 2:
                        m.append(("HAS_DIFFUSE", "1"))
                    if bits & 4:
                        m.append(("HAS_SPECULAR", "1"))
                    m.append(("NUM_TEXCOORDS", str(tc)))
                    add("ffp.vs.hlsl", *m)
    for bits in range(8):
        for tc in range(5):
            m = []
            if bits & 1:
                m.append(("HAS_NORMAL", "1"))
            if bits & 2:
                m.append(("HAS_DIFFUSE", "1"))
            if bits & 4:
                m.append(("HAS_SPECULAR", "1"))
            m.append(("NUM_TEXCOORDS", str(tc)))
            m.append(("TERRAIN_TESS", "1"))
            add("ffp.vs.hlsl", *m)

    # Uber pixel shader, its soft particle variant (Get_FFP_Soft_Pixel_Shader) and the alpha-to-coverage variant.
    add("ffp.ps.hlsl")
    add("ffp.ps.hlsl", ("SOFT_PARTICLES", "1"))
    add("ffp.ps.hlsl", ("ALPHA_COVERAGE", "1"))  # Get_FFP_Coverage_Pixel_Shader
    add("ffp.ps.hlsl", ("PIXEL_LIGHT", "1"))  # Get_FFP_Light_Pixel_Shader
    add("ffp.ps.hlsl", ("PIXEL_LIGHT", "1"), ("ALPHA_COVERAGE", "1"))

    # Shadow map vertex shader: RenderCore::Sh_Vertex_Shader, variants 0..11.
    for instanced in (False, True):
        for base in range(5 if instanced else 7):
            m = []
            if base == 5:
                m.append(("SHADOW_TREES", "1"))
            elif base == 6:
                m.append(("SHADOW_SKIN", "1"))
            elif base >= 1:
                m.append(("SHADOW_UV", str(base - 1)))
            if instanced:
                m.append(("SHADOW_INSTANCED", "1"))
            add("shadow.vs.hlsl", *m)

    # Post processing: RenderCore::Fx_Post_Shader (variants 1..127; the heat bit alone is heat.ps).
    post_bits = (("DO_FXAA", 1), ("DO_CAS", 2), ("DO_GRADE", 4), ("DO_HAZE", 8), ("DO_LIGHTS", 32), ("DO_DEBAND", 64))
    for variant in range(1, 128):
        if variant == 16:
            add("heat.ps.hlsl", ("HEAT_PASS", "1"))
            continue
        add("post.ps.hlsl", *[(name, "1") for name, bit in post_bits if variant & bit])
    add("post.ps.hlsl", ("DO_GAMMA", "1"))  # Fx_Gamma_Shader

    # Bloom chain and heat mask (Fx_Glow_Shader), SSAO passes, upscale filters.
    for index in range(4):
        add("bloom.ps.hlsl", ("BLOOM_PASS", str(index)))
    add("heat.ps.hlsl", ("HEAT_PASS", "0"))
    for p in range(3):
        add("ssao.ps.hlsl", ("SSAO_PASS", str(p)))
    for p in range(2):
        add("gtao.ps.hlsl", ("GTAO_PASS", str(p)))
    for p in range(2):
        add("taa.ps.hlsl", ("TAA_PASS", str(p)))
    for f in range(4):
        add("upscale.ps.hlsl", ("UPSCALE_FILTER", str(f)))
    # Shadow mask and the atmosphere after the occlusion (Sh_Ensure_Resources).
    add("shadowmask.ps.hlsl")
    add("shadowmask.ps.hlsl", ("ATMOSPHERE", "1"))
    # Scene conversion with the alpha target of a scene target without alpha (Fx_Convert_Scene).
    add("sceneconvert.ps.hlsl", ("SCENE_ALPHA", "1"))
    add("sceneconvert.ps.hlsl", ("TONEMAP", "1"))
    add("sceneconvert.ps.hlsl", ("TONEMAP", "1"), ("SCENE_ALPHA", "1"))

    # Terrain pixel shaders loaded by the game keep their source for the detail, rock and relief
    # variants (Td_Pixel_Shader, Tr_Pixel_Shader, Tn_Pixel_Shader, Tb_Pixel_Shader; Td_Is_Terrain_Shader names them).
    for datei in ("terrain.ps.hlsl", "terrainnoise.ps.hlsl", "terrainnoise2.ps.hlsl", "roadnoise2.ps.hlsl"):
        add(datei)
        add(datei, ("TERRAIN_DETAIL", "1"))
        add(datei, ("TERRAIN_ROCK", "1"))
        add(datei, ("TERRAIN_ROCK", "1"), ("TERRAIN_DETAIL", "1"))
        for detail in (False, True):
            for rock in (False, True):
                m = [("TERRAIN_NORMAL", "1")]
                if detail:
                    m.append(("TERRAIN_DETAIL", "1"))
                if rock:
                    m.append(("TERRAIN_ROCK", "1"))
                add(datei, *m)
        # Terrain light per pixel with every combination of the other layers (Tb_Pixel_Shader).
        for k in range(8):
            m = [("TERRAIN_LIGHT", "1")]
            if k & 1:
                m.append(("TERRAIN_DETAIL", "1"))
            if k & 2:
                m.append(("TERRAIN_ROCK", "1"))
            if k & 4:
                m.append(("TERRAIN_NORMAL", "1"))
            add(datei, *m)

    # Every other file once without macros (game shaders by name, full screen, particles, shadows,
    # tessellation stages).
    genannt = {d for d, _ in v}
    for pfad in sorted(glob.glob(os.path.join(SHADER_DIR, "*.hlsl"))):
        name = os.path.basename(pfad)
        if name not in genannt and _stage(name):
            add(name)

    # Dedupe while keeping the order (the post variants with the heat bit repeat others).
    seen = set()
    result = []
    for datei, makros in v:
        key = (datei, tuple(sorted(makros)))
        if key not in seen:
            seen.add(key)
            result.append((datei, makros))
    return result


def variant_name(datei, makros):
    if not makros:
        return datei
    return datei + " [" + " ".join(n if val == "1" else "%s=%s" % (n, val) for n, val in makros) + "]"


def finde_dxc(explizit):
    """(Befehl als Liste, braucht_wine) oder None."""
    kandidaten = []
    if explizit:
        kandidaten.append(explizit)
    if os.environ.get("DXC"):
        kandidaten.append(os.environ["DXC"])
    kandidaten += sorted(glob.glob("/opt/dxc-*/dxc.exe"), reverse=True)
    for name in ("dxc", "dxc.exe"):
        p = shutil.which(name)
        if p:
            kandidaten.append(p)
    for k in kandidaten:
        if not os.path.isfile(k):
            continue
        if k.lower().endswith(".exe") and os.name != "nt":
            wine = shutil.which("wine64") or ("/usr/lib/wine/wine64" if os.path.exists("/usr/lib/wine/wine64") else None)
            if not wine:
                return None
            return [wine, k], True
        return [k], False
    return None


def _host_path(pfad, wine):
    """Absolute path as the compiler sees it (Wine maps / to drive Z:)."""
    if not wine:
        return pfad
    return "Z:" + pfad.replace("/", "\\")


def uebersetze(dxc, wine, datei, makros, ziel):
    stage = _stage(datei)
    args = list(dxc) + [
        "-nologo", "-spirv", "-T", stage + "_6_0", "-E", "main",
        "-fspv-target-env=vulkan1.3", "-fvk-use-dx-layout", "-fvk-use-dx-position-w", "-HV", "2018",
        "-fvk-b-shift", "0", "all", "-fvk-t-shift", str(SHIFT_T), "all",
        "-fvk-s-shift", str(SHIFT_S), "all", "-fvk-u-shift", str(SHIFT_U), "all",
        "-I", ".",
    ]
    for name, wert in makros:
        args += ["-D", "%s=%s" % (name, wert)]
    args += ["-Fo", _host_path(ziel, wine), datei]
    env = dict(os.environ)
    if wine:
        env.setdefault("WINEDEBUG", "-all")
    r = subprocess.run(args, cwd=SHADER_DIR, capture_output=True, text=True, errors="replace", env=env)
    text = (r.stdout + r.stderr).strip()
    if r.returncode != 0 or not os.path.isfile(ziel):
        return False, text
    return True, text


def validiere(spirv_val, ziel):
    r = subprocess.run([spirv_val, "--target-env", "vulkan1.3", ziel], capture_output=True, text=True, errors="replace")
    return r.returncode == 0, (r.stdout + r.stderr).strip()


# SPIR-V opcodes and decorations used by the binding check.
_OP_TYPE_IMAGE, _OP_TYPE_SAMPLER, _OP_TYPE_SAMPLED_IMAGE, _OP_TYPE_ARRAY = 25, 26, 27, 28
_OP_TYPE_STRUCT, _OP_TYPE_POINTER, _OP_VARIABLE, _OP_DECORATE = 30, 32, 59, 71
_DEC_BINDING, _DEC_SET = 33, 34
_KLASSEN = (("b", 0, SHIFT_T), ("t", SHIFT_T, SHIFT_S), ("s", SHIFT_S, SHIFT_U))


def bindungen(pfad):
    """Liste von (Klasse, Satz, Bindung) der Ressourcen eines SPIR-V-Moduls; Klasse b, t, s oder ?."""
    with open(pfad, "rb") as f:
        data = f.read()
    words = [int.from_bytes(data[i:i + 4], "little") for i in range(0, len(data) - 3, 4)]
    if len(words) < 5 or words[0] != 0x07230203:
        raise ValueError("kein SPIR-V")
    typ, zeiger, variablen, binding, satz = {}, {}, [], {}, {}
    i = 5
    while i < len(words):
        op, n = words[i] & 0xFFFF, words[i] >> 16
        if n == 0:
            raise ValueError("kaputter Befehl")
        w = words[i:i + n]
        if op in (_OP_TYPE_IMAGE, _OP_TYPE_SAMPLED_IMAGE):
            typ[w[1]] = "t"
        elif op == _OP_TYPE_SAMPLER:
            typ[w[1]] = "s"
        elif op == _OP_TYPE_STRUCT:
            typ[w[1]] = "b"
        elif op == _OP_TYPE_ARRAY:
            typ[w[1]] = ("array", w[2])
        elif op == _OP_TYPE_POINTER:
            zeiger[w[1]] = w[3]
        elif op == _OP_VARIABLE:
            variablen.append((w[2], w[1]))
        elif op == _OP_DECORATE and len(w) >= 4:
            if w[2] == _DEC_BINDING:
                binding[w[1]] = w[3]
            elif w[2] == _DEC_SET:
                satz[w[1]] = w[3]
        i += n
    result = []
    for var, ptr in variablen:
        if var not in binding:
            continue
        t = typ.get(zeiger.get(ptr))
        while isinstance(t, tuple):
            t = typ.get(t[1])
        result.append((t or "?", satz.get(var, 0), binding[var]))
    return result


def pruefe_bindungen(pfad):
    """Fehlertext, wenn eine Ressource ausserhalb des Bereichs ihrer Klasse liegt, sonst None."""
    for klasse, satz, nummer in bindungen(pfad):
        bereich = [(lo, hi) for k, lo, hi in _KLASSEN if k == klasse]
        if not bereich or not (bereich[0][0] <= nummer < bereich[0][1]) or satz != 0:
            return "Ressource %s in Satz %d, Bindung %d ausserhalb ihres Bereichs" % (klasse, satz, nummer)
    return None


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--dxc", help="Pfad zu dxc bzw. dxc.exe")
    ap.add_argument("--out", help="Ordner fuer die SPIR-V-Dateien (sonst temporaer)")
    ap.add_argument("--liste", action="store_true", help="nur den Variantenkatalog ausgeben")
    ap.add_argument("--nur", help="nur Varianten, deren Name diesen Text enthaelt")
    ap.add_argument("-j", type=int, default=max(1, min(8, (os.cpu_count() or 2))), help="parallele Uebersetzungen")
    ap.add_argument("--ohne-val", action="store_true", help="spirv-val auslassen")
    ap.add_argument("--bindungen", action="store_true", help="belegte Bindungsnummern je Klasse ausgeben")
    a = ap.parse_args()

    varianten = katalog()
    if a.nur:
        varianten = [(d, m) for d, m in varianten if a.nur in variant_name(d, m)]
    if a.liste:
        for i, (d, m) in enumerate(varianten):
            print("%3d %s" % (i, variant_name(d, m)))
        print("%d Varianten aus %d Dateien" % (len(varianten), len({d for d, _ in varianten})))
        return 0

    found = finde_dxc(a.dxc)
    if not found:
        print("DXC nicht gefunden (--dxc, DXC, /opt/dxc-*/dxc.exe, dxc im Suchpfad)", file=sys.stderr)
        return 2
    dxc, wine = found
    spirv_val = None if a.ohne_val else shutil.which("spirv-val")
    if not a.ohne_val and not spirv_val:
        print("spirv-val nicht gefunden (Paket spirv-tools); --ohne-val laesst die Pruefung aus", file=sys.stderr)
        return 2

    out = a.out or tempfile.mkdtemp(prefix="spirv-")
    os.makedirs(out, exist_ok=True)
    start = time.time()

    def job(i_variante):
        i, (datei, makros) = i_variante
        ziel = os.path.join(out, "%03d_%s.spv" % (i, datei[:-5].replace(".", "_")))
        if os.path.exists(ziel):
            os.remove(ziel)
        ok, text = uebersetze(dxc, wine, datei, makros, ziel)
        if not ok:
            return i, "uebersetzt nicht", text
        if spirv_val:
            ok, vtext = validiere(spirv_val, ziel)
            if not ok:
                return i, "spirv-val", vtext
        btext = pruefe_bindungen(ziel)
        if btext:
            return i, "Bindungen", btext
        belegt[i] = bindungen(ziel)
        warnung = "warning:" in text
        return i, None, text if warnung else ""

    fehler = []
    warnungen = {}
    belegt = {}
    with concurrent.futures.ThreadPoolExecutor(max_workers=a.j) as pool:
        for i, art, text in pool.map(job, enumerate(varianten)):
            datei, makros = varianten[i]
            if art:
                fehler.append((variant_name(datei, makros), art, text))
            elif text:
                for zeile in text.splitlines():
                    if "warning:" in zeile:
                        key = zeile.split(": warning:", 1)[-1].strip()
                        ort = zeile.split(": warning:", 1)[0]
                        warnungen.setdefault((ort, key), 0)
                        warnungen[(ort, key)] += 1

    dauer = time.time() - start
    if a.out:
        with open(os.path.join(out, "katalog.txt"), "w", encoding="ascii", errors="replace") as f:
            for i, (d, m) in enumerate(varianten):
                f.write("%03d %s\n" % (i, variant_name(d, m)))
    else:
        shutil.rmtree(out, ignore_errors=True)

    for (ort, text), n in sorted(warnungen.items()):
        print("   Warnung (%dx) %s: %s" % (n, ort, text))
    for name, art, text in fehler:
        print("   FEHLER %s (%s)" % (name, art))
        for zeile in text.splitlines()[:6]:
            print("      " + zeile)
    if a.bindungen:
        for klasse, name in (("b", "Konstantenpuffer"), ("t", "Texturen"), ("s", "Sampler")):
            nummern = sorted({nr for res in belegt.values() for k, _, nr in res if k == klasse})
            print("   %s: %s" % (name, " ".join(str(n) for n in nummern) or "-"))
    dateien = len({d for d, _ in varianten})
    if fehler:
        print("%d von %d Varianten (%d Dateien) nicht in Ordnung, %.0f s" % (len(fehler), len(varianten), dateien, dauer))
        return 1
    print("%d Varianten aus %d Dateien nach SPIR-V (Vulkan 1.3)%s, %d Warnungsstellen, %.0f s" % (
        len(varianten), dateien, ", spirv-val ohne Befund" if spirv_val else "", len(warnungen), dauer))
    print("Bindungen je Klasse in ihrem Bereich (b ab 0, t ab %d, s ab %d, Satz 0)" % (SHIFT_T, SHIFT_S))
    return 0


if __name__ == "__main__":
    sys.exit(main())
