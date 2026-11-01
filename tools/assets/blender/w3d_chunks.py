# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
# W3D-Datei ohne Blender auf Abschnittsebene lesen: Abschnittsbaum, Netz-Kopf
# (Netz- und Containername, Attribute, Dreiecke/Ecken), Texturnamen, Shader, Hierarchie- und HLOD-Namen.
#
#   python w3d_chunks.py <datei.w3d> [--baum]          Zusammenfassung (mit --baum alle Abschnitte)
#   python w3d_chunks.py --vergleich <a.w3d> <b.w3d>    Namen, Attribute und Shader zweier Dateien nebeneinander
# Als Modul (objekt_ersetzen.py): normalen_ersetzen, normalen_glaetten -- Geometrie-Nachbearbeitung des Add-on-Exports.
# Das Gerüst der Naturmodelle schreibt w3d_geruest.py; der Vergleich mit EA-Modellen steht in
# natur_messen.py.
import struct
import sys

NAMEN = {
    0x0000: 'MESH', 0x0002: 'VERTICES', 0x0003: 'VERTEX_NORMALS', 0x000C: 'MESH_USER_TEXT', 0x000E: 'VERTEX_INFLUENCES',
    0x001F: 'MESH_HEADER3', 0x0020: 'TRIANGLES', 0x0022: 'VERTEX_SHADE_INDICES', 0x0028: 'MATERIAL_INFO',
    0x0029: 'SHADERS', 0x002A: 'VERTEX_MATERIALS', 0x002B: 'VERTEX_MATERIAL', 0x002C: 'VERTEX_MATERIAL_NAME',
    0x002D: 'VERTEX_MATERIAL_INFO', 0x002E: 'VERTEX_MAPPER_ARGS0', 0x002F: 'VERTEX_MAPPER_ARGS1', 0x0030: 'TEXTURES',
    0x0031: 'TEXTURE', 0x0032: 'TEXTURE_NAME', 0x0033: 'TEXTURE_INFO', 0x0038: 'MATERIAL_PASS', 0x0039: 'VERTEX_MATERIAL_IDS',
    0x003A: 'SHADER_IDS', 0x003B: 'DCG', 0x003C: 'DIG', 0x003E: 'SCG', 0x0048: 'TEXTURE_STAGE', 0x0049: 'TEXTURE_IDS',
    0x004A: 'STAGE_TEXCOORDS', 0x004B: 'PER_FACE_TEXCOORD_IDS', 0x0058: 'DEFORM', 0x0080: 'PS2_SHADERS',
    0x0090: 'AABBTREE', 0x0091: 'AABBTREE_HEADER', 0x0092: 'AABBTREE_POLYINDICES', 0x0093: 'AABBTREE_NODES',
    0x0100: 'HIERARCHY', 0x0101: 'HIERARCHY_HEADER', 0x0102: 'PIVOTS', 0x0103: 'PIVOT_FIXUPS',
    0x0700: 'HLOD', 0x0701: 'HLOD_HEADER', 0x0702: 'HLOD_LOD_ARRAY', 0x0703: 'HLOD_SUB_OBJECT_ARRAY_HEADER',
    0x0704: 'HLOD_SUB_OBJECT', 0x0705: 'HLOD_AGGREGATE_ARRAY', 0x0706: 'HLOD_PROXY_ARRAY',
    0x0740: 'BOX', 0x0900: 'DAZZLE',
}
CONTAINER = {0x0000, 0x002A, 0x002B, 0x0030, 0x0031, 0x0038, 0x0048, 0x0090, 0x0100, 0x0700, 0x0702,
             0x0705, 0x0706}


def abschnitte(daten, p=0, ende=None):
    ende = len(daten) if ende is None else ende
    while p + 8 <= ende:
        typ, groesse = struct.unpack('<II', daten[p:p + 8])
        inhalt = groesse & 0x7FFFFFFF
        yield typ, daten[p + 8:p + 8 + inhalt], bool(groesse & 0x80000000)
        p += 8 + inhalt


def text(b):
    return b.split(b'\0', 1)[0].decode('latin-1')


def baum(daten, tiefe=0, aus=None):
    aus = [] if aus is None else aus
    for typ, inhalt, sub in abschnitte(daten):
        aus.append('%s%s (0x%04X) %d' % ('  ' * tiefe, NAMEN.get(typ, '?'), typ, len(inhalt)))
        if sub or typ in CONTAINER:
            baum(inhalt, tiefe + 1, aus)
    return aus


def zusammenfassung(daten):
    """Liste von Zeilen, die für den Vergleich zählen (ohne Geometriewerte)."""
    z = []

    def netz(inhalt):
        for typ, b, _ in abschnitte(inhalt):
            if typ == 0x001F:
                version, attr = struct.unpack('<II', b[:8])
                name, cont = text(b[8:24]), text(b[24:40])
                ndr, neck, nmat, ndam, sort, prelit, fvec = struct.unpack('<IIIIiII', b[40:68])
                z.append('NETZ %s Container %r Attribute 0x%08X Sortierung %d Prelit 0x%X Vertexfunktion 0x%X'
                         % (name, cont, attr, sort, prelit, fvec))
                z.append('  Dreiecke %d Ecken %d Materialien %d' % (ndr, neck, nmat))
            elif typ == 0x0028:
                z.append('  MATERIAL_INFO Paesse %d VM %d Shader %d Texturen %d' % struct.unpack('<IIII', b[:16]))
            elif typ == 0x0029:
                for i in range(len(b) // 16):
                    z.append('  SHADER %s' % b[16 * i:16 * i + 16].hex())
            elif typ == 0x002A:
                for t2, vm, _ in abschnitte(b):
                    for t3, x, _ in abschnitte(vm):
                        if t3 == 0x002C:
                            z.append('  VERTEXMATERIAL %s' % text(x))
                        elif t3 == 0x002D:
                            attr = struct.unpack('<I', x[:4])[0]
                            z.append('    Attribute 0x%08X Farben %s' % (attr, x[4:20].hex()))
            elif typ == 0x0030:
                for t2, tex, _ in abschnitte(b):
                    for t3, x, _ in abschnitte(tex):
                        if t3 == 0x0032:
                            z.append('  TEXTUR %s' % text(x))
            elif typ == 0x0003:
                pass
            elif typ in (0x0038,):
                teile = [NAMEN.get(t2, hex(t2)) for t2, _, _ in abschnitte(b)]
                z.append('  PASS %s' % ' '.join(teile))

    for typ, inhalt, _ in abschnitte(daten):
        if typ == 0x0000:
            netz(inhalt)
        elif typ == 0x0100:
            for t2, b, _ in abschnitte(inhalt):
                if t2 == 0x0101:
                    z.append('HIERARCHIE %s Knoten %d' % (text(b[4:20]), struct.unpack('<I', b[20:24])[0]))
                elif t2 == 0x0102:
                    for i in range(len(b) // 60):
                        s = b[60 * i:60 * i + 60]
                        z.append('  KNOCHEN %-16s Eltern %d' % (text(s[:16]), struct.unpack('<i', s[16:20])[0]))
        elif typ == 0x0700:
            for t2, b, _ in abschnitte(inhalt):
                if t2 == 0x0701:
                    z.append('HLOD %s Hierarchie %s LOD %d' % (text(b[8:24]), text(b[24:40]), struct.unpack('<I', b[4:8])[0]))
                elif t2 in (0x0702, 0x0705, 0x0706):
                    for t3, x, _ in abschnitte(b):
                        if t3 == 0x0704:
                            z.append('  UNTEROBJEKT Knochen %d %s' % (struct.unpack('<I', x[:4])[0], text(x[4:36])))
                        elif t3 == 0x0703:
                            z.append('  LOD Anzahl %d max %.1f' % (struct.unpack('<I', x[:4])[0], struct.unpack('<f', x[4:8])[0]))
        elif typ == 0x0740:
            z.append('BOX %s' % text(inhalt[8:40]))
        else:
            z.append('ABSCHNITT %s (0x%04X)' % (NAMEN.get(typ, '?'), typ))
    return z


def knochen(daten):
    """Pivots der Hierarchie: [(name, eltern, (tx, ty, tz), (qx, qy, qz, qw))]."""
    out = []
    for typ, inhalt, _ in abschnitte(daten):
        if typ == 0x0100:
            for t2, b, _ in abschnitte(inhalt):
                if t2 == 0x0102:
                    for i in range(len(b) // 60):
                        s = b[60 * i:60 * i + 60]
                        out.append((text(s[:16]), struct.unpack('<i', s[16:20])[0], struct.unpack('<3f', s[20:32]),
                                    struct.unpack('<4f', s[44:60])))
    return out


def netze(daten):
    """{netzname: (anzahl dreiecke, [(x, y, z)...], [(nx, ny, nz)...])} -- Ecken in Netzkoordinaten."""
    out = {}
    for typ, inhalt, _ in abschnitte(daten):
        if typ != 0x0000:
            continue
        name, ndr, ecken, normalen = '?', 0, [], []
        for t2, b, _ in abschnitte(inhalt):
            if t2 == 0x001F:
                name = text(b[8:24])
                ndr = struct.unpack('<I', b[40:44])[0]
            elif t2 == 0x0002:
                ecken = [struct.unpack('<3f', b[12 * i:12 * i + 12]) for i in range(len(b) // 12)]
            elif t2 == 0x0003:
                normalen = [struct.unpack('<3f', b[12 * i:12 * i + 12]) for i in range(len(b) // 12)]
        out[name] = (ndr, ecken, normalen)
    return out


def normalen_ersetzen(daten, funktion):
    """Gibt die Datei mit neuen Eckennormalen zurück: funktion(netzname, ecken, normalen) -> normalen.
    Die Engine beleuchtet Bäume (W3DTreeBuffer) und Requisiten mit diesen Normalen; das Add-on schreibt nur die
    geglättete Flächennormale."""
    teile, p = [], 0
    for typ, inhalt, sub in abschnitte(daten):
        kopf = daten[p:p + 8]
        if typ == 0x0000:
            name = '?'
            ecken = []
            for t2, b, _ in abschnitte(inhalt):
                if t2 == 0x001F:
                    name = text(b[8:24])
                elif t2 == 0x0002:
                    ecken = [struct.unpack('<3f', b[12 * i:12 * i + 12]) for i in range(len(b) // 12)]
            neu, q = [], 0
            for t2, b, _ in abschnitte(inhalt):
                k2 = inhalt[q:q + 8]
                if t2 == 0x0003:
                    alt = [struct.unpack('<3f', b[12 * i:12 * i + 12]) for i in range(len(b) // 12)]
                    b = b''.join(struct.pack('<3f', *n) for n in funktion(name, ecken, alt))
                    k2 = struct.pack('<II', t2, struct.unpack('<I', k2[4:8])[0] & 0x80000000 | len(b))
                neu.append(k2 + b)
                q += 8 + (struct.unpack('<I', inhalt[q + 4:q + 8])[0] & 0x7FFFFFFF)
            inhalt = b''.join(neu)
            kopf = struct.pack('<II', typ, struct.unpack('<I', kopf[4:8])[0] & 0x80000000 | len(inhalt))
        teile.append(kopf + inhalt)
        p += 8 + (struct.unpack('<I', daten[p + 4:p + 8])[0] & 0x7FFFFFFF)
    return b''.join(teile)


def lesen_baum(daten):
    """Abschnitte als veränderbarer Baum: [[typ, unterabschnitte(bool), inhalt(bytes oder Liste)]]."""
    out = []
    for typ, inhalt, sub in abschnitte(daten):
        out.append([typ, sub, lesen_baum(inhalt) if (sub or typ in CONTAINER) else inhalt])
    return out


def schreiben_baum(baum_):
    teile = []
    for typ, sub, inhalt in baum_:
        b = schreiben_baum(inhalt) if isinstance(inhalt, list) else inhalt
        teile.append(struct.pack('<II', typ, len(b) | (0x80000000 if sub else 0)) + b)
    return b''.join(teile)


def kind(knoten, typ):
    return [k for k in knoten[2] if k[0] == typ] if isinstance(knoten[2], list) else []


def normalen_glaetten(daten, winkel=60.0):
    """Normalen von Ecken gleicher Lage mitteln (nur solche, die weniger als `winkel` voneinander abweichen).
    Das Add-on trennt Ecken an UV-Nähten und rechnet die Normale danach je Teilstück -- an jeder Naht entstünde
    sonst eine sichtbare Schattierungskante."""
    import math
    grenze = math.cos(math.radians(winkel))

    def glaetten(_name, ecken, normalen):
        gruppen = {}
        for i, p in enumerate(ecken):
            gruppen.setdefault(tuple(round(c, 3) for c in p), []).append(i)
        neu = list(normalen)
        for idx in gruppen.values():
            if len(idx) < 2:
                continue
            for i in idx:
                s = [0.0, 0.0, 0.0]
                for j in idx:
                    if sum(a * b for a, b in zip(normalen[i], normalen[j])) >= grenze:
                        s = [a + b for a, b in zip(s, normalen[j])]
                l = math.sqrt(sum(a * a for a in s)) or 1.0
                neu[i] = tuple(a / l for a in s)
        return neu
    return normalen_ersetzen(daten, glaetten)


if __name__ == '__main__':
    if sys.argv[1] == '--vergleich':
        a = zusammenfassung(open(sys.argv[2], 'rb').read())
        b = zusammenfassung(open(sys.argv[3], 'rb').read())
        breite = max(len(x) for x in a + ['']) + 2
        for i in range(max(len(a), len(b))):
            x = a[i] if i < len(a) else ''
            y = b[i] if i < len(b) else ''
            print('%s %-*s | %s' % (' ' if x == y else '*', breite, x, y))
    else:
        d = open(sys.argv[1], 'rb').read()
        print('\n'.join(baum(d) if '--baum' in sys.argv else zusammenfassung(d)))
