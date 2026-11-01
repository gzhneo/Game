# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
# W3D-Gerüst der Naturmodelle aus eigener Vorschrift: schreibt eine W3D-Datei
# vollständig neu aus eigener Geometrie (Ecken, Normalen, Dreiecke, UV) und legt jeden übrigen Wert nach seiner
# Bedeutung im Engine-Quelltext fest (Code/Renderer/WW3D2: w3d_file.h, meshmdlio.cpp, meshgeometry.cpp,
# w3d_util.cpp, htree.cpp, hlod.cpp, texture.cpp). Aus EA-Dateien wird nichts gelesen; einzige Schnittstelle zu EAs
# Daten sind die Namen, die die INIs erwarten (Modellname = Netz- bzw. Containername, Knochen AFLAME01).
#
#   python w3d_geruest.py zeigen <datei.w3d>            alle Werte der Datei mit der Regel, nach der sie gesetzt sind
#
# Als Modul: geometrie(daten) liest nur die Geometrie einer W3D-Datei; requisit(...) und baum(...) schreiben neu;
# soll(...) prüft eine Datei gegen den Neubau aus ihrer eigenen Geometrie (natur_bauen.py pruefen).
import math
import struct
import sys

# ------------------------------------------------------------------------------------------------
# Abschnittskennungen (w3d_file.h, enum W3D_CHUNK_*)
# ------------------------------------------------------------------------------------------------
MESH, VERTICES, VERTEX_NORMALS, MESH_HEADER3, TRIANGLES, VERTEX_SHADE_INDICES = 0x0000, 0x0002, 0x0003, 0x001F, 0x0020, 0x0022
MATERIAL_INFO, SHADERS, VERTEX_MATERIALS, VERTEX_MATERIAL, VERTEX_MATERIAL_NAME, VERTEX_MATERIAL_INFO = \
    0x0028, 0x0029, 0x002A, 0x002B, 0x002C, 0x002D
TEXTURES, TEXTURE, TEXTURE_NAME, MATERIAL_PASS, VERTEX_MATERIAL_IDS, SHADER_IDS = 0x0030, 0x0031, 0x0032, 0x0038, 0x0039, 0x003A
TEXTURE_STAGE, TEXTURE_IDS, STAGE_TEXCOORDS = 0x0048, 0x0049, 0x004A
HIERARCHY, HIERARCHY_HEADER, PIVOTS = 0x0100, 0x0101, 0x0102
HLOD, HLOD_HEADER, HLOD_LOD_ARRAY, HLOD_SUB_OBJECT_ARRAY_HEADER, HLOD_SUB_OBJECT = 0x0700, 0x0701, 0x0702, 0x0703, 0x0704
UNTER = {MESH, VERTEX_MATERIALS, VERTEX_MATERIAL, TEXTURES, TEXTURE, MATERIAL_PASS, TEXTURE_STAGE, HIERARCHY, HLOD,
         HLOD_LOD_ARRAY}
NAME_LEN = 16                                         # W3D_NAME_LEN


def version(haupt, neben):
    return (haupt << 16) | neben                      # W3D_MAKE_VERSION


# ------------------------------------------------------------------------------------------------
# Vorschrift: jeder Wert mit Grund
# ------------------------------------------------------------------------------------------------
# Netzkopf (W3dMeshHeader3Struct)
MESH_VERSION = version(4, 2)          # W3D_CURRENT_MESH_VERSION; ab 4.1 liest meshmdlio.cpp den Geometrietyp aus den Attributen
MESH_FLAG_NONE = 0x00000000           # Geometrietyp NORMAL (0): kein Skin, nicht zur Kamera ausgerichtet; nicht verborgen;
                                      # keine Kollisionsbits (Kollision kommt im Spiel aus der INI-Geometrie, nicht aus dem
                                      # Netz); kein PRELIT_* (Licht rechnet die Engine je Ecke aus den Normalen)
MESH_FLAG_TWO_SIDED = 0x00002000      # W3D_MESH_FLAG_TWO_SIDED -> MeshGeometryClass::TWO_SIDED: ohne Rückseitenkappung.
                                      # Nur der Baum: seine Karten und Astschichten sind einfache Flächen, die man von
                                      # beiden Seiten sieht. Felsen und Fass sind geschlossene Körper, die Kappung spart
                                      # dort die Hälfte der Pixel.
# W3D_MESH_FLAG_CAST_SHADOW (0x8000) bleibt aus: W3DVolumetricShadow fragt es nur bei Netzen mit Alpha oder
# Durchsichtigkeit ab. Deckende Netze werfen ihr Schattenvolumen ohnehin (wenn die INI eines verlangt); beim Baum
# ergäben die Alphatest-Karten rechteckige Schattenvolumen -- sein Schatten kommt aus dem Baum-Puffer (DoShadow) bzw.
# der Shadow Map.
SORT_LEVEL = 0                        # SORT_LEVEL_NONE: deckend bzw. Alphatest schreibt Tiefe, braucht keine Sortierung
PRELIT_VERSION = 0                    # kein Lightmap-Werkzeug beteiligt
VERTEX_CHANNELS = 0x1 | 0x2           # W3D_VERTEX_CHANNEL_LOCATION | _NORMAL; UV stehen im Materialpass
FACE_CHANNELS = 0x1                   # W3D_FACE_CHANNEL_FACE (W3dTriStruct)
SURFACE_TYPE_DEFAULT = 13             # W3D_SURFACE_TYPES: landet nur in Kollisionsergebnissen (aabtree.cpp,
                                      # meshgeometry.cpp), das Spiel liest es nirgends; DEFAULT ist der neutrale Wert
                                      # (so meldet auch BaseHeightMap das Gelände)

# Vertexmaterial (W3dVertexMaterialStruct): Werte von W3d_Vertex_Material_Reset, der Grundeinstellung der Engine
VM_ATTRIBUTE = 0x00000000             # W3DVERTMAT_STAGE0_MAPPING_UV (Textur nach UV), keine Tiefenabdunklung, kein
                                      # ARGB_EMISSIVE_ONLY, kein COPY_SPECULAR_TO_DIFFUSE
VM_AMBIENT = (255, 255, 255)          # Umgebungslicht wirkt voll: die Textur trägt die Farbe
VM_DIFFUSE = (255, 255, 255)          # gerichtetes Licht wirkt voll (Textur x Licht)
VM_SPECULAR = (0, 0, 0)               # matte Naturflächen (Stein, Rinde, Nadeln, alter Lack): kein Glanzlicht
VM_EMISSIVE = (0, 0, 0)               # leuchtet nicht selbst
VM_SHININESS = 1.0                    # Vorgabe der Engine; ohne Glanzfarbe ohne Wirkung (ffp.vs.hlsl: specular x m_specular)
VM_OPACITY = 1.0                      # deckend; Durchsicht beim Baum allein über den Alphatest
VM_TRANSLUCENCY = 0.0                 # kein Durchscheinen

# Shader (W3dShaderStruct, 16 Byte): W3d_Shader_Reset, dazu Textur an; der Baum zusätzlich mit Alphatest.
# Veraltete Felder (ColorMask, FogFunc, ShaderPreset) und das Füllbyte liest W3dUtilityClass::Convert_Shader nicht --
# sie bleiben 0 wie in W3d_Shader_Reset. PostDetail* setzt Convert_Shader aus den Detail-Feldern, die Datei-Werte
# werden ignoriert; 0 = DISABLE.
DEPTHCOMPARE_PASS_LEQUAL = 3          # Tiefentest wie überall in der Szene
DEPTHMASK_WRITE_ENABLE = 1            # schreibt Tiefe (deckend bzw. Alphatest)
DESTBLENDFUNC_ZERO = 0                # kein Überblenden: Ziel wird ersetzt
PRIGRADIENT_MODULATE = 1              # Textur x Licht je Ecke
SECGRADIENT_DISABLE = 0               # kein Zusatzglanz
SRCBLENDFUNC_ONE = 1                  # Quelle unverändert
TEXTURING_ENABLE = 1                  # Textur an
DETAILFUNC_DISABLE = 0                # keine zweite Texturstufe
ALPHATEST_DISABLE, ALPHATEST_ENABLE = 0, 1   # Baum: Karten mit hartem Alpha (Bild schneidet die Kontur)


def shader(alphatest):
    return bytes([DEPTHCOMPARE_PASS_LEQUAL, DEPTHMASK_WRITE_ENABLE, 0, DESTBLENDFUNC_ZERO, 0, PRIGRADIENT_MODULATE,
                  SECGRADIENT_DISABLE, SRCBLENDFUNC_ONE, TEXTURING_ENABLE, DETAILFUNC_DISABLE, DETAILFUNC_DISABLE, 0,
                  ALPHATEST_ENABLE if alphatest else ALPHATEST_DISABLE, DETAILFUNC_DISABLE, DETAILFUNC_DISABLE, 0])


# Textur: kein W3D_CHUNK_TEXTURE_INFO -- ohne ihn lädt texture.cpp (Load_Texture) mit allen Mip-Stufen als Farbtextur,
# genau das gewünschte Verhalten; die DDS bringt ihre Mip-Kette selbst mit.

# Hierarchie und HLOD (nur der Baum; Generals zeichnet ihn über W3DModelDraw und braucht den Knochen AFLAME01)
HTREE_VERSION = version(4, 1)         # W3D_CURRENT_HTREE_VERSION (kein Vor-3.0-Wurzelausgleich in htree.cpp)
HLOD_VERSION = version(1, 0)          # W3D_CURRENT_HLOD_VERSION
WURZEL = 'ROOTTRANSFORM'              # Name, den htree.cpp selbst der Wurzel gibt; Wurzel = Fußpunkt des Baums
NO_MAX_SCREEN_SIZE = 3.4028234663852886e38   # WWMATH_FLOAT_MAX: eine einzige LOD-Stufe, immer die höchste
BAUM_NETZ = 'BAUM'                    # eigener Netzname: W3DTreeBuffer nimmt das erste Unterobjekt, W3DModelDraw
                                      # spricht das Netz nicht mit Namen an
FEUER_KNOCHEN = 'AFLAME01'            # Schnittstelle: ParticleSysBone = AFlame01 ... in den INIs (Feuer, Glut)
ASTSCHICHT_NZ = 0.5                   # Dreiecke mit |n_z| > 0,5 zählen als waagrechte Astschicht (Feuerpunkt)


def name16(s):
    b = s.upper().encode('ascii')
    if len(b) >= NAME_LEN:
        raise ValueError('Name zu lang für W3D (max. 15 Zeichen): %s' % s)
    return b + b'\0' * (NAME_LEN - len(b))


# ------------------------------------------------------------------------------------------------
# Lesen (nur Geometrie)
# ------------------------------------------------------------------------------------------------
def abschnitte(daten):
    p = 0
    while p + 8 <= len(daten):
        typ, groesse = struct.unpack('<II', daten[p:p + 8])
        n = groesse & 0x7FFFFFFF
        yield typ, daten[p + 8:p + 8 + n]
        p += 8 + n


def text(b):
    return b.split(b'\0', 1)[0].decode('latin-1')


def geometrie(daten):
    """Je Netz {name, ecken, normalen, dreiecke, uv, textur} in Modellkoordinaten (Netz-Ecken plus Versatz des Knochens,
    an dem das Netz hängt). Alles andere der Datei wird übergangen."""
    knochen, unter = {}, {}
    for typ, b in abschnitte(daten):
        if typ == HIERARCHY:
            for t2, x in abschnitte(b):
                if t2 == PIVOTS:
                    for i in range(len(x) // 60):
                        s = x[60 * i:60 * i + 60]
                        knochen[i] = struct.unpack('<3f', s[20:32])
        elif typ == HLOD:
            for t2, x in abschnitte(b):
                if t2 == HLOD_LOD_ARRAY:
                    for t3, y in abschnitte(x):
                        if t3 == HLOD_SUB_OBJECT:
                            unter[text(y[4:36]).split('.')[-1]] = struct.unpack('<I', y[:4])[0]
    out = []
    for typ, b in abschnitte(daten):
        if typ != MESH:
            continue
        g = {'textur': None, 'uv': []}
        for t2, x in abschnitte(b):
            if t2 == MESH_HEADER3:
                g['name'] = text(x[8:24])
            elif t2 == VERTICES:
                g['ecken'] = [struct.unpack('<3f', x[12 * i:12 * i + 12]) for i in range(len(x) // 12)]
            elif t2 == VERTEX_NORMALS:
                g['normalen'] = [struct.unpack('<3f', x[12 * i:12 * i + 12]) for i in range(len(x) // 12)]
            elif t2 == TRIANGLES:
                g['dreiecke'] = [struct.unpack('<3I', x[32 * i:32 * i + 12]) for i in range(len(x) // 32)]
            elif t2 == TEXTURES:
                for _, tx in abschnitte(x):
                    for t3, y in abschnitte(tx):
                        if t3 == TEXTURE_NAME:
                            g['textur'] = text(y)
            elif t2 == MATERIAL_PASS:
                for t3, y in abschnitte(x):
                    if t3 == TEXTURE_STAGE:
                        for t4, z in abschnitte(y):
                            if t4 == STAGE_TEXCOORDS:
                                g['uv'] = [struct.unpack('<2f', z[8 * i:8 * i + 8]) for i in range(len(z) // 8)]
        v = knochen.get(unter.get(g['name']), (0.0, 0.0, 0.0))
        g['ecken'] = [(p[0] + v[0], p[1] + v[1], p[2] + v[2]) for p in g['ecken']]
        out.append(g)
    return out


# ------------------------------------------------------------------------------------------------
# Rechnen
# ------------------------------------------------------------------------------------------------
def _sub(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def _kreuz(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def _punkt(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def ebene(p0, p1, p2):
    """Flächennormale (Umlaufsinn der Ecken) und Abstand: meshgeometry.cpp liest Normal und Dist als Ebene n.x = Dist."""
    n = _kreuz(_sub(p1, p0), _sub(p2, p0))
    flaeche2 = math.sqrt(_punkt(n, n))
    n = tuple(c / flaeche2 for c in n) if flaeche2 > 0 else (0.0, 0.0, 1.0)
    return n, _punkt(n, p0), flaeche2 / 2


def huelle(ecken):
    """Achsparallele Box und Kugel: Mitte der Box, Halbmesser = größter Abstand einer Ecke (enthält alle Ecken; die
    Engine nimmt beides aus dem Kopf für Sichtbarkeit und Auswahl)."""
    mn = tuple(min(p[i] for p in ecken) for i in range(3))
    mx = tuple(max(p[i] for p in ecken) for i in range(3))
    m = tuple((mn[i] + mx[i]) / 2 for i in range(3))
    r = max(math.sqrt(_punkt(_sub(p, m), _sub(p, m))) for p in ecken)
    return mn, mx, m, r


def feuerpunkt(ecken, dreiecke):
    """Feuerpunkt des Baums: flächengewichteter Schwerpunkt der waagrechten Astschichten (|n_z| > ASTSCHICHT_NZ) --
    dort sitzt die Masse der Krone, die brennt. Senkrechte Umrisskarten und Stamm zählen nicht."""
    s, w = [0.0, 0.0, 0.0], 0.0
    for a, b, c in dreiecke:
        n, _, f = ebene(ecken[a], ecken[b], ecken[c])
        if abs(n[2]) > ASTSCHICHT_NZ:
            for i in range(3):
                s[i] += f * (ecken[a][i] + ecken[b][i] + ecken[c][i]) / 3
            w += f
    if w == 0:
        raise ValueError('Baum ohne waagrechte Astschichten -- Feuerpunkt nicht bestimmbar')
    return tuple(c / w for c in s)


# ------------------------------------------------------------------------------------------------
# Schreiben
# ------------------------------------------------------------------------------------------------
def abschnitt(typ, inhalt):
    if isinstance(inhalt, list):
        inhalt = b''.join(inhalt)
        return struct.pack('<II', typ, len(inhalt) | 0x80000000) + inhalt
    return struct.pack('<II', typ, len(inhalt)) + inhalt


def netz(name, container, g, beidseitig, alphatest):
    ecken, normalen, dreiecke, uv = g['ecken'], g['normalen'], g['dreiecke'], g['uv']
    if not (len(ecken) == len(normalen) == len(uv)):
        raise ValueError('%s: %d Ecken, %d Normalen, %d UV' % (name, len(ecken), len(normalen), len(uv)))
    mn, mx, m, r = huelle(ecken)
    kopf = struct.pack('<II', MESH_VERSION, MESH_FLAG_TWO_SIDED if beidseitig else MESH_FLAG_NONE)
    kopf += name16(name) + (name16(container) if container else b'\0' * NAME_LEN)
    kopf += struct.pack('<IIIIiII', len(dreiecke), len(ecken), 1, 0, SORT_LEVEL, PRELIT_VERSION, 0)
    kopf += struct.pack('<II', VERTEX_CHANNELS, FACE_CHANNELS)
    kopf += struct.pack('<3f3f3ff', *mn, *mx, *m, r)
    dr = b''
    for a, b, c in dreiecke:
        n, d, _ = ebene(ecken[a], ecken[b], ecken[c])
        dr += struct.pack('<3II3ff', a, b, c, SURFACE_TYPE_DEFAULT, *n, d)
    vm_info = struct.pack('<I', VM_ATTRIBUTE) + b''.join(bytes(list(f) + [0]) for f in
                                                          (VM_AMBIENT, VM_DIFFUSE, VM_SPECULAR, VM_EMISSIVE))
    vm_info += struct.pack('<3f', VM_SHININESS, VM_OPACITY, VM_TRANSLUCENCY)
    # Vertexmaterial heißt wie die Textur (ohne Endung); der Name hat in der Engine keine Wirkung
    vm_name = g['textur'].rsplit('.', 1)[0].encode('ascii') + b'\0'
    teile = [
        abschnitt(MESH_HEADER3, kopf),
        abschnitt(VERTICES, b''.join(struct.pack('<3f', *p) for p in ecken)),
        abschnitt(VERTEX_NORMALS, b''.join(struct.pack('<3f', *n) for n in normalen)),
        abschnitt(TRIANGLES, dr),
        # jede Ecke wird mit ihrer eigenen Normale beleuchtet (die Normalen an Nähten sind schon gemittelt)
        abschnitt(VERTEX_SHADE_INDICES, struct.pack('<%dI' % len(ecken), *range(len(ecken)))),
        abschnitt(MATERIAL_INFO, struct.pack('<IIII', 1, 1, 1, 1)),   # ein Pass, ein Material, ein Shader, eine Textur
        abschnitt(VERTEX_MATERIALS, [abschnitt(VERTEX_MATERIAL, [abschnitt(VERTEX_MATERIAL_NAME, vm_name),
                                                                 abschnitt(VERTEX_MATERIAL_INFO, vm_info)])]),
        abschnitt(SHADERS, shader(alphatest)),
        abschnitt(TEXTURES, [abschnitt(TEXTURE, [abschnitt(TEXTURE_NAME, g['textur'].encode('ascii') + b'\0')])]),
        # Pass 0: Material 0, Shader 0, Stufe 0 mit Textur 0 und einer UV je Ecke (Index 0 = für alle Ecken gleich)
        abschnitt(MATERIAL_PASS, [abschnitt(VERTEX_MATERIAL_IDS, struct.pack('<I', 0)),
                                  abschnitt(SHADER_IDS, struct.pack('<I', 0)),
                                  abschnitt(TEXTURE_STAGE, [abschnitt(TEXTURE_IDS, struct.pack('<I', 0)),
                                                            abschnitt(STAGE_TEXCOORDS,
                                                                      b''.join(struct.pack('<2f', *t) for t in uv))])]),
    ]
    return abschnitt(MESH, teile)


def pivot(name, eltern, lage):
    # Euler-Winkel liest htree.cpp nicht, die Drehung kommt aus dem Quaternion: keine Drehung (0, 0, 0, 1)
    return name16(name) + struct.pack('<i3f3f4f', eltern, *lage, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 1.0)


def f32(werte):
    """Auf float32 runden, wie sie in der Datei stehen: abgeleitete Werte (Hülle, Ebenen, Knochen) rechnen mit genau
    diesen Zahlen, damit ein Neubau aus der Datei dieselben Bytes ergibt."""
    return [struct.unpack('<%df' % len(w), struct.pack('<%df' % len(w), *w)) for w in werte]


def _geo(g, textur):
    return dict(g, textur=textur, ecken=f32(g['ecken']), normalen=f32(g['normalen']), uv=f32(g['uv']),
                dreiecke=[tuple(int(i) for i in d) for d in g['dreiecke']])


def requisit(modell, g, textur):
    """Fels oder Fass: ein Netz unter dem Modellnamen, ohne Hierarchie (W3DModelDraw findet das Modell über den
    Netznamen), deckend, einseitig."""
    g = _geo(g, textur)
    return netz(modell, '', g, beidseitig=False, alphatest=False)


def baum(modell, g, textur):
    """Baum: Hierarchie und HLOD unter dem Modellnamen mit einem Netz (W3DTreeBuffer verlangt genau ein einfaches Netz
    als Unterobjekt 0 und übernimmt dessen Knochenversatz) und dem Feuerknochen. Netz und Wurzel liegen im Fußpunkt
    (0, 0, 0): die Ecken stehen in Modellkoordinaten, der Versatz ist 0."""
    g = _geo(g, textur)
    f = feuerpunkt(g['ecken'], g['dreiecke'])
    knochen = [(WURZEL, -1, (0.0, 0.0, 0.0)), (BAUM_NETZ, 0, (0.0, 0.0, 0.0)), (FEUER_KNOCHEN, 0, f)]
    hier = abschnitt(HIERARCHY, [
        abschnitt(HIERARCHY_HEADER, struct.pack('<I', HTREE_VERSION) + name16(modell) +
                  struct.pack('<I3f', len(knochen), 0.0, 0.0, 0.0)),
        abschnitt(PIVOTS, b''.join(pivot(*k) for k in knochen)),
    ])
    voll = ('%s.%s' % (modell, BAUM_NETZ)).upper().encode('ascii')
    hlod = abschnitt(HLOD, [
        abschnitt(HLOD_HEADER, struct.pack('<II', HLOD_VERSION, 1) + name16(modell) + name16(modell)),
        abschnitt(HLOD_LOD_ARRAY, [abschnitt(HLOD_SUB_OBJECT_ARRAY_HEADER, struct.pack('<If', 1, NO_MAX_SCREEN_SIZE)),
                                   abschnitt(HLOD_SUB_OBJECT, struct.pack('<I', 1) + voll +
                                             b'\0' * (2 * NAME_LEN - len(voll)))]),
    ])
    return hier + netz(BAUM_NETZ, modell, g, beidseitig=True, alphatest=True) + hlod


def neu_bauen(daten, modell, art):
    """Neubau aus der Geometrie einer vorhandenen Datei (Textur behält ihren Namen)."""
    gs = geometrie(daten)
    if len(gs) != 1:
        raise ValueError('%s: %d Netze, erwartet 1' % (modell, len(gs)))
    return (baum if art == 'baum' else requisit)(modell, gs[0], gs[0]['textur'])


def soll(daten, modell, art, textur):
    """Prüft eine Datei gegen die Vorschrift: sie muss bytegleich dem Neubau aus ihrer eigenen Geometrie sein (Namen,
    Kopf, Shader, Material, Knochen und Hülle nach Regel) und die erwartete Textur nennen. Liste der Abweichungen."""
    fehler = []
    gs = geometrie(daten)
    if len(gs) != 1:
        return ['%d Netze statt 1' % len(gs)]
    if gs[0]['textur'] != textur:
        fehler.append('Textur %s statt %s' % (gs[0]['textur'], textur))
    erwartet = neu_bauen(daten, modell, art)
    if erwartet != daten:
        a, b = zeilen(daten), zeilen(erwartet)
        for x, y in zip(a, b):
            if x != y:
                fehler.append('ist %s | soll %s' % (x, y))
        if len(a) != len(b):
            fehler.append('%d Zeilen statt %d' % (len(a), len(b)))
        if not fehler:
            fehler.append('Bytes weichen ab (Geometrie oder Reihenfolge)')
    return fehler


def zeilen(daten):
    """Lesbare Fassung aller Gerüstwerte (ohne die Geometrie selbst)."""
    z = []

    def netz_z(b):
        for t2, x in abschnitte(b):
            if t2 == MESH_HEADER3:
                v, a = struct.unpack('<II', x[:8])
                z.append('Netz %s Container %r Version %X Attribute 0x%08X' % (text(x[8:24]), text(x[24:40]), v, a))
                z.append('  Zahlen %s Kanaele %s' % (struct.unpack('<IIIIiII', x[40:68]), struct.unpack('<II', x[68:76])))
                z.append('  Huelle %s' % ' '.join('%.4f' % c for c in struct.unpack('<10f', x[76:116])))
            elif t2 == TRIANGLES:
                z.append('  Dreiecke %d Oberflaechen %s' % (len(x) // 32, sorted({struct.unpack('<I', x[32 * i + 12:32 * i + 16])[0]
                                                                                for i in range(len(x) // 32)})))
            elif t2 == VERTEX_SHADE_INDICES:
                idx = struct.unpack('<%dI' % (len(x) // 4), x)
                z.append('  Schattierung %s' % ('je Ecke' if list(idx) == list(range(len(idx))) else 'geteilt'))
            elif t2 == MATERIAL_INFO:
                z.append('  Materialinfo %s' % (struct.unpack('<IIII', x),))
            elif t2 == VERTEX_MATERIALS:
                for _, vm in abschnitte(x):
                    for t3, y in abschnitte(vm):
                        if t3 == VERTEX_MATERIAL_NAME:
                            z.append('  Vertexmaterial %s' % text(y))
                        elif t3 == VERTEX_MATERIAL_INFO:
                            z.append('    Attribute 0x%08X Farben %s Glanz %.3f Deckung %.3f Durchscheinen %.3f'
                                     % ((struct.unpack('<I', y[:4])[0], y[4:20].hex()) + struct.unpack('<3f', y[20:32])))
                        else:
                            z.append('    Abschnitt 0x%04X' % t3)
            elif t2 == SHADERS:
                z.append('  Shader %s' % x.hex())
            elif t2 == TEXTURES:
                for _, tx in abschnitte(x):
                    for t3, y in abschnitte(tx):
                        z.append('  Textur %s' % text(y) if t3 == TEXTURE_NAME else '  Texturabschnitt 0x%04X' % t3)
            elif t2 in (VERTICES, VERTEX_NORMALS, MATERIAL_PASS):
                z.append('  Abschnitt 0x%04X %d Bytes' % (t2, len(x)))
            else:
                z.append('  fremder Abschnitt 0x%04X' % t2)

    for typ, b in abschnitte(daten):
        if typ == MESH:
            netz_z(b)
        elif typ == HIERARCHY:
            for t2, x in abschnitte(b):
                if t2 == HIERARCHY_HEADER:
                    z.append('Hierarchie %s Version %X Knoten %d Mitte %s' % (
                        text(x[4:20]), struct.unpack('<I', x[:4])[0], struct.unpack('<I', x[20:24])[0],
                        struct.unpack('<3f', x[24:36])))
                elif t2 == PIVOTS:
                    for i in range(len(x) // 60):
                        s = x[60 * i:60 * i + 60]
                        z.append('  Knochen %-14s Eltern %2d Lage %s Euler %s Drehung %s' % (
                            text(s[:16]), struct.unpack('<i', s[16:20])[0],
                            ' '.join('%.4f' % c for c in struct.unpack('<3f', s[20:32])),
                            ' '.join('%.1f' % c for c in struct.unpack('<3f', s[32:44])),
                            ' '.join('%.1f' % c for c in struct.unpack('<4f', s[44:60]))))
                else:
                    z.append('  fremder Abschnitt 0x%04X' % t2)
        elif typ == HLOD:
            for t2, x in abschnitte(b):
                if t2 == HLOD_HEADER:
                    z.append('HLOD %s Hierarchie %s Version %X Stufen %d' % (
                        text(x[8:24]), text(x[24:40]), *struct.unpack('<II', x[:8])))
                elif t2 == HLOD_LOD_ARRAY:
                    for t3, y in abschnitte(x):
                        if t3 == HLOD_SUB_OBJECT_ARRAY_HEADER:
                            z.append('  Stufe Modelle %d bis Bildgroesse %g' % struct.unpack('<If', y))
                        elif t3 == HLOD_SUB_OBJECT:
                            z.append('  Unterobjekt Knochen %d %s' % (struct.unpack('<I', y[:4])[0], text(y[4:36])))
                else:
                    z.append('  fremder Abschnitt 0x%04X' % t2)
        else:
            z.append('fremder Abschnitt 0x%04X' % typ)
    return z


if __name__ == '__main__':
    if len(sys.argv) == 3 and sys.argv[1] == 'zeigen':
        print('\n'.join(zeilen(open(sys.argv[2], 'rb').read())))
    else:
        sys.exit(__doc__ or 'Aufruf: python w3d_geruest.py zeigen <datei.w3d>')
