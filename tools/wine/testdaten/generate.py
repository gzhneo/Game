#!/usr/bin/env python3
# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
"""Writes a complete, minimal game data folder without EA files.

    python3 tools/wine/testdaten/generate.py <target folder> [--big <archive name>] [--polyhaven [1k|2k|4k]] [--superwaffe]
                                             [--gegnerarten [--ohne-turmskript]] [--kameraskripte]
                                             [--leuchtfeuer] [--wiederaufbau] [--wiederaufbau-loch] [--teamfarbe] [--winter] [--rund]

The folder holds what Zero Hour needs to start and to run an AI-against-AI skirmish: INI files,
texts, window layouts, textures, W3D models and two maps, all computed by this script (standard library only,
same output on every run). The engine reads loose files before archives, so the folder next to
GZHNeoZH.exe is enough.

--polyhaven replaces the terrain noise and the flat model colours with CC0 photo textures from Poly Haven
and the rock and supply pile boxes with Poly Haven models; with Blender (BLENDER or PATH) structures,
vehicles and the soldier are built in Blender with Poly Haven textures. Source resolution default 2k;
needs Pillow, downloads and builds once into a cache (see gen_polyhaven.py). Game logic and maps stay
the same.

--superwaffe gives each side a superweapon building (no power, limited by the superweapon option) with two
places in its build list, for tests of the AI with limited superweapons (-superwaffenlimit).

--gegnerarten gives each side an armed tower (the script builds up to five), an aircraft, an anti-air
vehicle, a stealthed infantry unit and a stealth-detecting radar; the script teams field aircraft and
stealthed units. For tests of the enemy model of the new AI (fortified, air, stealth). --ohne-turmskript
leaves the towers out of build list and scripts (they stay buildable), for the base defences of the turtle.

--teamfarbe gives the units (not the structures) a team colour texture (zhca_testunit.tga, alpha as mask), for
the team colour blend in the shader.

--rund makes the units coarse ellipsoids with smooth normals and a white highlight, textured with testround.tga and
its normal map testround_n.dds (round bumps), for the lighting per pixel of the models and their normal maps
(-effects modellight).

--winter writes ProjectData/INI/BodenDetail.ini (loose, also with --big) with detail materials for the test terrain:
grass, earth, rock as snow ground, sand. A map with snow ground, for the flat snow of the rock layer
(-effects terrainrock,flatsnow); run_game.sh links the rest of ProjectData from the build.

--leuchtfeuer gives each side a beacon (BeaconName), for the beacons between the new AI and a human partner.

--wiederaufbau gives each side an outpost in its build list (built at the start, automatic, three rebuilds)
that falls down by itself after a minute, for the rebuild of the classic AI. --wiederaufbau-loch also makes the
outpost leave a rebuild hole (as GLA buildings do) whose own worker rebuilds it.
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import gen_assets  # noqa: E402
import gen_logic  # noqa: E402
import gen_map  # noqa: E402
import gen_ui  # noqa: E402
import gen_ui_art  # noqa: E402
import gen_wnd  # noqa: E402

WINTER = False

# --winter: detail materials of the test terrain (WorldHeightMap.cpp, TERRAIN_DETAIL_MATERIALS)
WINTER_GROUND = {'TestGrass': 'gras', 'TestDirt': 'erde', 'TestRock': 'schnee', 'TestSand': 'sand'}
from gen_util import big_archive, write_bytes, write_text  # noqa: E402


def build(root, big=None, polyhaven=None):
    """Writes everything below root; returns the list of relative paths.

    With big (an archive name such as "TestDataZH.big") the files go into that archive instead of
    loose files, except what the engine must find as a file (Maps/MapCache.ini is read from the folder)."""
    written = []
    packed = []

    def text(rel, content, crlf=True):
        if big:
            packed.append((rel, content.replace('\r\n', '\n').replace('\n', '\r\n').encode('latin-1', 'replace') if crlf else content.encode('latin-1', 'replace')))
        else:
            write_text(root, rel, content, crlf)
        written.append(rel)

    def binary(rel, content):
        if big:
            packed.append((rel, content))
        else:
            write_bytes(root, rel, content)
        written.append(rel)

    # INI: interface, audio, language, terrain, water; a file of the game logic replaces an empty one
    logic = gen_logic.logic_files()
    for rel, content in sorted(gen_ui.ini_files().items()):
        if rel not in logic:
            text(rel, content)
    # INI: game logic
    for rel, content in sorted(logic.items()):
        text(rel, content)
    text('Data/INI/Default/GameData.ini', gen_logic.game_data())
    text('Data/INI/GameData.ini', '; overrides of Default/GameData.ini: none\n')
    text('Data/INI/CommandButton.ini', gen_logic.command_buttons())
    text('Data/INI/CommandSet.ini', gen_logic.command_sets())
    text('Data/INI/Multiplayer.ini', gen_logic.multiplayer())
    text('Data/INI/MappedImages/TextureSize_512/TestUI.ini', gen_ui_art.mapped_images())
    # texts (the engine reads Data/Generals.str when it exists)
    text('Data/Generals.str', gen_logic.strings())
    # windows
    for rel, content in sorted(gen_wnd.window_files().items()):
        text('Window/' + rel, content)
    # textures and models
    built = None
    if polyhaven:
        import gen_polyhaven
        built = gen_polyhaven.built_dir(polyhaven)
        art = gen_ui_art.full(built, gen_polyhaven.diffuse('metal_plate_02', polyhaven))
        terrain = gen_polyhaven.terrain_textures(polyhaven)
        for rel, content in sorted(gen_polyhaven.model_textures(polyhaven).items()):
            binary(rel, content)
        write_text(root, 'POLYHAVEN_CC0.txt', gen_polyhaven.credits(), False)
    else:
        art = gen_ui_art.basic()
        terrain = gen_assets.terrain_textures()
    for rel, content in sorted(terrain.items()):
        binary(rel, content)
    for rel, content in sorted(gen_assets.other_textures().items()):
        binary(rel, content)
    if gen_assets.ROUND:
        for rel, content in sorted(gen_assets.round_textures().items()):
            binary(rel, content)
    for rel, content in sorted(art.items()):
        binary(rel, content)
    binary(gen_ui.MUSIC_FILE, gen_assets.silent_wav())
    models = gen_assets.models(bool(polyhaven))
    if polyhaven:
        models.update(gen_polyhaven.prop_models(polyhaven))
        if built:
            models.update(gen_polyhaven.built_models(built))
    for rel, content in sorted(models.items()):
        binary(rel, content)
    # maps and the map list
    cache = ['; MapCache of the test data set\n']
    for name, maker, tag, players in (('TestSkirmish', gen_map.skirmish_map, 'MAP:TestSkirmish', 2),
                                      ('TestVersus', gen_map.versus_map, 'MAP:TestVersus', 2),
                                      ('TestWeit', gen_map.wide_map, 'MAP:TestWeit', 8),
                                      ('TestShell', gen_map.shell_map, 'MAP:TestShell', 0)):
        data, info = maker()
        rel = 'Maps/%s/%s.map' % (name, name)
        binary(rel, data)
        if 'preview' in info:
            binary('Maps/%s/%s.tga' % (name, name), info['preview'])
        cache.append(gen_map.map_cache_entry(rel, data, info, tag, players))
    text('Maps/MapCache.ini', '\n'.join(cache))
    if WINTER:
        lines = ['; BodenDetail.ini of the test data set (--winter): test terrain -> detail material']
        lines += ['%-24s = %s' % (name.lower() + '.tga', WINTER_GROUND[name]) for name, _, _, _ in gen_ui.TERRAIN]
        write_text(root, 'ProjectData/INI/BodenDetail.ini', '\n'.join(lines) + '\n')
        written.append('ProjectData/INI/BodenDetail.ini')
    if big:
        write_bytes(root, big, big_archive(sorted(packed)))
    return written


def main():
    ap = argparse.ArgumentParser(usage=__doc__.strip().splitlines()[2].strip())
    ap.add_argument('root')
    ap.add_argument('--big')
    ap.add_argument('--polyhaven', nargs='?', const='2k', choices=('1k', '2k', '4k'))
    ap.add_argument('--superwaffe', action='store_true')
    ap.add_argument('--gegnerarten', action='store_true')
    ap.add_argument('--ohne-turmskript', action='store_true')
    ap.add_argument('--leuchtfeuer', action='store_true')
    ap.add_argument('--wiederaufbau', action='store_true')
    ap.add_argument('--wiederaufbau-loch', action='store_true')
    ap.add_argument('--teamfarbe', action='store_true')
    ap.add_argument('--winter', action='store_true')
    ap.add_argument('--rund', action='store_true')
    ap.add_argument('--kameraskripte', action='store_true',
                    help='Skirmish map with camera scripts on the civilian side (gen_map.camera_scripts)')
    a = ap.parse_args()
    gen_map.CAMERA_SCRIPTS = a.kameraskripte
    if a.superwaffe:
        gen_logic.enable_superweapon()
    if a.gegnerarten:
        gen_logic.enable_variety()
    if a.ohne_turmskript:
        gen_logic.TOWER_SCRIPT = False
    if a.leuchtfeuer:
        gen_logic.BEACONS = True
    if a.teamfarbe:
        gen_assets.TEAM_COLOUR = True
    if a.rund:
        gen_assets.ROUND = True
    if a.winter:
        global WINTER
        WINTER = True
    if a.wiederaufbau:
        gen_logic.enable_rebuild()
    if a.wiederaufbau_loch:
        gen_logic.enable_rebuild_hole()
    root, big = a.root, a.big
    files = build(root, big, a.polyhaven)
    if big:
        print('%d Dateien im Archiv %s' % (len(files), os.path.join(root, big)))
    else:
        size = sum(os.path.getsize(os.path.join(root, *f.split('/'))) for f in files)
        print('%d Dateien (%d KB) nach %s' % (len(files), size // 1024, root))
    return 0


if __name__ == '__main__':
    sys.exit(main())
