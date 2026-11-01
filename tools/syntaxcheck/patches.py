#!/usr/bin/env python3
# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
"""Harness-Patches im Symlink-Spiegel: MSVC-Eigenheiten, die clang nicht akzeptiert.
Aendert NUR den Spiegel, nie das Repository."""
import os, sys, shutil
T = os.path.abspath(sys.argv[1])
def unlink_copy(rel):
    p = os.path.join(T, rel)
    if os.path.islink(p):
        real = os.path.realpath(p); os.unlink(p); shutil.copy(real, p)
    return p
def sub(rel, pairs):
    p = unlink_copy(rel)
    s = open(p, errors='replace').read()
    for a, b in pairs:
        s = s.replace(a, b)
    open(p, 'w').write(s)
W = 'Core/'
# MSVC erlaubt die Spezialisierung eines statischen Templatemembers ohne "template<>".
# DEFINE_AUTO_POOL(T,N) erzeugt genau das; ohne diesen Patch scheitert jede Datei, die den
# Makro benutzt (multilist, slnode, aabtreecull, gridcull, hanim, meshrenderer).
# "template<> ... ::Allocator;" waere nur eine Deklaration -- erst der Initialisierer macht
# daraus eine Definition, sonst fehlt das Symbol beim Linken (tools/linuxtests).
sub(W+'WWLib/mempool.h', [('\nObjectPoolClass<T,BLOCKSIZE> AutoPoolClass<T,BLOCKSIZE>::Allocator;',
                           '\ntemplate<> ObjectPoolClass<T,BLOCKSIZE> AutoPoolClass<T,BLOCKSIZE>::Allocator{};')])
# Strukturierte Ausnahmebehandlung (__try/__except) kennen clang und g++ im MinGW-Ziel nicht.
# Fuer die Pruefung reicht der Aufruf ohne Wachposten -- der Ausnahmepfad wird nicht geprueft.
sub(W+'WWLib/thread.cpp', [("""		__try {
			tc->Thread_Function();
		} __except(tc->ExceptionHandler(GetExceptionCode(), GetExceptionInformation())) {};""",
                            """		tc->Thread_Function();	/* Spiegel: __try/__except entfernt */""")])
# MSVC macht "friend class X" fuer die normale Namenssuche sichtbar
sub('Renderer/WW3D2/Mesh/meshmdl.h', [('#include "meshrendererlist.h"', '#include "meshrendererlist.h"\nclass MeshModelClass;')])
# MSVC erlaubt Vorwaertsdeklaration unskopierter enums ohne Basistyp: in allen Headern
# des Spiegels "enum X;" zu "enum X : int;" machen.
import re as _re
_FWD_NAMES = set()
import re
_fwd = _re.compile(r'^(\s*)((?:typedef\s+)?)enum\s+([A-Za-z_][A-Za-z0-9_]*)\s*;', _re.M)
for _dp, _dn, _fns in os.walk(T):
    _dn[:] = [d for d in _dn if not os.path.islink(os.path.join(_dp, d))]
    for _fn in _fns:
        if not _fn.endswith('.h'): continue
        _p = os.path.join(_dp, _fn)
        try: _s = open(_p, errors='replace').read()
        except OSError: continue
        if not _fwd.search(_s): continue
        for _m in _fwd.finditer(_s):
            _FWD_NAMES.add(_m.group(3))
        _s2 = _fwd.sub(r'\1\2enum \3 : int;', _s)
        if _s2 != _s:
            if os.path.islink(_p):
                _r = os.path.realpath(_p); os.unlink(_p); shutil.copy(_r, _p)
            open(_p, 'w').write(_s2)

# ... und die Definitionen derselben enums bekommen denselben Basistyp
for _dp, _dn, _fns in os.walk(T):
    _dn[:] = [d for d in _dn if not os.path.islink(os.path.join(_dp, d))]
    for _fn in _fns:
        if not _fn.endswith(('.h', '.cpp')): continue
        _p = os.path.join(_dp, _fn)
        try: _s = open(_p, errors='replace').read()
        except OSError: continue
        _s2 = _s
        for _n in _FWD_NAMES:
            _s2 = re.sub(r'\benum\s+' + _n + r'\s*(?=\{)', 'enum ' + _n + ' : int ', _s2)
            _s2 = re.sub(r'\benum\s+' + _n + r'\s*\n(\s*)\{', 'enum ' + _n + ' : int\n\\1{', _s2)
        if _s2 != _s:
            if os.path.islink(_p):
                _r = os.path.realpath(_p); os.unlink(_p); shutil.copy(_r, _p)
            open(_p, 'w').write(_s2)
print("patches ok")
