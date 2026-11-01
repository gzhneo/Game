#!/usr/bin/env python3
# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
"""Find German text and document markers in string literals of the code.

Code texts (log lines, message boxes, static_assert texts, CMake descriptions) are English and carry no
references such as R-128, E-8.1, P-1.4 or TODO 68. This check lists every string literal that still has
German words or such a marker. Fixed keys that tools parse (KISIEG, WACHSTUM, the Testablauf command
words, asset file names, ...) are listed in texte_schluessel.txt and are allowed.

A word counts as German if it is in the German word list and not in the English one; the spellings
ae/oe/ue for umlauts are recognised. Word lists (Debian/Ubuntu): wamerican-large, wngerman.

Usage:
    python tools/refactor/texte_im_code.py               # check, exit code 1 on new findings
    python tools/refactor/texte_im_code.py --liste       # print all findings including allowed ones
    python tools/refactor/texte_im_code.py --schluessel  # rewrite texte_schluessel.txt from the current state
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..'))
SCHLUESSEL = os.path.join(HERE, 'texte_schluessel.txt')
EN_LISTE = '/usr/share/dict/american-english-large'
DE_LISTE = '/usr/share/dict/ngerman'
BEREICHE = ['Code', 'cmake', 'CMakeLists.txt']
ENDUNGEN = ('.cpp', '.h', '.inl', '.cmake', '.rc')

# Technical words the English list lacks but that are no German.
EN_EXTRA = set('''ok api fps ms id ini dds wnd big exe ssao msaa hlsl dxgi vsync gpu cpu bc dxt bgra rgba ui fifo lod
argb mip mips shader shaders texel texels viewport debug renderer sdk backend frontend bool enum int struct ptr src dst
num buf str tex len min max bit bits ref refs pos hex rgb crc log logs mod mods loc alt ctrl esc temp tmp arg args
config init def var vars prev nano dump dumps png bmp tga wav mp3 html json txt csv zip dll lib dir dirs util utils
async sync vulkan volk spirv'''.split())

LITERAL = re.compile(r'(?:L|u8)?"((?:[^"\\\n]|\\.)*)"')
MARKER = re.compile(r'\b(?:R-\d{2,3}|[EPVXKHMNAW]-\d+(?:\.\d+)*[a-z]?|TODO\s*\d+|Nr\.\s*\d+)\b')
WORT = re.compile(r"[A-Za-zÄÖÜäöüß]{3,}")
FORMAT = re.compile(r'%[-+ #0-9.]*[a-zA-Z]+|\\[nrt]')


def lade(pfad):
	with open(pfad, encoding='utf-8', errors='ignore') as f:
		return set(w.strip().lower() for w in f)


def deutsch(wort, en, de):
	w = wort.lower()
	if w in en or w in EN_EXTRA:
		return False
	if w in de:
		return True
	u = w.replace('ae', 'ä').replace('oe', 'ö').replace('ue', 'ü')
	if u != w and u in de and u not in en:
		return True
	return bool(re.search('[äöüß]', w))


def dateien():
	for bereich in BEREICHE:
		voll = os.path.join(ROOT, bereich)
		if os.path.isfile(voll):
			yield bereich
			continue
		for d, _, fs in os.walk(voll):
			for f in sorted(fs):
				if f.lower().endswith(ENDUNGEN) or f == 'CMakeLists.txt':
					yield os.path.relpath(os.path.join(d, f), ROOT).replace(os.sep, '/')


def funde(en, de):
	for rel in dateien():
		with open(os.path.join(ROOT, rel), encoding='latin-1') as f:
			zeilen = f.read().splitlines()
		for nr, zeile in enumerate(zeilen, 1):
			if zeile.strip().startswith(('//', '*', '/*', '#include')):
				continue
			for m in LITERAL.finditer(zeile):
				text = m.group(1)
				woerter = [w for w in WORT.findall(FORMAT.sub('  ', text)) if not re.search('[A-Z].*[A-Z]', w[1:])]
				if MARKER.search(text) or any(deutsch(w, en, de) for w in woerter):
					yield rel, nr, text


def main():
	if not (os.path.isfile(EN_LISTE) and os.path.isfile(DE_LISTE)):
		print('Wortlisten fehlen (apt-get install wamerican-large wngerman) -- Pruefung uebersprungen')
		return 0
	en, de = lade(EN_LISTE), lade(DE_LISTE)
	alle = list(funde(en, de))
	if '--schluessel' in sys.argv:
		with open(SCHLUESSEL, 'w', encoding='latin-1', newline='\n') as f:
			f.write('# Erlaubte Zeichenketten mit deutschen Woertern oder Markern: feste Schluessel, die Werkzeuge\n'
					'# auswerten, Asset- und Dateinamen. Je Zeile <Datei>TAB<Text>; erzeugt mit texte_im_code.py --schluessel.\n')
			for eintrag in sorted(set((rel, text) for rel, _, text in alle)):
				f.write('%s\t%s\n' % eintrag)
		print('%d Eintraege geschrieben' % len(set((rel, text) for rel, _, text in alle)))
		return 0
	erlaubt = set()
	if os.path.isfile(SCHLUESSEL):
		with open(SCHLUESSEL, encoding='latin-1') as f:
			erlaubt = set(tuple(z.rstrip('\n').split('\t', 1)) for z in f if z.strip() and not z.startswith('#'))
	neu = [(rel, nr, text) for rel, nr, text in alle if (rel, text) not in erlaubt]
	for rel, nr, text in (alle if '--liste' in sys.argv else neu):
		print('%s:%d: %s' % (rel, nr, text[:160]))
	print('%d auffaellige Zeichenketten, davon %d erlaubt (texte_schluessel.txt), %d neu'
		  % (len(alle), len(alle) - len(neu), len(neu)))
	return 1 if neu else 0


if __name__ == '__main__':
	sys.exit(main())
