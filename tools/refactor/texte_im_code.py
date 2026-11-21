#!/usr/bin/env python3
# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
"""Find German text and document markers in string literals of the code.

Code texts (log lines, message boxes, static_assert texts, CMake descriptions) are English and carry no
references to issue numbers, plan steps or to-do items. This check lists every string literal that still has
German words or such a marker. Fixed keys that tools parse (AIVICTORY, GROWTH, the Testablauf command
words, asset file names, ...) are listed in texte_schluessel.txt and are allowed.

A word counts as German if it is in the German word list and not in the English one; the spellings
ae/oe/ue for umlauts are recognised. Word lists: /usr/share/dict from the Debian/Ubuntu packages
wamerican-large and wngerman; elsewhere (Windows) "--setup" downloads the same packages once and unpacks the
two lists to <GZH_WERKZEUGE>/wordlists, or next to the machine file of tools/pfade.py if GZH_WERKZEUGE is unset.
Without the lists the check fails.

Usage:
    python tools/refactor/texte_im_code.py               # check, exit code 1 on new findings
    python tools/refactor/texte_im_code.py --liste       # print all findings including allowed ones
    python tools/refactor/texte_im_code.py --schluessel  # rewrite texte_schluessel.txt from the current state
    python tools/refactor/texte_im_code.py --setup       # fetch the word lists (Windows, once per machine)
"""
import io
import os
import re
import sys
import tarfile
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..'))
SCHLUESSEL = os.path.join(HERE, 'texte_schluessel.txt')
LISTEN = {'en': 'american-english-large', 'de': 'ngerman'}
# Debian pool folder and package that carry each list as usr/share/dict/<name>
PAKETE = {'en': ('s/scowl/', 'wamerican-large'), 'de': ('i/igerman98/', 'wngerman')}
POOL = 'https://deb.debian.org/debian/pool/main/'
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


def eigener_ordner():
	"""Machine folder for the word lists where /usr/share/dict does not have them."""
	sys.path.insert(0, os.path.dirname(HERE))
	import pfade
	ordner = pfade.werkzeuge('wordlists', pflicht=False)
	return ordner or os.path.join(os.path.dirname(pfade.rechnerdatei()), 'wordlists')


def finde_listen():
	"""Paths of the English and the German list, or None if one is missing."""
	for ordner in ('/usr/share/dict', eigener_ordner()):
		pfade_ = [os.path.join(ordner, LISTEN[k]) for k in ('en', 'de')]
		if all(os.path.isfile(x) for x in pfade_):
			return pfade_
	return None


def deb_datei(daten, gesucht):
	"""One file from the data archive of a .deb (ar archive with data.tar.xz)."""
	if not daten.startswith(b'!<arch>\n'):
		raise ValueError('not a .deb archive')
	pos = 8
	while pos + 60 <= len(daten):
		name = daten[pos:pos + 16].decode('ascii').strip().rstrip('/')
		groesse = int(daten[pos + 48:pos + 58].decode('ascii').strip())
		inhalt = daten[pos + 60:pos + 60 + groesse]
		pos += 60 + groesse + (groesse & 1)
		if name.startswith('data.tar'):
			if name.endswith('.zst'):
				raise ValueError('data.tar.zst is not supported')
			with tarfile.open(fileobj=io.BytesIO(inhalt), mode='r:*') as tar:
				for eintrag in tar.getmembers():
					if eintrag.name.lstrip('./') == gesucht:
						return tar.extractfile(eintrag).read()
			raise ValueError('%s not in the package' % gesucht)
	raise ValueError('no data archive in the package')


def einrichten():
	ziel = eigener_ordner()
	os.makedirs(ziel, exist_ok=True)
	for k, (ordner, paket) in PAKETE.items():
		with urllib.request.urlopen(POOL + ordner, timeout=60) as r:
			seite = r.read().decode('utf-8', 'replace')
		# newest version: compare the numbers in the name, not the text (-16 after -9)
		namen = sorted(set(re.findall(r'%s_[^"/]+_all\.deb' % re.escape(paket), seite)),
					   key=lambda n: [int(x) for x in re.findall(r'\d+', n)])
		if not namen:
			print('no package %s found in %s%s' % (paket, POOL, ordner))
			return 1
		with urllib.request.urlopen(POOL + ordner + namen[-1], timeout=120) as r:
			daten = r.read()
		liste = deb_datei(daten, 'usr/share/dict/' + LISTEN[k])
		with open(os.path.join(ziel, LISTEN[k]), 'wb') as f:
			f.write(liste)
		print('%s: %s, %d lines' % (namen[-1], os.path.join(ziel, LISTEN[k]), liste.count(b'\n')))
	return 0


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
	if '--setup' in sys.argv:
		return einrichten()
	listen = finde_listen()
	if listen is None:
		print('word lists missing: apt-get install wamerican-large wngerman, or python tools/refactor/texte_im_code.py --setup')
		return 2
	en, de = lade(listen[0]), lade(listen[1])
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
