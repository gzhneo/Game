# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
"""Maps of the test data set: skirmish maps for two and for eight players and a small shell map.

The chunk format is the one of Code/Tools/Editors/MapEditor/Model (Chunk.cpp, MapIO.cpp); the reader in the
game is Code/GameEngine/GameLogic/Map and W3DDevice/GameClient/Terrain/WorldHeightMap.cpp.
Heights are computed from smooth noise, the start areas are flattened.
"""
import math
import random

import gen_logic
import gen_ui
from gen_util import ChunkFile, Crc, tga_bytes

CELL = 10.0            # world units per height map cell
HEIGHT_UNIT = 0.625    # world units per height value (MAP_HEIGHT_SCALE)


def heights(width, height, border, starts, seed, flat_radius=22):
    """Height values 0..255: gentle hills, flat around the start positions (world units)."""
    rnd = random.Random(seed)
    phases = [(rnd.random() * 6.28, rnd.random() * 6.28) for _ in range(4)]
    out = bytearray()
    for y in range(height):
        for x in range(width):
            wx, wy = (x - border) * CELL, (y - border) * CELL
            v = 40.0
            for k, (px, py) in enumerate(phases):
                f = 0.012 * (k + 1)
                v += 9.0 / (k + 1) * math.sin(wx * f + px) * math.cos(wy * f * 1.3 + py)
            # flatten near the starts
            for sx, sy in starts:
                d = math.hypot(wx - sx, wy - sy) / (flat_radius * CELL)
                if d < 1.0:
                    t = d * d * (3 - 2 * d)
                    v = 40.0 * (1 - t) + v * t
            out.append(max(0, min(255, int(round(v)))))
    return bytes(out)


def preview_tga(width, height, border, starts, hm, size=128):
    """Map preview for the skirmish menu (Maps/<name>/<name>.tga, 128 x 128, MapUtil getMapPreviewImage):
    the playable area from above, grass and the dirt around the starts as in blend_tile_chunk, shaded by
    the slope towards a light from the upper left. Row 0 is the north edge (largest y)."""
    playable = width - 2 * border
    px = bytearray()
    for row in range(size):
        y = border + int((size - 1 - row) * playable / size)
        for col in range(size):
            x = border + int(col * playable / size)
            wx, wy = (x - border) * CELL, (y - border) * CELL
            near = any(math.hypot(wx - sx, wy - sy) < 130 for sx, sy in starts)
            base = (128, 104, 74) if near else (92, 124, 58)
            h = hm[y * width + x]
            dx = hm[y * width + min(width - 1, x + 1)] - hm[y * width + max(0, x - 1)]
            dy = hm[min(height - 1, y + 1) * width + x] - hm[max(0, y - 1) * width + x]
            shade = max(0.75, min(1.2, 1.0 + 0.025 * (dy - dx) + (h - 40) * 0.003))
            r, g, b = (max(0, min(255, int(c * shade))) for c in base)
            px += bytes((b, g, r))
    return tga_bytes(size, size, bytes(px))


def blend_tile_chunk(cf, width, height, starts, border):
    """BlendTileData: grass everywhere, a dirt patch around each start, no blends."""
    classes = []
    first = 0
    for name, side, base, cls in gen_ui.TERRAIN:
        classes.append((first, side * side, side, name))
        first += side * side
    grass, dirt = classes[0], classes[1]
    tiles = []
    for y in range(height):
        for x in range(width):
            wx, wy = (x - border) * CELL, (y - border) * CELL
            near = any(math.hypot(wx - sx, wy - sy) < 130 for sx, sy in starts)
            f, n, w, _ = dirt if near else grass
            base = f + ((x >> 1) % w) + w * ((y >> 1) % w)
            tiles.append((base << 2) + ((y & 1) << 1) + (x & 1))
    cf.begin('BlendTileData', 8)
    cells = width * height
    cf.i32(cells)
    for arr in (tiles, [0] * cells, [0] * cells, [0] * cells):
        for v in arr:
            cf.out += v.to_bytes(2, 'little', signed=True)
    cf.out += bytes(((width + 7) // 8) * height)          # cliff flags
    cf.i32(first)                                           # numBitmapTiles
    cf.i32(1)                                               # blended tiles: entry 0 only
    cf.i32(1)                                               # cliff infos: entry 0 only
    cf.i32(len(classes))
    for f, n, w, name in classes:
        cf.i32(f)
        cf.i32(n)
        cf.i32(w)
        cf.i32(0)
        cf.ascii(name)
    cf.i32(0)                                               # edge tiles
    cf.i32(0)                                               # edge classes
    cf.end()


def lighting_chunk(cf):
    cf.begin('GlobalLighting', 3)
    cf.i32(1)   # time of day: afternoon
    tods = [((100, 100, 110), (200, 190, 170)), ((120, 120, 120), (230, 220, 200)),
            ((110, 90, 90), (190, 140, 110)), ((50, 50, 80), (70, 70, 110))]
    for amb, dif in tods:
        # terrain[0], objects[0..2], terrain[1], terrain[2]
        for k in range(6):
            a = amb if k in (0, 1) else (0, 0, 0)
            d = dif if k in (0, 1) else (0, 0, 0)
            for v in a:
                cf.f32(v / 255.0)
            for v in d:
                cf.f32(v / 255.0)
            for v in (-0.5, 0.5, -0.8):
                cf.f32(v)
    cf.u32(0xFF404060)   # shadow colour
    cf.end()


# ---------------------------------------------------------------------------------------------
# Scripts (PlayerScriptsList inside SidesList)

class Call:
    def __init__(self, name, params=(), kind=0):
        self.name = name
        self.params = list(params)   # (type, string) with type per Parameter::ParameterType
        self.kind = kind


P_INT = 0
P_REAL = 1
P_TEAM = 3
P_COUNTER = 4
P_SCRIPT = 2
P_WAYPOINT = 7
P_SIDE = 11
P_OBJECT_TYPE = 15
P_COORD3D = 16

# Scripts of the civilian side (a skirmish keeps only that side of the map's own) that move the camera and wait for it (generate.py --kameraskripte): the camera
# calls run after the logic tick, but the condition "camera movement finished" must see a move started in the
# same tick. Each finished move gives the civilian player skill points (part of the logic CRC, money is not),
# so the CRC shows when the condition became true.
CAMERA_SCRIPTS = False


def write_call(cf, chunk, version, call):
    cf.begin(chunk, version)
    cf.i32(call.kind)
    cf.name_key(call.name)
    cf.i32(len(call.params))
    for param in call.params:
        ptype = param[0]
        cf.i32(ptype)
        if ptype == P_COORD3D:      # three reals instead of int, real and string (Parameter::ReadParameter)
            for v in param[1]:
                cf.f32(v)
            continue
        cf.i32(param[2] if len(param) > 2 else 0)
        cf.f32(param[3] if len(param) > 3 else 0.0)
        cf.ascii(param[1])
    cf.end()


def write_script(cf, name, conditions, actions, subroutine=False, one_shot=False):
    cf.begin('Script', 2)
    for text in (name, '', '', ''):
        cf.ascii(text)
    cf.boolean(True)          # active
    cf.boolean(one_shot)
    cf.boolean(True)
    cf.boolean(True)
    cf.boolean(True)          # easy, normal, hard
    cf.boolean(subroutine)
    cf.i32(0)                 # delay
    if conditions:
        cf.begin('OrCondition', 1)
        for c in conditions:
            write_call(cf, 'Condition', 4, c)
        cf.end()
    for a in actions:
        write_call(cf, 'ScriptAction', 2, a)
    cf.end()


def side_scripts(cf, side, hunt_delay=0):
    """The scripts of one skirmish side: production condition and the team's first order. With hunt_delay
    (frames) the hunting teams are only built once the timer HuntDelay, started at the beginning, has run out."""
    cf.begin('ScriptList', 1)
    write_script(cf, '%sProduceAlways' % side, [Call('CONDITION_TRUE')], [])
    if hunt_delay:
        write_script(cf, '%sStartHuntDelay' % side, [Call('CONDITION_TRUE')],
                     [Call('SET_TIMER', [(P_COUNTER, 'HuntDelay'), (P_INT, '', hunt_delay)])], one_shot=True)
        write_script(cf, '%sProduceHunters' % side, [Call('TIMER_EXPIRED', [(P_COUNTER, 'HuntDelay')])], [])
    if gen_logic.SUPERWEAPON:
        # like the EA skirmish scripts: order the superweapon again and again (a priority build of the AI)
        write_script(cf, '%sBuildSuperweapon' % side, [Call('CONDITION_TRUE')],
                     [Call('SKIRMISH_BUILD_BUILDING', [(P_OBJECT_TYPE, '%sSuperweapon' % side)])])
    if gen_logic.VARIETY and gen_logic.TOWER_SCRIPT:
        # towers again and again (MaxSimultaneousOfType ends it at five), as with the superweapon
        write_script(cf, '%sBuildTowers' % side, [Call('CONDITION_TRUE')],
                     [Call('SKIRMISH_BUILD_BUILDING', [(P_OBJECT_TYPE, '%sTower' % side)])])
    # Barracks and factory are part of the build list with InitiallyBuilt = Yes (AIData.ini): the AI places
    # them at the start, so no script has to order them.
    # a script without a condition never runs its actions
    write_script(cf, '%sTeamHunt' % side, [Call('CONDITION_TRUE')], [Call('TEAM_HUNT', [(P_TEAM, '<This Team>')])], subroutine=True)
    cf.end()


def camera_scripts(cf):
    """Civilian side: two camera moves, each followed by skill points once the move has finished."""
    def move(waypoint, seconds):
        return Call('MOVE_CAMERA_TO', [(P_WAYPOINT, waypoint), (P_REAL, '', 0, seconds), (P_REAL, '', 0, 0.0),
                                       (P_REAL, '', 0, 0.0), (P_REAL, '', 0, 0.0)])

    def points(amount):
        return Call('PLAYER_ADD_SKILLPOINTS', [(P_SIDE, 'PlyrCivilian'), (P_INT, '', amount)])
    finished = [Call('CAMERA_MOVEMENT_FINISHED')]
    cf.begin('ScriptList', 1)
    write_script(cf, 'CamStart', [Call('CONDITION_TRUE')], [move('Player_1_Start', 2.0)], one_shot=True)
    write_script(cf, 'CamFirst', finished, [points(10), move('Player_2_Start', 1.5)], one_shot=True)
    write_script(cf, 'CamSecond', finished, [points(20)], one_shot=True)
    cf.end()


def skirmish_map(name='TestSkirmish', starts=((320.0, 320.0), (680.0, 680.0)), piles=None, playable=100, hunt_delay=0):
    """Bytes of Maps/<name>/<name>.map and the values for MapCache.ini. piles(sx, sy) gives the supply piles of a
    start position (default: two piles 150 units away, towards the middle of the map); playable is the edge
    length of the playable area in height map cells; hunt_delay see side_scripts."""
    border = 8
    centre = playable * CELL / 2
    width = height = playable + 2 * border
    starts = list(starts)
    cf = ChunkFile()

    cf.begin('HeightMapData', 4)
    cf.i32(width)
    cf.i32(height)
    cf.i32(border)
    cf.i32(1)
    cf.i32(playable)
    cf.i32(playable)
    hm = heights(width, height, border, starts, 7)
    cf.i32(len(hm))
    cf.out += hm
    cf.end()

    blend_tile_chunk(cf, width, height, starts, border)

    cf.begin('WorldInfo', 1)
    cf.dict([('mapName', 'MAP:%s' % name), ('weather', 0)])
    cf.end()

    # Sides: the neutral player and one skirmish template per side of the game (the engine copies
    # scripts and teams of the side whose template matches the slot's).
    cf.begin('SidesList', 3)
    # The neutral player, the civilian (SidesList keeps a FactionCivilian side when it removes the skirmish
    # templates from the active list; without one the last skirmish template stays as a live player) and
    # one skirmish template per side of the game.
    sides = [[('playerName', ''), ('playerIsHuman', True), ('playerDisplayName', ('u', 'Neutral')),
              ('playerFaction', ''), ('playerAllies', ''), ('playerEnemies', '')],
             [('playerName', 'PlyrCivilian'), ('playerIsHuman', False), ('playerDisplayName', ('u', 'Civilian')),
              ('playerFaction', 'FactionCivilian'), ('playerAllies', ''), ('playerEnemies', '')]]
    for side, tmpl, disp, colour in gen_logic.SIDES:
        sides.append([('playerName', 'Skirmish%s' % side), ('playerIsHuman', False),
                      ('playerDisplayName', ('u', 'Skirmish %s' % disp)), ('playerFaction', tmpl),
                      ('playerAllies', ''), ('playerEnemies', '')])
    cf.i32(len(sides))
    for d in sides:
        cf.dict(d)
        cf.i32(0)                 # no build list; AIData.ini has the skirmish build lists
    teams = [[('teamName', 'team'), ('teamOwner', ''), ('teamIsSingleton', True)],
             [('teamName', 'teamPlyrCivilian'), ('teamOwner', 'PlyrCivilian'), ('teamIsSingleton', True)]]
    for side, tmpl, disp, colour in gen_logic.SIDES:
        owner = 'Skirmish%s' % side
        teams.append([('teamName', 'team%s' % owner), ('teamOwner', owner), ('teamIsSingleton', True)])
        # The collector team orders a truck at the headquarters: the AI's own truck order waits for a unit
        # made there and takes the first one, which then works the supply piles.
        kinds = [('Riflemen', 'Rifleman', 3, 4, 2, True), ('Tanks', 'Tank', 2, 3, 3, True), ('Miners', 'Collector', 1, 1, 9, False)]
        if gen_logic.VARIETY:
            kinds += [('Flyers', 'Flyer', 2, 3, 4, True), ('Shades', 'Shade', 2, 3, 2, True)]
        for tname, unit, lo, hi, prio, hunt in kinds:
            t = [('teamName', 'Team%s%s' % (side, tname)), ('teamOwner', owner), ('teamIsSingleton', False),
                 ('teamMaxInstances', 6),
                 ('teamUnitType1', '%s%s' % (side, unit)), ('teamUnitMinCount1', lo), ('teamUnitMaxCount1', hi),
                 ('teamProductionCondition', ('%sProduceHunters' if hunt and hunt_delay else '%sProduceAlways') % side)]
            if hunt:
                t.append(('teamOnCreateScript', '%sTeamHunt' % side))
            t += [('teamProductionPriority', prio), ('teamAutoReinforce', False)]
            teams.append(t)
    cf.i32(len(teams))
    for t in teams:
        cf.dict(t)
    cf.begin('PlayerScriptsList', 5)
    for d in sides:
        name = dict(d)['playerName']
        if name.startswith('Skirmish'):
            side_scripts(cf, name[len('Skirmish'):], hunt_delay)
        elif name == 'PlyrCivilian' and CAMERA_SCRIPTS:
            camera_scripts(cf)
        else:
            cf.begin('ScriptList', 1)
            cf.end()
    cf.end()
    cf.end()

    # Objects: waypoints, supply piles near each start, some rocks
    cf.begin('ObjectsList', 3)

    def obj(x, y, name, props, z=0.0):
        cf.begin('Object', 3)
        cf.f32(x)
        cf.f32(y)
        cf.f32(z)
        cf.f32(0.0)
        cf.i32(0)
        cf.ascii(name)
        cf.dict(props)
        cf.end()

    wid = 1
    for i, (sx, sy) in enumerate(starts):
        obj(sx, sy, '*Waypoints/Waypoint', [('waypointID', wid), ('waypointName', 'Player_%d_Start' % (i + 1))])
        wid += 1
    obj(centre, centre, '*Waypoints/Waypoint', [('waypointID', wid), ('waypointName', 'InitialCameraPosition')])
    wid += 1
    for sx, sy in starts:
        if piles:
            spots = piles(sx, sy)
        else:
            dx = 1 if sx < centre else -1
            spots = [(sx + dx * ox, sy + dx * oy) for ox, oy in ((150, 20), (20, 150))]
        for px, py in spots:
            obj(px, py, 'TestSupplyPile', [('originalOwner', 'team'), ('objectInitialHealth', 100)])
    rnd = random.Random(3)
    for k in range(int(14 * (playable / 100) ** 2)):
        x, y = rnd.uniform(centre - playable * 2.5, centre + playable * 2.5), rnd.uniform(centre - playable * 2.5, centre + playable * 2.5)
        if all(math.hypot(x - sx, y - sy) > 160 for sx, sy in starts):
            obj(x, y, 'TestRock', [('originalOwner', 'team')])
    # A map light (static scene light of the terrain prelighting): z < 0 puts it the given height above
    # the terrain (W3DTerrainVisual).
    obj(centre + 60.0, centre - 30.0, '*Lights/Light', [('lightHeightAboveTerrain', 25.0), ('lightAmbientColor', 0x201008),
                                         ('lightDiffuseColor', 0xC06020), ('lightInnerRadius', 40.0),
                                         ('lightOuterRadius', 160.0)], z=-1.0)
    cf.end()

    cf.begin('PolygonTriggers', 4)
    cf.i32(0)
    cf.end()

    lighting_chunk(cf)

    cf.begin('WaypointsList', 1)
    cf.i32(0)
    cf.end()

    data = cf.file_bytes()
    return data, {'starts': starts, 'extent': (0.0, 0.0, playable * CELL, playable * CELL),
                  'preview': preview_tga(width, height, border, starts, hm)}


def versus_map():
    """Maps/TestVersus: two start positions 220 units apart, so that each base is in view of the other; for fights
    between two players at the slow pace of the Wine runs. The supply piles lie above and below each base, away
    from the other one."""
    return skirmish_map('TestVersus', ((390.0, 500.0), (610.0, 500.0)), lambda sx, sy: [(sx, sy + 150), (sx, sy - 150)])


def wide_map():
    """Maps/TestWeit: eight start positions on a ring in a map of 2400 x 2400 units, neighbours about 690 units
    apart and opposite ones 1800, for up to eight AIs (-skirmishAI with eight places). The script teams only
    start hunting after six minutes, so that the command layer of the new AI gathers and launches its own
    attacks before the game is decided. Starts 1 and 2 lie
    opposite each other, as do 3 and 4, 5 and 6, 7 and 8. The supply piles lie beside each base, towards the middle."""
    playable = 240
    centre = playable * CELL / 2
    radius = 900.0
    starts = [(centre + radius * math.cos(math.pi / 2 - k * math.pi / 4), centre + radius * math.sin(math.pi / 2 - k * math.pi / 4))
              for k in (0, 4, 2, 6, 1, 5, 3, 7)]

    def piles(sx, sy):
        ux, uy = centre - sx, centre - sy
        n = math.hypot(ux, uy)
        ux, uy = ux / n, uy / n
        out = []
        for a in (0.7, -0.7):
            c, s = math.cos(a), math.sin(a)
            out.append((sx + 150 * (ux * c - uy * s), sy + 150 * (ux * s + uy * c)))
        return out
    return skirmish_map('TestWeit', starts, piles, playable, hunt_delay=6 * 60 * 30)


def shell_map():
    """A small map without players for the shell background (main menu)."""
    border = 8
    playable = 60
    width = height = playable + 2 * border
    cf = ChunkFile()
    cf.begin('HeightMapData', 4)
    cf.i32(width)
    cf.i32(height)
    cf.i32(border)
    cf.i32(1)
    cf.i32(playable)
    cf.i32(playable)
    hm = heights(width, height, border, [(300.0, 300.0)], 11, 30)
    cf.i32(len(hm))
    cf.out += hm
    cf.end()
    blend_tile_chunk(cf, width, height, [(300.0, 300.0)], border)
    cf.begin('WorldInfo', 1)
    cf.dict([('mapName', 'MAP:TestShell'), ('weather', 0)])
    cf.end()
    cf.begin('SidesList', 3)
    cf.i32(2)
    cf.dict([('playerName', ''), ('playerIsHuman', True), ('playerDisplayName', ('u', 'Neutral')),
             ('playerFaction', ''), ('playerAllies', ''), ('playerEnemies', '')])
    cf.i32(0)
    cf.dict([('playerName', 'PlyrCivilian'), ('playerIsHuman', False), ('playerDisplayName', ('u', 'Civilian')),
             ('playerFaction', 'FactionCivilian'), ('playerAllies', ''), ('playerEnemies', '')])
    cf.i32(0)
    cf.i32(2)
    cf.dict([('teamName', 'team'), ('teamOwner', ''), ('teamIsSingleton', True)])
    cf.dict([('teamName', 'teamPlyrCivilian'), ('teamOwner', 'PlyrCivilian'), ('teamIsSingleton', True)])
    cf.begin('PlayerScriptsList', 5)
    for _ in range(2):
        cf.begin('ScriptList', 1)
        cf.end()
    cf.end()
    cf.end()
    cf.begin('ObjectsList', 3)
    for x, y in ((300.0, 300.0), (240.0, 330.0), (350.0, 250.0)):
        cf.begin('Object', 3)
        cf.f32(x)
        cf.f32(y)
        cf.f32(0.0)
        cf.f32(0.0)
        cf.i32(0)
        cf.ascii('TestRock')
        cf.dict([('originalOwner', 'team')])
        cf.end()
    cf.end()
    cf.begin('PolygonTriggers', 4)
    cf.i32(0)
    cf.end()
    lighting_chunk(cf)
    cf.begin('WaypointsList', 1)
    cf.i32(0)
    cf.end()
    return cf.file_bytes(), {'extent': (0.0, 0.0, playable * CELL, playable * CELL)}


def map_cache_entry(rel_path, data, info, tag, players):
    """One MapCache block for Maps/MapCache.ini (the key is the lower case path with backslashes)."""
    lines = ['MapCache %s' % rel_path.lower().replace('/', '\\'),
             '  fileSize = %d' % len(data),
             '  fileCRC = %d' % Crc.of(data),
             '  timestampLo = 0', '  timestampHi = 0',
             '  isOfficial = no', '  isMultiplayer = %s' % ('yes' if players >= 2 else 'no'),
             '  numPlayers = %d' % players,
             '  extentMin = X:%.2f Y:%.2f Z:%.2f' % (info['extent'][0], info['extent'][1], 0.0),
             '  extentMax = X:%.2f Y:%.2f Z:%.2f' % (info['extent'][2], info['extent'][3], 100.0),
             '  nameLookupTag = %s' % tag]
    for i, (x, y) in enumerate(info.get('starts', [])):
        lines.append('  Player_%d_Start = X:%.2f Y:%.2f Z:0.00' % (i + 1, x, y))
    lines.append('END\n')
    return '\n'.join(lines)
