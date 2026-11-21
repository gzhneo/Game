#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
"""Find references to to-do items, issue numbers and plan steps in the published text files.

The published tree has no tracker, so a to-do number, an issue number or a plan step in a comment, README, tool or
data file points nowhere. This check scans tools/, tests/, Data/, Code/ and the top-level documents and lists every such
reference; the files that are left out of the published tree (see tools/oeffentlich/ausschluss.txt, if present) are
skipped. Exceptions go into nummern_erlaubt.txt (one `path<TAB>text` per line) where a number is part of the content.

Usage:
    python tools/refactor/nummern_im_text.py          # exit code 1 on findings
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..'))
BEREICHE = ['tools', 'tests', 'Data', 'Code', 'cmake', 'README.md', 'BUILD.md', 'NOTICE.md', 'ASSET_CREDITS.md']
AUSGELASSEN = ('tools/oeffentlich/', 'tools/unzensiert/', 'tools/ordnung/', 'tools/mdsync/', 'tools/refactor/',
               'tools/git/cherry_weiter.sh', 'tools/git/beide_behalten.py')
BEHALTEN = ('tools/mdsync/treediff.py', 'tools/mdsync/zielvergleich.py', 'tools/refactor/texte_im_code.py',
            'tools/refactor/nummern_im_text.py', 'tools/refactor/tabellenende.py', 'tools/refactor/renderer_schichten.py',
            'tools/refactor/projekttexte.py', 'tools/refactor/renderer_ordner.tsv', 'tools/refactor/singletons_kontext.py',
            'tools/refactor/singletons_kontext.txt')
TEXT = ('.py', '.sh', '.ps1', '.cmd', '.txt', '.md', '.ini', '.json', '.html', '.cmake', '.cpp', '.h', '.inl', '.hlsl', '.tsv',
        '.str', '.yml', '.yaml', '.csv')
MUSTER = re.compile(r'\b(?:TODO\s*(?:Nr\.\s*)?\d+|Nr\.\s*\d+|R-\d{2,4}[a-z]?|[EPV]-\d+\.\d+(?:\.\d+)*)\b')
ERLAUBT = os.path.join(HERE, 'nummern_erlaubt.txt')


def oeffentlich(rel):
	return rel in BEHALTEN or not rel.startswith(AUSGELASSEN)


def dateien():
	for b in BEREICHE:
		p = os.path.join(ROOT, b)
		if os.path.isfile(p):
			yield b
			continue
		for d, dirs, fs in os.walk(p):
			dirs[:] = [x for x in dirs if x not in ('.git', 'node_modules', 'seeds', '__pycache__')]
			for f in fs:
				if f.endswith(TEXT):
					yield os.path.relpath(os.path.join(d, f), ROOT).replace(os.sep, '/')


def main():
	erlaubt = set()
	if os.path.exists(ERLAUBT):
		with open(ERLAUBT, encoding='utf-8') as f:
			erlaubt = {z.rstrip('\n') for z in f if z.strip() and not z.startswith('#')}
	funde = []
	for rel in dateien():
		if not oeffentlich(rel):
			continue
		with open(os.path.join(ROOT, rel), encoding='utf-8', errors='ignore') as f:
			for n, z in enumerate(f, 1):
				for m in MUSTER.finditer(z):
					if rel + '\t' + m.group(0) not in erlaubt:
						funde.append('%s:%d: %s' % (rel, n, m.group(0)))
	for z in funde:
		print(z)
	print('%d Verweise auf Nummern' % len(funde))
	return 1 if funde else 0


if __name__ == '__main__':
	sys.exit(main())
