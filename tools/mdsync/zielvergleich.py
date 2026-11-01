#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
"""Zielvergleich zweier CMake-Build-Ordner (Phase 8, Schritt 0, P8-6).

Belegt "gleich gebaut": liest die von CMake erzeugten .vcxproj zweier Build-Ordner und vergleicht je
Ziel und Konfiguration alles, was den erzeugten Code bestimmt:

  - Eigenschaften je Konfiguration (ConfigurationType, TargetName, TargetExt, OutDir, ...),
  - Werkzeugeinstellungen je Konfiguration (ClCompile, Link, Lib, ResourceCompile, Midl, Pre-/Post-
    Build-Schritte): Include-Reihenfolge, Definitionen, Schalter, Bibliotheken,
  - die Items (Quelldateien, Header, Ressourcen, Objektdateien, eigene Bauschritte) samt ihren
    Metadaten je Datei (z. B. /wd4018 an fuenf Dateien, PCH, ExcludedFromBuild),
  - die Reihenfolge der Quelldateien (sie bestimmt die Linkreihenfolge und damit die Reihenfolge der
    statischen Initialisierung),
  - die Projektverweise (nur die Zielnamen, keine GUIDs),
  - die ctest-Eintraege (CTestTestfile.cmake, add_test und set_tests_properties).

Nicht verglichen: GUIDs, die Regenerationsregel von CMake (CustomBuild auf CMakeLists.txt) und die
Hilfsziele ALL_BUILD, ZERO_CHECK, INSTALL, RUN_TESTS, PACKAGE.

Pfade werden vor dem Vergleich normalisiert: Quellwurzel -> <SRC>, Build-Ordner -> <BUILD>,
Rueckwaertsschraegstriche -> '/', Gross/Klein bei Pfaden egal. Damit sind zwei Build-Ordner desselben
Stands gleich, auch wenn sie verschieden heissen. Fuer Schritte, die absichtlich etwas verschieben,
gibt es Abbildungen, die auf Seite A angewandt werden, bevor verglichen wird:

  --pfad ALT=NEU   Pfadpraefix umschreiben (nach der Normalisierung, z. B.
                   --pfad "<SRC>/Generals/Code/GameEngine/Source/Common=<SRC>/Code/GameEngine/Common")
  --pfad-datei D   dieselben Abbildungen aus einer Datei, je Zeile eine (Zeilen wie bei --pfad);
                   fuer lange Listen, die nicht auf eine Befehlszeile passen
  --quelle-a, --quelle-b  Quellwurzel je Seite (Vorgabe: das Repository dieses Skripts). Noetig, wenn A und B aus
                   verschiedenen Arbeitskopien konfiguriert wurden, etwa vorher in einem Worktree auf dem alten Stand.
  --ziel ALT=NEU   Zielnamen umschreiben, auch in Verweisen und Bibliotheksnamen
                   (z. B. --ziel GameEngineCommonMD=GameEngineCommon fuer Schritt 4)

Aufruf:
  python tools/mdsync/zielvergleich.py A B                  alle Ziele, alle Konfigurationen
  python tools/mdsync/zielvergleich.py A B --konfig Debug   nur eine Konfiguration (mehrfach moeglich)
  python tools/mdsync/zielvergleich.py A B --nur GZHNeo     nur Ziele, deren Name den Text enthaelt
  python tools/mdsync/zielvergleich.py A B --einseitig      Ziele, die nur auf einer Seite liegen, als
                                                            Abweichung werten (sonst nur gemeldet)
  python tools/mdsync/zielvergleich.py --selbsttest         prueft das Werkzeug an erfundenen Projekten

Rueckgabe 0: gleich. 1: Abweichung. 2: Aufruffehler.
Liest nur, schreibt nichts. Die .vcxproj sind UTF-8 (CMake), CTestTestfile.cmake wird als UTF-8 mit
Ersatzzeichen gelesen.
"""
import argparse, collections, os, re, sys, tempfile
import xml.etree.ElementTree as ET

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
NS = "{http://schemas.microsoft.com/developer/msbuild/2003}"
HILFSZIELE = {"ALL_BUILD", "ZERO_CHECK", "INSTALL", "RUN_TESTS", "PACKAGE"}
# Einstellungen, deren Wert eine ';'-Liste ist; verglichen als Liste (Reihenfolge zaehlt, wird aber
# getrennt gemeldet, damit "nur umsortiert" von "fehlt" unterscheidbar ist).
LISTEN = {"AdditionalIncludeDirectories", "PreprocessorDefinitions", "AdditionalDependencies",
          "AdditionalLibraryDirectories", "DisableSpecificWarnings", "ForcedIncludeFiles",
          "AdditionalUsingDirectories", "DelayLoadDLLs", "IgnoreSpecificDefaultLibraries"}
# Elemente, die nichts ueber den erzeugten Code sagen.
UNWICHTIG = {"ProjectGuid", "Project", "ProjectName", "Keyword", "VCProjectUpgraderObjectName",
             "_ProjectFileVersion"}
KONFIG_RE = re.compile(r"'\$\(Configuration\)\|\$\(Platform\)'\s*==\s*'([^|']+)\|([^']+)'")


# ------------------------------------------------------------------------------------------------
# Normalisierung
# ------------------------------------------------------------------------------------------------
class Normalisierer:
    def __init__(self, build_dir, pfade, ziele, quelle=None):
        self.ersetz = []
        for wurzel, name in ((os.path.abspath(build_dir), "<BUILD>"), (os.path.abspath(quelle or ROOT), "<SRC>")):
            # Build-Ordner zuerst: er liegt meist unter der Quellwurzel.
            w = wurzel.replace("\\", "/").rstrip("/")
            self.ersetz.append((re.compile(re.escape(w), re.I), name))
        self.pfade = [(re.compile(re.escape(a), re.I), b) for a, b in pfade]
        # Zielnamen nur als ganze Woerter (GameEngineCommonMD, nicht GameEngineCommonMDX) und nicht als
        # Verzeichnis (folgt ein '/', ist es ein Ordnername wie Code/Platform, kein Ziel).
        self.ziele = [(re.compile(r"(?<![A-Za-z0-9_])" + re.escape(a) + r"(?![A-Za-z0-9_/])"), b)
                      for a, b in ziele]

    def text(self, s):
        if s is None:
            return ""
        s = s.strip().replace("\\", "/")
        for rx, name in self.ersetz:
            s = rx.sub(name, s)
        for rx, neu in self.pfade:
            s = rx.sub(lambda m, neu=neu: neu, s)
        for rx, neu in self.ziele:
            s = rx.sub(lambda m, neu=neu: neu, s)
        s = re.sub(r"/{2,}", "/", s)
        return s

    def ziel(self, name):
        for rx, neu in self.ziele:
            name = rx.sub(lambda m, neu=neu: neu, name)
        return name


def pfad_schluessel(s):
    """Pfade vergleicht Windows ohne Gross/Klein; alles, was nach Pfad aussieht, klein."""
    return s.lower() if ("<SRC>" in s or "<BUILD>" in s or "/" in s) else s


def liste(wert):
    return [t for t in (x.strip() for x in wert.split(";")) if t and not t.startswith("%(")]


# ------------------------------------------------------------------------------------------------
# Einlesen
# ------------------------------------------------------------------------------------------------
def konfig_aus(cond):
    if not cond:
        return "*"
    m = KONFIG_RE.search(cond)
    return m.group(1) if m else "?" + cond


def lies_projekt(pfad, norm, konfigs):
    """Liefert (werte, quellreihenfolge, verweise): werte ist dict schluessel -> normierter Text."""
    baum = ET.parse(pfad).getroot()
    werte = {}
    quellen = []
    verweise = set()

    def nimm(k):
        return not konfigs or k == "*" or k in konfigs

    projektordner = os.path.dirname(os.path.abspath(pfad))

    def absolut(eintrag):
        # CMake schreibt Bibliotheken und Suchpfade teils relativ zum Projektordner (../GameEngine/
        # Debug/x.lib). Zieht ein Ziel in einen anderen Ordner, sind das dieselben Dateien unter
        # anderem Weg -- deshalb vor dem Vergleich gegen den Projektordner aufloesen.
        e = eintrag.strip()
        if re.match(r"^\.\.?[\\/]", e):
            return os.path.normpath(os.path.join(projektordner, e))
        return e

    def setze(schluessel, einstellung, text):
        if einstellung in LISTEN and text:
            text = ";".join(absolut(x) for x in text.split(";"))
        text = norm.text(text)
        if einstellung in LISTEN:
            werte[schluessel] = ("LISTE", [pfad_schluessel(x) for x in liste(text)])
        else:
            werte[schluessel] = ("TEXT", pfad_schluessel(text))

    for pg in baum.findall(NS + "PropertyGroup"):
        k = konfig_aus(pg.get("Condition"))
        if not nimm(k):
            continue
        for e in pg:
            name = e.tag.replace(NS, "")
            if name in UNWICHTIG:
                continue
            setze("eigenschaft[%s].%s" % (k, name), name, e.text)

    for idg in baum.findall(NS + "ItemDefinitionGroup"):
        k = konfig_aus(idg.get("Condition"))
        if not nimm(k):
            continue
        for werkzeug in idg:
            wname = werkzeug.tag.replace(NS, "")
            for e in werkzeug:
                name = e.tag.replace(NS, "")
                setze("einstellung[%s].%s.%s" % (k, wname, name), name, e.text)

    for ig in baum.findall(NS + "ItemGroup"):
        for item in ig:
            art = item.tag.replace(NS, "")
            inc = norm.text(item.get("Include"))
            if art == "ProjectReference":
                # Vom rohen Pfad ausgehen: norm.text hat die Zielnamen-Abbildung schon angewandt, ein
                # zweites norm.ziel wuerde verkettete Regeln (A->B, B->C) doppelt ausfuehren.
                roh = (item.get("Include") or "").replace("\\", "/")
                verweise.add(norm.ziel(os.path.splitext(os.path.basename(roh))[0]))
                continue
            if art == "CustomBuild" and inc.lower().endswith("cmakelists.txt"):
                continue  # Regenerationsregel von CMake
            ikey = pfad_schluessel(inc)
            werte["item[%s] %s" % (art, ikey)] = ("TEXT", "vorhanden")
            if art in ("ClCompile", "Object", "ResourceCompile"):
                quellen.append("%s %s" % (art, ikey))
            for e in item:
                name = e.tag.replace(NS, "")
                if name in UNWICHTIG:
                    continue
                k = konfig_aus(e.get("Condition"))
                if not nimm(k):
                    continue
                setze("item[%s] %s .%s[%s]" % (art, ikey, name, k), name, e.text)
    return werte, quellen, verweise


def projektdateien(build_dir):
    """Die .vcxproj, die zum aktuellen Stand gehoeren. Massgeblich ist die Projektmappe (.slnx, sonst .sln):
    Ziele, die es nicht mehr gibt, hinterlassen ihre .vcxproj im Build-Ordner (so lag RenderUnitTestsMD nach
    Teil 5 noch in build-x64-tools). Ohne Projektmappe werden alle .vcxproj gesammelt (Rueckgabe dann mit
    Warnung)."""
    namen = sorted(os.listdir(build_dir))
    for f in namen:
        if f.endswith(".slnx"):
            wurzel = ET.parse(os.path.join(build_dir, f)).getroot()
            return [os.path.normpath(os.path.join(build_dir, p.get("Path")))
                    for p in wurzel.iter("Project") if p.get("Path", "").endswith(".vcxproj")], f
    for f in namen:
        if f.endswith(".sln"):
            with open(os.path.join(build_dir, f), encoding="utf-8-sig", errors="replace") as fh:
                pfade = re.findall(r'^Project\("[^"]*"\)\s*=\s*"[^"]*",\s*"([^"]+\.vcxproj)"', fh.read(), re.M)
            return [os.path.normpath(os.path.join(build_dir, p)) for p in pfade], f
    gefunden = []
    for dirpath, dirnames, filenames in os.walk(build_dir):
        dirnames[:] = [d for d in dirnames if d not in ("CMakeFiles", "vcpkg_installed", "_deps", ".vs")]
        gefunden += [os.path.join(dirpath, f) for f in filenames if f.endswith(".vcxproj")]
    return gefunden, None


def lies_build(build_dir, pfade, ziele, konfigs, nur, aus=sys.stdout, quelle=None):
    norm = Normalisierer(build_dir, pfade, ziele, quelle)
    projekte = {}
    doppelt = []
    dateien, mappe = projektdateien(build_dir)
    if mappe is None:
        print("WARNUNG: keine Projektmappe in %s, alle .vcxproj gelesen (auch veraltete)" % build_dir, file=aus)
    for pfad in dateien:
        ziel = os.path.splitext(os.path.basename(pfad))[0]
        if ziel in HILFSZIELE:
            continue
        ziel = norm.ziel(ziel)
        if nur and not any(n.lower() in ziel.lower() for n in nur):
            continue
        if not os.path.isfile(pfad):
            print("WARNUNG: in der Projektmappe, aber nicht vorhanden: %s" % pfad, file=aus)
            continue
        if ziel in projekte:
            doppelt.append(ziel)
        projekte[ziel] = (pfad, lies_projekt(pfad, norm, konfigs))
    tests = lies_ctest(build_dir, norm)
    return projekte, tests, doppelt


def lies_ctest(build_dir, norm):
    """add_test/set_tests_properties je Test (normierte Zeilen). Gelesen wird ab der CTestTestfile.cmake der
    Wurzel entlang der subdirs()-Kette -- genau das, was ctest selbst liest; liegengebliebene Dateien
    entfernter Ziele zaehlen nicht."""
    tests = collections.defaultdict(list)
    offen = [os.path.abspath(build_dir)]
    gesehen = set()
    while offen:
        ordner = offen.pop(0)
        datei = os.path.join(ordner, "CTestTestfile.cmake")
        if ordner in gesehen or not os.path.isfile(datei):
            continue
        gesehen.add(ordner)
        with open(datei, encoding="utf-8", errors="replace") as fh:
            for zeile in fh:
                z = zeile.strip()
                m = re.match(r"(add_test|set_tests_properties)\(\s*(\[=\[)?([^\]\s\)]+)", z)
                if m:
                    # _BACKTRACE_TRIPLES nennt die Zeile der CMakeLists, in der add_test steht --
                    # verschiebt sich bei jeder Kommentaraenderung und sagt nichts ueber den Test.
                    z = re.sub(r'\s*_BACKTRACE_TRIPLES\s+"[^"]*"', '', z)
                    tests[norm.text(m.group(3))].append(pfad_schluessel(norm.text(z)))
                    continue
                m = re.match(r'subdirs\(\s*"?([^")]+)"?\s*\)', z)
                if m:
                    offen.append(os.path.normpath(os.path.join(ordner, m.group(1))))
    return {k: sorted(v) for k, v in tests.items()}


# ------------------------------------------------------------------------------------------------
# Vergleich
# ------------------------------------------------------------------------------------------------
def vergleiche_liste(a, b):
    """Beschreibung einer Listenabweichung oder None."""
    if a == b:
        return None
    teile = []
    nur_a = [x for x in a if x not in b]
    nur_b = [x for x in b if x not in a]
    if nur_a:
        teile.append("nur A: " + "; ".join(nur_a))
    if nur_b:
        teile.append("nur B: " + "; ".join(nur_b))
    if not nur_a and not nur_b:
        if collections.Counter(a) != collections.Counter(b):
            teile.append("Vielfachheit verschieden")
        else:
            teile.append("nur die Reihenfolge ist anders")
    return " | ".join(teile)


def vergleiche_projekt(pa, pb):
    wa, qa, va = pa
    wb, qb, vb = pb
    befunde = []
    for k in sorted(set(wa) | set(wb)):
        if k not in wb:
            befunde.append("fehlt in B: %s = %s" % (k, zeige(wa[k])))
        elif k not in wa:
            befunde.append("neu in B:   %s = %s" % (k, zeige(wb[k])))
        elif wa[k] != wb[k]:
            if wa[k][0] == "LISTE" and wb[k][0] == "LISTE":
                befunde.append("anders:     %s -- %s" % (k, vergleiche_liste(wa[k][1], wb[k][1])))
            else:
                befunde.append("anders:     %s\n              A: %s\n              B: %s" % (k, zeige(wa[k]), zeige(wb[k])))
    gemeinsam_a = [q for q in qa if q in set(qb)]
    gemeinsam_b = [q for q in qb if q in set(qa)]
    if gemeinsam_a != gemeinsam_b:
        i = next(n for n, (x, y) in enumerate(zip(gemeinsam_a, gemeinsam_b)) if x != y)
        befunde.append("Reihenfolge der Quellen anders ab Nr. %d: A %s, B %s" % (i + 1, gemeinsam_a[i], gemeinsam_b[i]))
    if va != vb:
        if va - vb:
            befunde.append("Verweise nur in A: " + ", ".join(sorted(va - vb)))
        if vb - va:
            befunde.append("Verweise nur in B: " + ", ".join(sorted(vb - va)))
    return befunde


def zeige(w):
    return ";".join(w[1]) if w[0] == "LISTE" else w[1]


def vergleiche(a_dir, b_dir, pfade, ziele, konfigs, nur, einseitig, aus=sys.stdout, quelle_a=None, quelle_b=None):
    pa, ta, da = lies_build(a_dir, pfade, ziele, konfigs, nur, aus, quelle_a)
    pb, tb, db = lies_build(b_dir, [], [], konfigs, nur, aus, quelle_b)
    abweichung = False
    for seite, d in (("A", da), ("B", db)):
        if d:
            print("WARNUNG: Zielname in %s mehrfach: %s" % (seite, ", ".join(sorted(set(d)))), file=aus)
            abweichung = True
    nur_a = sorted(set(pa) - set(pb))
    nur_b = sorted(set(pb) - set(pa))
    if nur_a:
        print("Ziele nur in A (%d): %s" % (len(nur_a), ", ".join(nur_a)), file=aus)
    if nur_b:
        print("Ziele nur in B (%d): %s" % (len(nur_b), ", ".join(nur_b)), file=aus)
    if einseitig and (nur_a or nur_b):
        abweichung = True
    gleich = 0
    for ziel in sorted(set(pa) & set(pb)):
        befunde = vergleiche_projekt(pa[ziel][1], pb[ziel][1])
        if befunde:
            abweichung = True
            print("\n=== %s  (%d Abweichungen)" % (ziel, len(befunde)), file=aus)
            for b in befunde:
                print("  " + b, file=aus)
        else:
            gleich += 1
    tbef = []
    for t in sorted(set(ta) | set(tb)):
        if t not in tb:
            tbef.append("Test nur in A: %s" % t)
        elif t not in ta:
            tbef.append("Test nur in B: %s" % t)
        elif ta[t] != tb[t]:
            tbef.append("Test anders: %s\n    A: %s\n    B: %s" % (t, "\n       ".join(ta[t]), "\n       ".join(tb[t])))
    if tbef:
        abweichung = True
        print("\n=== ctest", file=aus)
        for b in tbef:
            print("  " + b, file=aus)
    print("\nZiele verglichen: %d, davon gleich: %d; ctest-Eintraege: A %d, B %d%s" % (
        len(set(pa) & set(pb)), gleich, len(ta), len(tb),
        "" if not konfigs else "; Konfigurationen: " + ", ".join(sorted(konfigs))), file=aus)
    print("ERGEBNIS: " + ("ABWEICHUNG" if abweichung else "GLEICH"), file=aus)
    return 1 if abweichung else 0


# ------------------------------------------------------------------------------------------------
# Selbsttest
# ------------------------------------------------------------------------------------------------
VORLAGE = """<?xml version="1.0" encoding="UTF-8"?>
<Project DefaultTargets="Build" ToolsVersion="17.0" xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
  <PropertyGroup Label="Globals"><ProjectGuid>{GUID}</ProjectGuid></PropertyGroup>
  <PropertyGroup Condition="'$(Configuration)|$(Platform)'=='Debug|x64'" Label="Configuration">
    <ConfigurationType>StaticLibrary</ConfigurationType>
  </PropertyGroup>
  <ItemDefinitionGroup Condition="'$(Configuration)|$(Platform)'=='Debug|x64'">
    <ClCompile>
      <AdditionalIncludeDirectories>INCS;%(AdditionalIncludeDirectories)</AdditionalIncludeDirectories>
      <PreprocessorDefinitions>%(PreprocessorDefinitions);DEFS</PreprocessorDefinitions>
    </ClCompile>
  </ItemDefinitionGroup>
  <ItemGroup>
    QUELLEN
  </ItemGroup>
  <ItemGroup>
    <CustomBuild Include="SRC\\CMakeLists.txt"><Command>BUILD\\egal GUID</Command></CustomBuild>
    <ProjectReference Include="BUILD\\x\\WWLib.vcxproj"><Project>{GUID}</Project></ProjectReference>
  </ItemGroup>
</Project>
"""


def schreibe_projekt(ordner, name, incs, defs, quellen, guid, verweis="WWLib"):
    src = ROOT.replace("/", "\\")
    b = os.path.abspath(ordner).replace("/", "\\")
    q = "\n    ".join('<ClCompile Include="%s\\%s"%s' % (src, n, (">" + meta + "</ClCompile>") if meta else " />")
                     for n, meta in quellen)
    t = (VORLAGE.replace("INCS", ";".join(src + "\\" + i for i in incs)).replace("DEFS", ";".join(defs))
         .replace("QUELLEN", q).replace("SRC", src).replace("BUILD", b).replace("GUID", guid)
         .replace("WWLib.vcxproj", verweis + ".vcxproj"))
    os.makedirs(os.path.join(ordner, "x"), exist_ok=True)
    with open(os.path.join(ordner, "x", name + ".vcxproj"), "w", encoding="utf-8") as fh:
        fh.write(t)


def selbsttest():
    basis = dict(incs=["Code\\A\\Include", "Code\\B\\Include"], defs=["WIN32", "_DEBUG"],
                 quellen=[("Code\\a.cpp", ""), ("Code\\b.cpp", "<DisableSpecificWarnings>4018</DisableSpecificWarnings>")])
    faelle = [
        ("gleicher Stand, andere GUIDs und Ordnernamen", {}, 0, None),
        ("Definition fehlt", dict(defs=["WIN32"]), 1, "_debug"),
        ("Include-Reihenfolge vertauscht", dict(incs=["Code\\B\\Include", "Code\\A\\Include"]), 1, "Reihenfolge"),
        ("Quelle fehlt", dict(quellen=basis["quellen"][:1]), 1, "fehlt in B"),
        ("Quellen umsortiert", dict(quellen=list(reversed(basis["quellen"]))), 1, "Reihenfolge der Quellen"),
        ("Datei-Schalter fehlt", dict(quellen=[("Code\\a.cpp", ""), ("Code\\b.cpp", "")]), 1, "DisableSpecificWarnings"),
    ]
    fehler = 0
    with tempfile.TemporaryDirectory() as tmp:
        for i, (titel, aend, erwartet, merkmal) in enumerate(faelle):
            a = os.path.join(tmp, "a%d" % i)
            b = os.path.join(tmp, "b%d_anders" % i)
            schreibe_projekt(a, "Ziel", guid="{1111}", **basis)
            p = dict(basis)
            p.update(aend)
            schreibe_projekt(b, "Ziel", guid="{2222}", **p)
            import io
            puffer = io.StringIO()
            rc = vergleiche(a, b, [], [], set(), [], False, aus=puffer)
            text = puffer.getvalue()
            ok = rc == erwartet and (merkmal is None or merkmal.lower() in text.lower())
            print("%s  %s (Rueckgabe %d, erwartet %d)" % ("ok  " if ok else "FEHL", titel, rc, erwartet))
            if not ok:
                fehler += 1
                print(text)
        # Abbildung: A hat den alten Pfad und den alten Zielnamen, B den neuen -- mit --pfad/--ziel gleich.
        a = os.path.join(tmp, "m_a")
        b = os.path.join(tmp, "m_b")
        schreibe_projekt(a, "ZielMD", guid="{1}", incs=["GeneralsMD\\Code\\A\\Include"], defs=["X"], quellen=[("GeneralsMD\\Code\\a.cpp", "")])
        schreibe_projekt(b, "Ziel", guid="{2}", incs=["Code\\A\\Include"], defs=["X"], quellen=[("Code\\a.cpp", "")])
        import io
        rc = vergleiche(a, b, [("<SRC>/GeneralsMD/Code", "<SRC>/Code")], [("ZielMD", "Ziel")], set(), [], True, aus=io.StringIO())
        ok = rc == 0
        print("%s  Abbildung --pfad/--ziel (Rueckgabe %d, erwartet 0)" % ("ok  " if ok else "FEHL", rc))
        fehler += 0 if ok else 1
        # Verkettete Regeln (Lib -> Lib_ALT, LibMD -> Lib) auch in Projektverweisen nur einmal anwenden;
        # und ein gleichnamiger Ordner (Code/Lib/Include) wird nicht umbenannt.
        a = os.path.join(tmp, "k_a")
        b = os.path.join(tmp, "k_b")
        schreibe_projekt(a, "ZielMD", guid="{1}", incs=["Code\\Lib\\Include"], defs=["X"], quellen=[("Code\\a.cpp", "")], verweis="LibMD")
        schreibe_projekt(b, "Ziel", guid="{2}", incs=["Code\\Lib\\Include"], defs=["X"], quellen=[("Code\\a.cpp", "")], verweis="Lib")
        rc = vergleiche(a, b, [], [("Lib", "Lib_ALT"), ("ZielMD", "Ziel"), ("LibMD", "Lib")], set(), [], True, aus=io.StringIO())
        ok = rc == 0
        print("%s  verkettete --ziel-Regeln in Projektverweisen (Rueckgabe %d, erwartet 0)" % ("ok  " if ok else "FEHL", rc))
        fehler += 0 if ok else 1
        # Projektmappe: eine liegengebliebene .vcxproj eines entfernten Ziels zaehlt nicht mit.
        a = os.path.join(tmp, "s_a")
        b = os.path.join(tmp, "s_b")
        for d in (a, b):
            schreibe_projekt(d, "Ziel", guid="{1}", **basis)
            with open(os.path.join(d, "Mappe.slnx"), "w", encoding="utf-8") as fh:
                fh.write('<Solution><Project Path="x/Ziel.vcxproj" /></Solution>')
        schreibe_projekt(a, "Veraltet", guid="{3}", **basis)
        rc = vergleiche(a, b, [], [], set(), [], True, aus=io.StringIO())
        ok = rc == 0
        print("%s  veraltete .vcxproj ausserhalb der Projektmappe (Rueckgabe %d, erwartet 0)" % ("ok  " if ok else "FEHL", rc))
        fehler += 0 if ok else 1
    print("SELBSTTEST: " + ("bestanden" if fehler == 0 else "%d Fehler" % fehler))
    return 1 if fehler else 0


def paare(werte, was):
    aus = []
    for w in werte or []:
        if "=" not in w:
            raise SystemExit("--%s erwartet ALT=NEU, bekommen: %s" % (was, w))
        a, b = w.split("=", 1)
        aus.append((a, b))
    return aus


def pfade_aus_datei(dateien):
    """--pfad-Zeilen aus Dateien (je Zeile --pfad "ALT=NEU" oder ALT=NEU; # und Leerzeilen zaehlen nicht)."""
    aus = []
    for name in dateien or []:
        with open(name, encoding="utf-8") as f:
            for zeile in f:
                zeile = zeile.strip()
                if not zeile or zeile.startswith("#"):
                    continue
                if zeile.startswith("--pfad"):
                    zeile = zeile[len("--pfad"):].strip()
                aus.append(zeile.strip('"'))
    return aus


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("a", nargs="?", help="Build-Ordner A (vorher)")
    ap.add_argument("b", nargs="?", help="Build-Ordner B (nachher)")
    ap.add_argument("--konfig", action="append", help="nur diese Konfiguration (Debug, Release, ...)")
    ap.add_argument("--nur", action="append", help="nur Ziele, deren Name den Text enthaelt")
    ap.add_argument("--pfad", action="append", help="Pfadpraefix auf Seite A umschreiben, ALT=NEU")
    ap.add_argument("--pfad-datei", action="append", help="--pfad-Abbildungen aus einer Datei (je Zeile eine)")
    ap.add_argument("--ziel", action="append", help="Zielnamen auf Seite A umschreiben, ALT=NEU")
    ap.add_argument("--einseitig", action="store_true", help="Ziele nur auf einer Seite zaehlen als Abweichung")
    ap.add_argument("--quelle-a", help="Quellwurzel, aus der A konfiguriert wurde (Vorgabe: dieses Repository)")
    ap.add_argument("--quelle-b", help="Quellwurzel, aus der B konfiguriert wurde (Vorgabe: dieses Repository)")
    ap.add_argument("--selbsttest", action="store_true")
    a = ap.parse_args()
    if a.selbsttest:
        return selbsttest()
    if not a.a or not a.b:
        ap.print_usage()
        return 2
    for d in (a.a, a.b):
        if not os.path.isdir(d):
            print("Kein Ordner: " + d)
            return 2
    return vergleiche(a.a, a.b, paare((a.pfad or []) + pfade_aus_datei(a.pfad_datei), "pfad"), paare(a.ziel, "ziel"), set(a.konfig or []), a.nur or [], a.einseitig,
                      quelle_a=a.quelle_a, quelle_b=a.quelle_b)


if __name__ == "__main__":
    sys.exit(main())
