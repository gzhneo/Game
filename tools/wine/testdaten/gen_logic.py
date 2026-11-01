# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
"""Game logic INI files of the test data set: two invented sides, their buildings and units.

All names and numbers are made up for this data set. Field and keyword names are the ones the INI
parsers of the engine define; the values are small and simple. Each side has the same set of
objects (headquarters with supply dock, power plant, barracks, factory; dozer, collector, rifleman,
tank), only the names and colours differ.
"""

SIDES = [
    # side name (Object "Side" value), template name, display name, colour (r, g, b)
    ('TestAlpha', 'FactionTestAlpha', 'Alpha', (60, 110, 200)),
    ('TestBravo', 'FactionTestBravo', 'Bravo', (200, 80, 60)),
]

# With SUPERWEAPON (generate.py --superwaffe) each side gets a superweapon building without a power
# (FS_SUPERWEAPON, limited by the superweapon option) and two places for it in its build list; for tests
# of the AI with limited superweapons. Off by default, so the usual data set stays the same.
SUPERWEAPON = False
SUPERWEAPON_PARTS = [(0, 0, 10, 30, 30, 20), (0, 0, 26, 8, 8, 12)]


def enable_superweapon():
    global SUPERWEAPON
    SUPERWEAPON = True
    MODEL_PARTS['Superweapon'] = SUPERWEAPON_PARTS


# With REBUILD (generate.py --wiederaufbau) each side has an outpost in its build list (built at the start,
# automatic, three rebuilds) that falls down by itself after a minute, for tests of the rebuild of the
# classic AI.
REBUILD = False


def enable_rebuild():
    global REBUILD
    REBUILD = True
    MODEL_PARTS['Outpost'] = [(0, 0, 5, 14, 14, 10)]


# With REBUILD_HOLE (generate.py --wiederaufbau-loch) the outpost leaves a rebuild hole when it falls down, as GLA
# buildings do: the hole sends a worker of its own after a few seconds, which rebuilds the outpost.
REBUILD_HOLE = False


def enable_rebuild_hole():
    global REBUILD_HOLE
    enable_rebuild()
    REBUILD_HOLE = True
    MODEL_PARTS['Hole'] = [(0, 0, 1, 16, 16, 2)]
    MODEL_PARTS['Worker'] = [(0, 0, 3, 12, 8, 6), (0, 0, 8, 6, 6, 4)]


# With VARIETY (generate.py --gegnerarten) each side also gets an armed tower (base defence), an aircraft,
# an anti-air vehicle, an infantry unit with innate stealth and a radar that detects stealth; the script
# teams field them. For tests of the enemy model of the new AI. Off by default.
VARIETY = False
TOWER_SCRIPT = True     # with VARIETY: build list and script order the towers
BEACONS = False         # a beacon object per side (BeaconName of the player template)
VARIETY_PARTS = {
    'Tower': [(0, 0, 10, 14, 14, 20), (0, 0, 24, 8, 8, 8)],
    'Flyer': [(0, 0, 3, 18, 6, 5), (0, 0, 7, 4, 22, 1)],
    'Flak': [(0, 0, 3, 18, 11, 6), (0, 0, 9, 6, 6, 6)],
    'Shade': [(0, 0, 5, 3, 3, 10), (0, 0, 11, 3, 3, 3)],
    'Radar': [(0, 0, 6, 24, 24, 12), (0, 0, 16, 4, 4, 8)],
    'Seeker': [(0, 0, 3, 14, 9, 5), (0, 0, 8, 2, 2, 6)],
}
VARIETY_UNITS = ('Flyer', 'Flak', 'Shade', 'Seeker')
VARIETY_BUILDINGS = ('Tower', 'Radar')


def enable_variety():
    global VARIETY
    VARIETY = True
    MODEL_PARTS.update(VARIETY_PARTS)


# Model names of the generated W3D files: name -> (parts, size class)
def model_name(side, part):
    # W3D mesh and container names hold 16 bytes: a long name is cut and the asset is not found
    short = {'Headquarters': 'HQ', 'PowerPlant': 'Power'}.get(part, part)
    return 'T%s_%s' % (side[4], short)


# Object parts: id, model boxes (cx, cy, cz, sx, sy, sz); z up, lowest point at 0
MODEL_PARTS = {
    'Headquarters': [(0, 0, 8, 56, 56, 16), (0, 0, 22, 28, 28, 12), (0, 0, 32, 8, 8, 8)],
    'PowerPlant': [(0, 0, 8, 34, 34, 16), (0, 0, 22, 14, 14, 12)],
    'Barracks': [(0, 0, 7, 44, 30, 14), (0, 0, 17, 36, 22, 6)],
    'Factory': [(0, 0, 9, 52, 40, 18), (10, 0, 21, 20, 24, 6)],
    'Dozer': [(0, 0, 3, 16, 10, 6), (-2, 0, 8, 8, 8, 4), (5, 0, 4, 3, 12, 8)],
    'Collector': [(0, 0, 3, 18, 9, 6), (-5, 0, 8, 8, 8, 6), (6, 0, 5, 6, 9, 4)],
    'Rifleman': [(0, 0, 5, 3, 3, 10), (0, 0, 11, 3, 3, 3)],
    'Tank': [(0, 0, 3, 20, 12, 6), (0, 0, 8, 10, 8, 4), (9, 0, 8, 12, 2, 2)],
    'SupplyPile': [(0, 0, 6, 26, 26, 12), (0, 0, 15, 14, 14, 6)],
}
NEUTRAL_MODELS = ['SupplyPile', 'RallyMarker', 'Debris']

# The rifleman is a skin with four bones (root with the legs, torso with the head, two arms) and a
# looping animation: exercises the skinning path of the renderer.
RIFLEMAN_PIVOTS = [(0, 0, 0), (0, 0, 6), (0, 2.2, 9.5), (0, -2.2, 9.5)]
RIFLEMAN_PARENTS = [-1, 0, 1, 1]
RIFLEMAN_BOXES = [(0, 0, 3, 2, 2.5, 6), (0, 0, 8, 2.5, 3, 5), (0, 0, 11.5, 2, 2, 2),
                  (0, 2.2, 7.5, 1, 1, 4.5), (0, -2.2, 7.5, 1, 1, 4.5)]
RIFLEMAN_BONES = [0, 1, 1, 2, 3]


def header(name):
    return '; %s -- test data set of tools/wine/testdaten (own values, no EA content)\n\n' % name


def game_data():
    lines = [header('GameData'), 'GameData']
    add = lambda *a: lines.extend('  ' + x for x in a)
    add('Windowed = Yes', 'XResolution = 800', 'YResolution = 600',
        'UseTrees = Yes', 'UseFPSLimit = Yes', 'FramesPerSecondLimit = 30', 'MaxShellScreens = 8',
        'UseCloudMap = No', 'UseLightMap = No', 'BilinearTerrainTex = Yes', 'TrilinearTerrainTex = No',
        'MultiPassTerrain = Yes', 'AdjustCliffTextures = No', 'Use3WayTerrainBlends = 0',
        'StretchTerrain = No', 'UseHalfHeightMap = No', 'DrawEntireTerrain = No',
        'RightMouseAlwaysScrolls = No', 'UseWaterPlane = Yes', 'UseCloudPlane = No',
        'UseShadowVolumes = No', 'UseShadowDecals = No', 'UseBehindBuildingMarker = No',
        'ShowSoftWaterEdge = Yes', 'DownwindAngle = -0.785',
        'CameraPitch = 37.5', 'CameraYaw = 0.0', 'CameraHeight = 0.0', 'MaxCameraHeight = 310.0',
        'MinCameraHeight = 100.0', 'TerrainHeightAtEdgeOfMap = 0.0',
        'UnitDamagedThreshold = 0.5', 'UnitReallyDamagedThreshold = 0.1',
        'GroundStiffness = 0.5', 'StructureStiffness = 0.5', 'Gravity = -1.0',
        'StealthFriendlyOpacity = 50%', 'DefaultOcclusionDelay = 3000',
        'PartitionCellSize = 40.0', 'HistoricDamageLimit = 4000', 'MaxTerrainTracks = 100',
        'TimeOfDay = AFTERNOON', 'Weather = NORMAL', 'MakeTrackMarks = No', 'HideGarrisonFlags = No',
        'ForceModelsToFollowTimeOfDay = Yes', 'ForceModelsToFollowWeather = Yes',
        'NumberGlobalLights = 1')
    for tod, amb, dif in (('Morning', (110, 110, 120), (190, 180, 160)), ('Afternoon', (120, 120, 120), (220, 210, 190)),
                          ('Evening', (110, 90, 90), (180, 130, 100)), ('Night', (50, 50, 80), (70, 70, 110))):
        for kind in ('TerrainLighting', 'TerrainObjectsLighting'):
            add('%s%sAmbient = R:%d G:%d B:%d' % (kind, tod, *amb),
                '%s%sDiffuse = R:%d G:%d B:%d' % (kind, tod, *dif),
                '%s%sLightPos = X:-0.5 Y:0.5 Z:-0.8' % (kind, tod))
        add('InfantryLight%sScale = 1.5' % tod)
    add('MaxTranslucentObjects = 512', 'OccludedColorLuminanceScale = 0.5',
        'MaxRoadSegments = 500', 'MaxRoadVertex = 3000', 'MaxRoadIndex = 5000', 'MaxRoadTypes = 8',
        'ValuePerSupplyBox = 100', 'ParticleScale = 1.0', 'MaxParticleCount = 2500',
        'MaxFieldParticleCount = 30',
        'BuildSpeed = 1.0', 'MinDistFromEdgeOfMapForBuild = 20.0', 'SupplyBuildBorder = 20.0',
        'AllowedHeightVariationForBuilding = 10.0', 'MinLowEnergyProductionSpeed = 0.5',
        'MaxLowEnergyProductionSpeed = 0.9', 'LowEnergyPenaltyModifier = 0.5', 'MultipleFactory = 0.85',
        'RefundPercent = 50%', 'CommandCenterHealRange = 300.0', 'CommandCenterHealAmount = 0.5',
        'StandardMinefieldDensity = 0.01', 'StandardMinefieldDistance = 40.0',
        'MaxLineBuildObjects = 50', 'MaxTunnelCapacity = 10',
        'HorizontalScrollSpeedFactor = 1.0', 'VerticalScrollSpeedFactor = 1.0',
        'ScrollAmountCutoff = 10.0', 'CameraAdjustSpeed = 0.1', 'EnforceMaxCameraHeight = Yes',
        'KeyboardScrollSpeedFactor = 0.5', 'KeyboardDefaultScrollSpeedFactor = 0.5',
        'KeyboardCameraRotateSpeed = 0.1',
        'DefaultStructureRubbleHeight = 8.0', 'SellPercentage = 50%',
        'ShellMapName = Maps\\TestShell\\TestShell.map', 'ShellMapOn = Yes', 'MoveHintName = TestMoveHint', 'PlayIntro = No',
        'DefaultStartingCash = 5000',
        'ShroudColor = R:0 G:0 B:0', 'ClearAlpha = 255', 'FogAlpha = 127', 'ShroudAlpha = 0',
        'PowerBarBase = 7', 'PowerBarIntervals = 3.0', 'PowerBarYellowRange = 5',
        'UnlookPersistDuration = 100', 'GroupSelectMinSelectSize = 5', 'MaxUnitSelectSounds = 8',
        'CameraAudibleRadius = 500.0', 'MaxShakeIntensity = 10.0', 'MaxShakeRange = 150.0',
        'MaxShellScreens = 8')
    lines.append('End\n')
    return '\n'.join(lines)


def weapons():
    out = [header('Weapon')]
    for name, dmg, rng, delay, dtype in (('TestRifle', 14, 100, 500, 'SMALL_ARMS'), ('TestCannon', 60, 140, 1000, 'ARMOR_PIERCING')):
        out.append('''Weapon %(n)s
  PrimaryDamage = %(d)d.0
  PrimaryDamageRadius = 0.0
  AttackRange = %(r)d.0
  DamageType = %(t)s
  DeathType = NORMAL
  WeaponSpeed = 999999.0
  DelayBetweenShots = %(dl)d
  ClipSize = 0
  ClipReloadTime = 0
  AcceptableAimDelta = 30
  FireFX = FX_TestMuzzleLight
End
''' % dict(n=name, d=dmg, r=rng, dl=delay, t=dtype))
    if VARIETY:
        # tower (turns on the spot, so any aim is fine), aircraft gun, anti-air gun (air targets only)
        for name, dmg, rng, delay, extra in (('TestTowerGun', 40, 160, 800, '  AcceptableAimDelta = 180\n'),
                                             ('TestRotorGun', 18, 110, 500, '  AcceptableAimDelta = 30\n'),
                                             ('TestFlak', 35, 200, 500, '  AcceptableAimDelta = 30\n  AntiAirborneVehicle = Yes\n  AntiGround = No\n')):
            out.append('''Weapon %s
  PrimaryDamage = %d.0
  PrimaryDamageRadius = 0.0
  AttackRange = %d.0
  DamageType = ARMOR_PIERCING
  DeathType = NORMAL
  WeaponSpeed = 999999.0
  DelayBetweenShots = %d
  ClipSize = 0
  ClipReloadTime = 0
%s  FireFX = FX_TestMuzzleLight
End
''' % (name, dmg, rng, delay, extra))
    # looked up by the AI at start
    for name, dmg in (('VehicleCrashesIntoBuildingWeapon', 100), ('VehicleCrashesIntoNonBuildingWeapon', 50)):
        out.append('''Weapon %s
  PrimaryDamage = %d.0
  PrimaryDamageRadius = 5.0
  AttackRange = 1.0
  DamageType = CRUSH
  DeathType = CRUSHED
  WeaponSpeed = 999999.0
  DelayBetweenShots = 1000
End
''' % (name, dmg))
    return '\n'.join(out)


def armor():
    return header('Armor') + '''Armor DefaultArmor
  Armor = DEFAULT 100%
End

Armor TestStructureArmor
  Armor = DEFAULT 100%
  Armor = SMALL_ARMS 60%
End

Armor TestUnitArmor
  Armor = DEFAULT 100%
  Armor = SMALL_ARMS 70%
End
'''


def locomotor():
    return header('Locomotor') + '''Locomotor TestInfantryLocomotor
  Surfaces = GROUND
  Speed = 60
  SpeedDamaged = 20
  TurnRate = 240
  Acceleration = 100
  Braking = 100
  MinTurnSpeed = 0
  ZAxisBehavior = NO_Z_MOTIVE_FORCE
  Appearance = TWO_LEGS
  StickToGround = Yes
End

Locomotor TestVehicleLocomotor
  Surfaces = GROUND
  Speed = 80
  SpeedDamaged = 25
  TurnRate = 120
  Acceleration = 60
  Braking = 80
  MinTurnSpeed = 5
  ZAxisBehavior = NO_Z_MOTIVE_FORCE
  Appearance = TREADS
  StickToGround = Yes
End

Locomotor TestTruckLocomotor
  Surfaces = GROUND
  Speed = 100
  SpeedDamaged = 30
  TurnRate = 120
  Acceleration = 60
  Braking = 80
  MinTurnSpeed = 5
  ZAxisBehavior = NO_Z_MOTIVE_FORCE
  Appearance = FOUR_WHEELS
  StickToGround = Yes
End
''' + ('''
Locomotor TestFlyerLocomotor
  Surfaces = AIR
  Speed = 120
  SpeedDamaged = 60
  TurnRate = 180
  Acceleration = 80
  Lift = 120
  LiftDamaged = 60
  Braking = 80
  MinTurnSpeed = 0
  PreferredHeight = 60
  PreferredHeightDamping = 0.5
  ZAxisBehavior = SURFACE_RELATIVE_HEIGHT
  Appearance = HOVER
  AllowAirborneMotiveForce = Yes
End
''' if VARIETY else '')


def upgrades_sciences():
    return header('Upgrade') + '''Upgrade DefaultUpgrade
  Type = PLAYER
  BuildTime = 1.0
  BuildCost = 0
End

Upgrade Upgrade_TestArmor
  Type = PLAYER
  BuildTime = 10.0
  BuildCost = 300
  DisplayName = UPGRADE:TestArmor
End
'''


def science():
    return header('Science') + '''Science SCIENCE_TestRank1
  PrerequisiteSciences = None
  SciencePurchasePointCost = 0
  IsGrantable = No
End
'''


def rank():
    out = [header('Rank')]
    points = [0, 100, 300, 600, 1000]
    for i, p in enumerate(points, 1):
        out.append('Rank %d\n  RankName = RANK:%d\n  SkillPointsNeeded = %d\n  SciencePurchasePointsGranted = %d\nEnd\n' % (i, i, p, 0 if i == 1 else 1))
    return '\n'.join(out)


def special_power():
    return header('SpecialPower') + '''SpecialPower DefaultSpecialPower
  Enum = SPECIAL_DAISY_CUTTER
  ReloadTime = 60000
  PublicTimer = No
End
'''


MP_COLORS = [('Blue', (60, 110, 200)), ('Red', (200, 80, 60)), ('Green', (60, 170, 80)), ('Yellow', (220, 200, 60)),
             ('Orange', (230, 130, 40)), ('Purple', (150, 70, 170)), ('Cyan', (60, 190, 200)), ('Grey', (150, 150, 150))]


def multiplayer():
    colors = MP_COLORS
    out = [header('MultiplayerSettings'), '''MultiplayerSettings
  StartCountdownTimer = 5
  MaxBeaconsPerPlayer = 3
  UseShroud = No
  ShowRandomPlayerTemplate = Yes
  ShowRandomStartPos = Yes
  ShowRandomColor = Yes
End
''']
    for name, (r, g, b) in colors:
        out.append('MultiplayerColor Color%s\n  TooltipName = COLOR:%s\n  RGBColor = R:%d G:%d B:%d\n  RGBNightColor = R:%d G:%d B:%d\nEnd\n'
                   % (name, name, r, g, b, r // 2, g // 2, b // 2))
    out.append('MultiplayerStartingMoneyChoice\n  Value = 5000\n  Default = Yes\nEnd\n')
    return '\n'.join(out)


def player_templates():
    out = [header('PlayerTemplate')]
    for side, tmpl, disp, colour in SIDES:
        out.append('''PlayerTemplate %(t)s
  Side = %(s)s
  BaseSide = %(s)s
  PlayableSide = Yes
  DisplayName = SIDE:%(s)s
  StartMoney = 0
  PreferredColor = R:%(r)d G:%(g)d B:%(b)d
  StartingBuilding = %(s)sHeadquarters
  StartingUnit0 = %(s)sDozer
  StartingUnit1 = %(s)sCollector
  StartingUnit2 = %(s)sRifleman
  IntrinsicSciences = SCIENCE_TestRank1
  IntrinsicSciencePurchasePoints = 0
  LoadScreenImage = TestLoadScreen
  ScoreScreenImage = TestLoadScreen
  SideIconImage = Icon_Side_%(s)s
  EnabledImage = Icon_Side_%(s)s
  HeadWaterMark = Icon_Side_%(s)s
  FlagWaterMark = Icon_Side_%(s)s
  ArmyTooltip = TOOLTIP:%(s)s
  Features = FEATURES:%(s)s
%(beacon)sEnd
''' % dict(t=tmpl, s=side, r=colour[0], g=colour[1], b=colour[2], beacon='  BeaconName = %sBeacon\n' % side if BEACONS else ''))
    out.append('''PlayerTemplate FactionObserver
  Side = Observer
  BaseSide = Observer
  PlayableSide = No
  DisplayName = SIDE:Observer
  IsObserver = Yes
  StartMoney = 0
  PreferredColor = R:255 G:255 B:255
End

PlayerTemplate FactionCivilian
  Side = Civilian
  BaseSide = Civilian
  PlayableSide = No
  DisplayName = SIDE:Civilian
  StartMoney = 0
  PreferredColor = R:180 G:180 B:180
End
''')
    return '\n'.join(out)


def superweapon_build_list(side):
    out = ''
    if REBUILD:
        out += '''    Structure %(s)sOutpost
      Name = %(s)sOutpost
      Location = X:60 Y:-90
      Rebuilds = 3
      Angle = 0
      InitiallyBuilt = Yes
      AutomaticallyBuild = Yes
    End
''' % dict(s=side)
    if VARIETY and TOWER_SCRIPT:
        # five tower places around the base; the script SKIRMISH_BUILD_BUILDING builds them
        for k, (x, y) in enumerate(((140, 0), (0, 140), (-140, 0), (0, -140), (100, 100))):
            out += '''    Structure %(s)sTower
      Name = %(s)sTower%(k)d
      Location = X:%(x)d Y:%(y)d
      Rebuilds = 1
      Angle = 0
      InitiallyBuilt = No
      AutomaticallyBuild = Yes
    End
''' % dict(s=side, k=k + 1, x=x, y=y)
    if not SUPERWEAPON:
        return out
    for k, (x, y) in enumerate(((-70, -90), (90, -90))):
        out += '''    Structure %(s)sSuperweapon
      Name = %(s)sSuperweapon%(k)d
      Location = X:%(x)d Y:%(y)d
      Rebuilds = 1
      Angle = 0
      InitiallyBuilt = No
      AutomaticallyBuild = Yes
    End
''' % dict(s=side, k=k + 1, x=x, y=y)
    return out


def ai_data():
    out = [header('AIData'), '''AIData
  StructureSeconds = 1.0
  TeamSeconds = 2.0
  Wealthy = 5000
  Poor = 1500
  ForceIdleMSEC = 1000
  StructuresWealthyRate = 1.5
  TeamsWealthyRate = 1.5
  StructuresPoorRate = 0.6
  TeamsPoorRate = 0.6
  TeamResourcesToStart = 0.1
  GuardInnerModifierAI = 1.1
  GuardOuterModifierAI = 1.333
  GuardInnerModifierHuman = 1.8
  GuardOuterModifierHuman = 2.2
  GuardChaseUnitsDuration = 10000
  GuardEnemyScanRate = 500
  GuardEnemyReturnScanRate = 1000
  SkirmishGroupFudgeDistance = 20.0
  RepulsedDistance = 100.0
  EnableRepulsors = No
  AlertRangeModifier = 0.5
  AggressiveRangeModifier = 1.5
  ForceSkirmishAI = Yes
  RotateSkirmishBases = No
  AttackUsesLineOfSight = No
  AttackIgnoreInsignificantBuildings = No
  AttackPriorityDistanceModifier = 100.0
  MaxRecruitRadius = 500.0
  SkirmishBaseDefenseExtraDistance = 100.0
  WallHeight = 0.0
  MinInfantryForGroup = 3
  MinVehiclesForGroup = 3
  MinDistanceForGroup = 100.0
  DistanceRequiresGroup = 200.0
  MinClumpDensity = 0.5
  InfantryPathfindDiameter = 6
  VehiclePathfindDiameter = 6
  RebuildDelayTimeSeconds = 10
  SupplyCenterSafeRadius = 250.0
  AIDozerBoredRadiusModifier = 2.0
  AICrushesInfantry = No
  MaxRetaliationDistance = 210.0
  RetaliationFriendsRadius = 120.0
''']
    for side, tmpl, disp, colour in SIDES:
        out.append('''  SideInfo %(s)s
    ResourceGatherersEasy = 1
    ResourceGatherersNormal = 1
    ResourceGatherersHard = 2
    BaseDefenseStructure1 = %(s)s%(d)s
  End
''' % dict(s=side, d='Tower' if VARIETY else 'Barracks'))
    for side, tmpl, disp, colour in SIDES:
        # The first entry must be the command center (positions of the others are relative to it).
        out.append('''  SkirmishBuildList %(s)s
    Structure %(s)sHeadquarters
      Name = %(s)sHeadquarters
      Location = X:0 Y:0
      Rebuilds = 0
      Angle = 0
      InitiallyBuilt = Yes
      AutomaticallyBuild = No
    End
    Structure %(s)sPowerPlant
      Name = %(s)sPowerPlant
      Location = X:90 Y:20
      Rebuilds = 1
      Angle = 0
      InitiallyBuilt = Yes
      AutomaticallyBuild = Yes
    End
    Structure %(s)sBarracks
      Name = %(s)sBarracks
      Location = X:20 Y:100
      Rebuilds = 1
      Angle = 0
      InitiallyBuilt = Yes
      AutomaticallyBuild = Yes
    End
    Structure %(s)sFactory
      Name = %(s)sFactory
      Location = X:-90 Y:60
      Rebuilds = 1
      Angle = 0
      InitiallyBuilt = Yes
      AutomaticallyBuild = Yes
    End
%(sw)s  End
''' % dict(s=side, sw=superweapon_build_list(side)))
    out.append('End\n')
    return '\n'.join(out)


def crate():
    return header('CrateData') + '''CrateData TestMoneyCrate
  CreationChance = 0.0
  KilledByType = None
End
'''


def damage_fx():
    return header('DamageFX') + 'DamageFX TestStructureDamageFX\nEnd\n\nDamageFX TestUnitDamageFX\nEnd\n'


def beacon_smoke():
    # BeaconClientUpdate looks up BeaconSmoke<player colour RRGGBB>, falling back to BeaconSmokeFFFFFF
    out = []
    for rgb in [c for _, c in MP_COLORS] + [(255, 255, 255)]:
        out.append('''ParticleSystem BeaconSmoke%02X%02X%02X
  Priority = ALWAYS_RENDER
  IsOneShot = No
  Shader = ALPHA
  Type = PARTICLE
  ParticleName = testsmoke.tga
  Lifetime = 30.00 30.00
  SystemLifetime = 0
  Size = 4.00 4.00
  Alpha1 = 0.80 0.80 0
  Alpha2 = 0.00 0.00 30
  Color1 = R:%d G:%d B:%d 0
  BurstDelay = 10.00 10.00
  BurstCount = 1.00 1.00
  DriftVelocity = X:0.00 Y:0.00 Z:1.00
  VelocityType = SPHERICAL
  VelSpherical = 0.00 0.00
  VolumeType = POINT
End
''' % (rgb[0], rgb[1], rgb[2], rgb[0], rgb[1], rgb[2]))
    return '\n'.join(out)


def particle_systems():
    # Smoke: translucent camera facing particles (sorted, rotating); ring: additive, lying on the ground.
    return header('ParticleSystem') + (beacon_smoke() + '\n' if BEACONS else '') + '''ParticleSystem TestMuzzleSmoke
  Priority = WEAPON_EXPLOSION
  IsOneShot = No
  Shader = ALPHA
  Type = PARTICLE
  ParticleName = testsmoke.tga
  AngleZ = 0.00 6.28
  AngularRateZ = -0.05 0.05
  AngularDamping = 1.00 1.00
  VelocityDamping = 0.98 0.98
  Gravity = 0.00
  Lifetime = 40.00 70.00
  SystemLifetime = 6
  Size = 3.00 5.00
  StartSizeRate = 0.00 0.00
  SizeRate = 0.15 0.30
  SizeRateDamping = 1.00 1.00
  Alpha1 = 0.00 0.00 0
  Alpha2 = 0.60 0.80 6
  Alpha3 = 0.00 0.00 70
  Color1 = R:220 G:220 B:220 0
  Color2 = R:120 G:120 B:120 60
  ColorScale = 0.00 0.00
  BurstDelay = 1.00 1.00
  BurstCount = 2.00 3.00
  InitialDelay = 0.00 0.00
  DriftVelocity = X:0.00 Y:0.00 Z:0.30
  VelocityType = SPHERICAL
  VelSpherical = 0.20 0.50
  VolumeType = POINT
  IsHollow = No
  IsGroundAligned = No
  IsEmitAboveGroundOnly = No
  IsParticleUpTowardsEmitter = No
End

ParticleSystem TestGroundRing
  Priority = WEAPON_EXPLOSION
  IsOneShot = No
  Shader = ADDITIVE
  Type = PARTICLE
  ParticleName = testring.tga
  AngleZ = 0.00 6.28
  AngularRateZ = 0.02 0.04
  AngularDamping = 1.00 1.00
  VelocityDamping = 1.00 1.00
  Gravity = 0.00
  Lifetime = 25.00 25.00
  SystemLifetime = 1
  Size = 4.00 4.00
  StartSizeRate = 0.00 0.00
  SizeRate = 0.60 0.60
  SizeRateDamping = 1.00 1.00
  Alpha1 = 1.00 1.00 0
  Alpha2 = 0.00 0.00 25
  Color1 = R:255 G:180 B:90 0
  Color2 = R:60 G:30 B:10 25
  ColorScale = 0.00 0.00
  BurstDelay = 1.00 1.00
  BurstCount = 1.00 1.00
  InitialDelay = 0.00 0.00
  DriftVelocity = X:0.00 Y:0.00 Z:0.00
  VelocityType = NONE
  VolumeType = POINT
  IsHollow = No
  IsGroundAligned = Yes
  IsEmitAboveGroundOnly = No
  IsParticleUpTowardsEmitter = No
End
'''


def fx_ocl():
    # Muzzle flash as a dynamic light (W3DDynamicLight): lights the terrain around every shot.
    fx = header('FXList') + '''FXList FX_TestNone
End

FXList FX_TestMuzzleLight
  LightPulse
    Color = R:255 G:170 B:80
    Radius = 60
    IncreaseTime = 0
    DecreaseTime = 900
  End
  ParticleSystem
    Name = TestMuzzleSmoke
  End
  ParticleSystem
    Name = TestGroundRing
  End
End
'''
    return fx, header('ObjectCreationList') + 'ObjectCreationList OCL_TestNone\nEnd\n'


def anim_name(model):
    # W3D names hold 15 characters
    return model[:6] + 'Walk'


# Skinned models (see gen_assets.models): model -> looping animation "<hierarchy>.<animation>"
ANIMATED = {model_name(side[0], 'Rifleman'): '%s.%s' % (model_name(side[0], 'Rifleman'), anim_name(model_name(side[0], 'Rifleman')))
            for side in SIDES}


def _draw(model):
    anim = ''
    if model in ANIMATED:
        anim = '      Animation = %s\n      AnimationMode = LOOP\n' % ANIMATED[model]
    return '''  Draw = W3DModelDraw ModuleTag_Draw
    OkToChangeModelColor = Yes
    DefaultConditionState
      Model = %s
%s    End
  End
''' % (model, anim)


def _geom_box(major, minor, height):
    return '''  Geometry = BOX
  GeometryMajorRadius = %.1f
  GeometryMinorRadius = %.1f
  GeometryHeight = %.1f
  GeometryIsSmall = No
''' % (major, minor, height)


def _thing(name, side, lines, model, hp, kind, behaviors='', geom='', armor='TestStructureArmor', weapon=None, locomotor=None):
    """One Object block; lines: plain header fields."""
    out = ['Object %s' % name]
    out += ['  ' + l for l in lines]
    out.append('  KindOf = %s' % kind)
    # every object needs a weapon set the engine can find, an empty one for unarmed things
    out.append('  WeaponSet\n    Conditions = None\n%s  End' % ('    Weapon = PRIMARY %s\n' % weapon if weapon else ''))
    out.append('  ArmorSet\n    Conditions = None\n    Armor = %s\n    DamageFX = %s\n  End' % (armor, 'TestStructureDamageFX' if 'STRUCTURE' in kind else 'TestUnitDamageFX'))
    body = 'StructureBody' if 'STRUCTURE' in kind else 'ActiveBody'
    out.append('  Body = %s ModuleTag_Body\n    MaxHealth = %d.0\n    InitialHealth = %d.0\n  End' % (body, hp, hp))
    out.append(_draw(model).rstrip('\n'))
    if behaviors:
        out.append(behaviors.rstrip('\n'))
    out.append('  Behavior = DestroyDie ModuleTag_Die\n  End')
    if locomotor:
        out.append('  Locomotor = SET_NORMAL %s' % locomotor)
    out.append(geom.rstrip('\n'))
    out.append('End\n')
    return '\n'.join(out)


def _production(exit_at, rally, queue=5):
    return ('  Behavior = ProductionUpdate ModuleTag_Production\n    MaxQueueEntries = %d\n  End\n'
            '  Behavior = DefaultProductionExitUpdate ModuleTag_Exit\n    UnitCreatePoint = X:%.1f Y:0.0 Z:0.0\n'
            '    NaturalRallyPoint = X:%.1f Y:0.0 Z:0.0\n  End\n') % (queue, exit_at, rally)


def objects():
    out = [header('Object')]
    for side, tmpl, disp, colour in SIDES:
        s = side
        # the second side is a little sturdier, so that a match has a winner
        edge = 8 if side == SIDES[1][0] else 1
        common = lambda label, cost, time, vis, extra=(): [
            'DisplayName = OBJECT:%s%s' % (s, label), 'Side = %s' % s, 'BuildCost = %d' % cost,
            'BuildTime = %d.0' % time, 'VisionRange = %d.0' % vis, 'ShroudClearingRange = %d.0' % vis,
            'SelectPortrait = Cameo_%s' % model_name(s, label), 'ButtonImage = Cameo_%s' % model_name(s, label)] + list(extra)
        pre = lambda obj: ['Prerequisites\n    Object = %s%s\n  End' % (s, obj)]
        out.append(_thing('%sHeadquarters' % s, s, common('Headquarters', 1000, 20, 300,
                          ['EditorSorting = STRUCTURE', 'BuildCompletion = PLACED_BY_PLAYER', 'RadarPriority = STRUCTURE', 'CommandSet = %sHeadquartersCommandSet' % s]),
                          model_name(s, 'Headquarters'), 600,
                          'STRUCTURE SELECTABLE IMMOBILE COMMANDCENTER SCORE MP_COUNT_FOR_VICTORY FS_FACTORY FS_SUPPLY_CENTER CAPTURABLE',
                          _production(45, 80) +
                          '  Behavior = SupplyCenterCreate ModuleTag_SupplyCreate\n  End\n'
                          '  Behavior = SupplyCenterDockUpdate ModuleTag_Dock\n    NumberApproachPositions = 5\n  End\n',
                          _geom_box(30, 30, 20)))
        out.append(_thing('%sPowerPlant' % s, s, common('PowerPlant', 300, 4, 150,
                          ['EditorSorting = STRUCTURE', 'BuildCompletion = PLACED_BY_PLAYER', 'RadarPriority = STRUCTURE', 'EnergyProduction = 8'] + pre('Headquarters')),
                          model_name(s, 'PowerPlant'), 200,
                          'STRUCTURE SELECTABLE IMMOBILE FS_POWER SCORE MP_COUNT_FOR_VICTORY CAPTURABLE', '', _geom_box(17, 17, 16)))
        out.append(_thing('%sBarracks' % s, s, common('Barracks', 400, 4, 200,
                          ['EditorSorting = STRUCTURE', 'BuildCompletion = PLACED_BY_PLAYER', 'RadarPriority = STRUCTURE', 'EnergyProduction = -2', 'CommandSet = %sBarracksCommandSet' % s] + pre('Headquarters')),
                          model_name(s, 'Barracks'), 250,
                          'STRUCTURE SELECTABLE IMMOBILE FS_BARRACKS SCORE MP_COUNT_FOR_VICTORY CAPTURABLE',
                          _production(30, 60), _geom_box(22, 15, 14)))
        out.append(_thing('%sFactory' % s, s, common('Factory', 600, 5, 200,
                          ['EditorSorting = STRUCTURE', 'BuildCompletion = PLACED_BY_PLAYER', 'RadarPriority = STRUCTURE', 'EnergyProduction = -3', 'CommandSet = %sFactoryCommandSet' % s] + pre('PowerPlant')),
                          model_name(s, 'Factory'), 300,
                          'STRUCTURE SELECTABLE IMMOBILE FS_WARFACTORY SCORE MP_COUNT_FOR_VICTORY CAPTURABLE',
                          _production(36, 70), _geom_box(26, 20, 18)))
        if REBUILD:
            out.append(_thing('%sOutpost' % s, s, common('Outpost', 200, 3, 150,
                              ['EditorSorting = STRUCTURE', 'BuildCompletion = PLACED_BY_PLAYER', 'RadarPriority = STRUCTURE'] + pre('Headquarters')),
                              model_name(s, 'Outpost'), 200, 'STRUCTURE SELECTABLE IMMOBILE SCORE',
                              '  Behavior = LifetimeUpdate ModuleTag_Lifetime\n    MinLifetime = 60000\n    MaxLifetime = 60000\n  End\n'
                              + ('  Behavior = RebuildHoleExposeDie ModuleTag_HoleDie\n    HoleName = %sHole\n    HoleMaxHealth = 100.0\n  End\n' % s
                                 if REBUILD_HOLE else ''),
                              _geom_box(10, 10, 10)))
        if REBUILD_HOLE:
            out.append(_thing('%sHole' % s, s, ['DisplayName = OBJECT:%sHole' % s, 'Side = %s' % s, 'EditorSorting = STRUCTURE',
                              'VisionRange = 100.0', 'ShroudClearingRange = 100.0', 'RadarPriority = STRUCTURE'],
                              model_name(s, 'Hole'), 100, 'STRUCTURE SELECTABLE IMMOBILE REBUILD_HOLE',
                              '  Behavior = RebuildHoleBehavior ModuleTag_Hole\n    WorkerObjectName = %sWorker\n'
                              '    WorkerRespawnDelay = 5000\n    HoleHealthRegen%%PerSecond = 10%%\n  End\n' % s,
                              _geom_box(10, 10, 2)))
            out.append(_thing('%sWorker' % s, s, common('Worker', 0, 1, 100, ['RadarPriority = UNIT', 'EditorSorting = VEHICLE']),
                              model_name(s, 'Worker'), 150, 'SELECTABLE VEHICLE DOZER',
                              '  Behavior = DozerAIUpdate ModuleTag_AI\n    RepairHealthPercentPerSecond = 2%\n    BoredTime = 5000\n    BoredRange = 150.0\n  End\n'
                              '  Behavior = PhysicsBehavior ModuleTag_Physics\n    Mass = 20.0\n  End\n',
                              _geom_box(8, 5, 6), 'TestUnitArmor', None, 'TestVehicleLocomotor'))
        if SUPERWEAPON:
            out.append(_thing('%sSuperweapon' % s, s, common('Superweapon', 1500, 8, 200,
                              ['EditorSorting = STRUCTURE', 'BuildCompletion = PLACED_BY_PLAYER', 'RadarPriority = STRUCTURE',
                               'EnergyProduction = -2', 'MaxSimultaneousOfType = DeterminedBySuperweaponRestriction'] + pre('Factory')),
                              model_name(s, 'Superweapon'), 500,
                              'STRUCTURE SELECTABLE IMMOBILE FS_SUPERWEAPON SCORE MP_COUNT_FOR_VICTORY CAPTURABLE', '', _geom_box(15, 15, 20)))
        out.append(_thing('%sDozer' % s, s, common('Dozer', 150, 2, 120,
                          ['RadarPriority = UNIT', 'EditorSorting = VEHICLE', 'CommandSet = %sDozerCommandSet' % s] + pre('Headquarters')),
                          model_name(s, 'Dozer'), 250, 'SELECTABLE VEHICLE DOZER SCORE',
                          '  Behavior = DozerAIUpdate ModuleTag_AI\n    RepairHealthPercentPerSecond = 2%\n    BoredTime = 5000\n    BoredRange = 150.0\n  End\n'
                          '  Behavior = PhysicsBehavior ModuleTag_Physics\n    Mass = 40.0\n  End\n',
                          _geom_box(11, 6, 8), 'TestUnitArmor', None, 'TestVehicleLocomotor'))
        out.append(_thing('%sCollector' % s, s, common('Collector', 200, 2, 120,
                          ['RadarPriority = UNIT', 'EditorSorting = VEHICLE'] + pre('Headquarters')),
                          model_name(s, 'Collector'), 200, 'SELECTABLE VEHICLE HARVESTER SCORE',
                          '  Behavior = SupplyTruckAIUpdate ModuleTag_AI\n    MaxBoxes = 3\n    SupplyCenterActionDelay = 1000\n'
                          '    SupplyWarehouseActionDelay = 500\n    SupplyWarehouseScanDistance = 300.0\n  End\n'
                          '  Behavior = PhysicsBehavior ModuleTag_Physics\n    Mass = 40.0\n  End\n',
                          _geom_box(11, 6, 8), 'TestUnitArmor', None, 'TestTruckLocomotor'))
        out.append(_thing('%sRifleman' % s, s, common('Rifleman', 100, 2, 150,
                          ['RadarPriority = UNIT', 'EditorSorting = INFANTRY'] + pre('Barracks')),
                          model_name(s, 'Rifleman'), 100 * edge, 'SELECTABLE INFANTRY CAN_ATTACK SCORE',
                          '  Behavior = AIUpdateInterface ModuleTag_AI\n    AutoAcquireEnemiesWhenIdle = Yes\n  End\n'
                          '  Behavior = PhysicsBehavior ModuleTag_Physics\n    Mass = 5.0\n  End\n',
                          '  Geometry = CYLINDER\n  GeometryMajorRadius = 3.0\n  GeometryHeight = 10.0\n  GeometryIsSmall = Yes\n',
                          'TestUnitArmor', 'TestRifle', 'TestInfantryLocomotor'))
        out.append(_thing('%sTank' % s, s, common('Tank', 350, 3, 180,
                          ['RadarPriority = UNIT', 'EditorSorting = VEHICLE'] + pre('Factory')),
                          model_name(s, 'Tank'), 400 * edge, 'SELECTABLE VEHICLE CAN_ATTACK SCORE',
                          '  Behavior = AIUpdateInterface ModuleTag_AI\n    AutoAcquireEnemiesWhenIdle = Yes\n  End\n'
                          '  Behavior = PhysicsBehavior ModuleTag_Physics\n    Mass = 60.0\n  End\n',
                          _geom_box(11, 6, 8), 'TestUnitArmor', 'TestCannon', 'TestVehicleLocomotor'))
        if VARIETY:
            ai = '  Behavior = AIUpdateInterface ModuleTag_AI\n    AutoAcquireEnemiesWhenIdle = Yes\n  End\n'
            out.append(_thing('%sTower' % s, s, common('Tower', 500, 4, 220,
                              ['EditorSorting = STRUCTURE', 'BuildCompletion = PLACED_BY_PLAYER', 'RadarPriority = STRUCTURE',
                               'MaxSimultaneousOfType = 5'] + pre('Barracks')),
                              model_name(s, 'Tower'), 400, 'STRUCTURE SELECTABLE IMMOBILE FS_BASE_DEFENSE CAN_ATTACK SCORE',
                              ai, _geom_box(8, 8, 20), 'TestStructureArmor', 'TestTowerGun'))
            out.append(_thing('%sRadar' % s, s, common('Radar', 400, 4, 220,
                              ['EditorSorting = STRUCTURE', 'BuildCompletion = PLACED_BY_PLAYER', 'RadarPriority = STRUCTURE',
                               'EnergyProduction = -1'] + pre('Barracks')),
                              model_name(s, 'Radar'), 300, 'STRUCTURE SELECTABLE IMMOBILE SCORE',
                              '  Behavior = StealthDetectorUpdate ModuleTag_Detector\n    DetectionRate = 500\n    DetectionRange = 300.0\n  End\n',
                              _geom_box(13, 13, 14)))
            out.append(_thing('%sFlyer' % s, s, common('Flyer', 400, 4, 200,
                              ['RadarPriority = UNIT', 'EditorSorting = VEHICLE'] + pre('Factory')),
                              model_name(s, 'Flyer'), 220, 'SELECTABLE VEHICLE AIRCRAFT CAN_ATTACK SCORE',
                              ai + '  Behavior = PhysicsBehavior ModuleTag_Physics\n    Mass = 20.0\n  End\n',
                              _geom_box(10, 4, 6), 'TestUnitArmor', 'TestRotorGun', 'TestFlyerLocomotor'))
            out.append(_thing('%sFlak' % s, s, common('Flak', 300, 3, 200,
                              ['RadarPriority = UNIT', 'EditorSorting = VEHICLE'] + pre('Factory')),
                              model_name(s, 'Flak'), 250, 'SELECTABLE VEHICLE CAN_ATTACK SCORE',
                              ai + '  Behavior = PhysicsBehavior ModuleTag_Physics\n    Mass = 50.0\n  End\n',
                              _geom_box(10, 6, 8), 'TestUnitArmor', 'TestFlak', 'TestVehicleLocomotor'))
            out.append(_thing('%sSeeker' % s, s, common('Seeker', 250, 3, 200,
                              ['RadarPriority = UNIT', 'EditorSorting = VEHICLE'] + pre('Factory')),
                              model_name(s, 'Seeker'), 200, 'SELECTABLE VEHICLE CAN_ATTACK SCORE',
                              ai + '  Behavior = PhysicsBehavior ModuleTag_Physics\n    Mass = 30.0\n  End\n'
                              '  Behavior = StealthDetectorUpdate ModuleTag_Detector\n    DetectionRate = 500\n    DetectionRange = 200.0\n  End\n',
                              _geom_box(8, 5, 6), 'TestUnitArmor', 'TestRifle', 'TestVehicleLocomotor'))
            out.append(_thing('%sShade' % s, s, common('Shade', 150, 3, 150,
                              ['RadarPriority = UNIT', 'EditorSorting = INFANTRY'] + pre('Barracks')),
                              model_name(s, 'Shade'), 90, 'SELECTABLE INFANTRY CAN_ATTACK SCORE',
                              ai + '  Behavior = PhysicsBehavior ModuleTag_Physics\n    Mass = 5.0\n  End\n'
                              '  Behavior = StealthUpdate ModuleTag_Stealth\n    StealthDelay = 2500\n    StealthForbiddenConditions = ATTACKING\n'
                              '    InnateStealth = Yes\n  End\n',
                              '  Geometry = CYLINDER\n  GeometryMajorRadius = 3.0\n  GeometryHeight = 10.0\n  GeometryIsSmall = Yes\n',
                              'TestUnitArmor', 'TestRifle', 'TestInfantryLocomotor'))
    out.append(_thing('TestSupplyPile', '', ['DisplayName = OBJECT:TestSupplyPile', 'EditorSorting = STRUCTURE', 'VisionRange = 50.0'],
                      'TestSupplyPile', 100, 'STRUCTURE IMMOBILE SUPPLY_SOURCE SUPPLY_SOURCE_ON_PREVIEW',
                      '  Behavior = SupplyWarehouseCreate ModuleTag_Create\n  End\n'
                      '  Behavior = SupplyWarehouseDockUpdate ModuleTag_Dock\n    NumberApproachPositions = 3\n'
                      '    StartingBoxes = 60\n    DeleteWhenEmpty = No\n  End\n', _geom_box(13, 13, 15)))
    out.append(_thing('TestRock', '', ['DisplayName = OBJECT:TestRock', 'EditorSorting = SHRUBBERY'], 'TestRock', 100,
                      'IMMOBILE OBSTACLE', '', _geom_box(8, 8, 12)))
    for name, model in (('RallyPointMarker', 'TestRallyMarker'), ('GenericDebris', 'TestDebris')):
        out.append('Object %s\n  EditorSorting = MISC_MAN_MADE\n  KindOf = NO_COLLIDE IMMOBILE DRAWABLE_ONLY\n%sEnd\n'
                   % (name, _draw(model)))
    if BEACONS:
        # like the EA beacons: an inactive body (effectively dead, not attackable) and the client update
        # that shows or hides it
        for side, tmpl, disp, colour in SIDES:
            out.append('Object %(s)sBeacon\n  DisplayName = OBJECT:%(s)sBeacon\n  Side = %(s)s\n  EditorSorting = SYSTEM\n'
                       '  KindOf = IMMOBILE SELECTABLE UNATTACKABLE NO_COLLIDE\n  RadarPriority = NOT_ON_RADAR\n'
                       '  WeaponSet\n    Conditions = None\n  End\n'
                       '  ArmorSet\n    Conditions = None\n    Armor = TestStructureArmor\n    DamageFX = TestStructureDamageFX\n  End\n'
                       '  Body = InactiveBody ModuleTag_Body\n  End\n%(d)s'
                       '  ClientUpdate = BeaconClientUpdate ModuleTag_Beacon\n  End\n'
                       '  Geometry = CYLINDER\n  GeometryMajorRadius = 4.0\n  GeometryHeight = 10.0\n  GeometryIsSmall = Yes\nEnd\n'
                       % dict(s=side, d=_draw(model_name(side, 'Rifleman'))))
    return '\n'.join(out)


def default_object():
    return header('Default Object') + '''Object DefaultThingTemplate
  Scale = 1.0
  VisionRange = 100.0
  ShroudClearingRange = 100.0
  RadarPriority = NOT_ON_RADAR
End
'''


def command_buttons():
    out = [header('CommandButton')]

    def btn(name, command, **kw):
        lines = ['CommandButton %s' % name, '  Command = %s' % command]
        for k, v in kw.items():
            lines.append('  %s = %s' % (k, v))
        lines.append('End\n')
        out.append('\n'.join(lines))

    for side, tmpl, disp, colour in SIDES:
        s = side
        for unit in ('Dozer', 'Collector', 'Rifleman', 'Tank') + (VARIETY_UNITS if VARIETY else ()):
            btn('Command_Build%s%s' % (s, unit), 'UNIT_BUILD', Object='%s%s' % (s, unit),
                TextLabel='OBJECT:%s%s' % (s, unit), ButtonImage='Cameo_' + model_name(s, unit), ButtonBorderType='BUILD',
                DescriptLabel='TOOLTIP:Unit')
        for bld in ('PowerPlant', 'Barracks', 'Factory') + (('Superweapon',) if SUPERWEAPON else ()) + (VARIETY_BUILDINGS if VARIETY else ()) + (('Outpost',) if REBUILD else ()):
            btn('Command_Construct%s%s' % (s, bld), 'DOZER_CONSTRUCT', Object='%s%s' % (s, bld),
                TextLabel='OBJECT:%s%s' % (s, bld), ButtonImage='Cameo_' + model_name(s, bld), ButtonBorderType='BUILD',
                DescriptLabel='TOOLTIP:Structure')
    btn('Command_CancelConstruction', 'DOZER_CONSTRUCT_CANCEL', TextLabel='GUI:Cancel', ButtonImage='Icon_Cancel', ButtonBorderType='SYSTEM')
    btn('Command_CancelUnitCreate', 'CANCEL_UNIT_BUILD', TextLabel='GUI:Cancel', ButtonImage='Icon_Cancel', ButtonBorderType='SYSTEM')
    btn('Command_CancelUpgradeCreate', 'CANCEL_UPGRADE', TextLabel='GUI:Cancel', ButtonImage='Icon_Cancel', ButtonBorderType='SYSTEM')
    btn('Command_Stop', 'STOP', TextLabel='GUI:Stop', ButtonImage='Icon_Stop', ButtonBorderType='ACTION')
    btn('Command_AttackMove', 'ATTACK_MOVE', TextLabel='GUI:AttackMove', ButtonImage='Icon_AttackMove', ButtonBorderType='ACTION')
    btn('Command_Sell', 'SELL', TextLabel='GUI:Sell', ButtonImage='Icon_Sell', ButtonBorderType='SYSTEM')
    btn('Command_SetRallyPoint', 'SET_RALLY_POINT', TextLabel='GUI:SetRallyPoint', ButtonImage='Icon_Rally', ButtonBorderType='SYSTEM')
    btn('Command_Evacuate', 'EVACUATE', TextLabel='GUI:Evacuate', ButtonImage='Icon_Exit', ButtonBorderType='SYSTEM')
    btn('Command_TransportExit', 'EXIT_CONTAINER', TextLabel='GUI:Exit', ButtonImage='Icon_Exit', ButtonBorderType='SYSTEM')
    btn('Command_StructureExit', 'EXIT_CONTAINER', TextLabel='GUI:Exit', ButtonImage='Icon_Exit', ButtonBorderType='SYSTEM')
    btn('Command_PlaceBeacon', 'PLACE_BEACON', TextLabel='GUI:Beacon', ButtonImage='Icon_Beacon', ButtonBorderType='SYSTEM')
    for name in ('Communicator', 'Options', 'IdleWorker', 'Beacon', 'GeneralsExperience', 'UpDown', 'BriefingHistory'):
        btn('NonCommand_%s' % name, 'NONE', TextLabel='GUI:%s' % name, ButtonImage='Icon_Generic', ButtonBorderType='SYSTEM')
    return '\n'.join(out)


def command_sets():
    out = [header('CommandSet')]
    for side, tmpl, disp, colour in SIDES:
        s = side
        out.append('CommandSet %sHeadquartersCommandSet\n  1 = Command_Build%sDozer\n  2 = Command_Build%sCollector\n  3 = Command_Build%sRifleman\n  13 = Command_SetRallyPoint\n  14 = Command_Sell\nEnd\n' % (s, s, s, s))
        shade = '  2 = Command_Build%sShade\n' % s if VARIETY else ''
        air = '  2 = Command_Build%sFlyer\n  3 = Command_Build%sFlak\n  4 = Command_Build%sSeeker\n' % (s, s, s) if VARIETY else ''
        out.append('CommandSet %sBarracksCommandSet\n  1 = Command_Build%sRifleman\n%s  13 = Command_SetRallyPoint\n  14 = Command_Sell\nEnd\n' % (s, s, shade))
        out.append('CommandSet %sFactoryCommandSet\n  1 = Command_Build%sTank\n%s  13 = Command_SetRallyPoint\n  14 = Command_Sell\nEnd\n' % (s, s, air))
        extra = '  4 = Command_Construct%sSuperweapon\n' % s if SUPERWEAPON else ''
        if VARIETY:
            extra += '  5 = Command_Construct%sTower\n  6 = Command_Construct%sRadar\n' % (s, s)
        if REBUILD:
            extra += '  7 = Command_Construct%sOutpost\n' % s
        out.append('CommandSet %sDozerCommandSet\n  1 = Command_Construct%sPowerPlant\n  2 = Command_Construct%sBarracks\n  3 = Command_Construct%sFactory\n%s  14 = Command_Stop\nEnd\n' % (s, s, s, s, extra))
    return '\n'.join(out)


def strings():
    """Data/generals.str: labels for the fixed texts of the data set (own wording)."""
    rows = [
        ('GUI:Command&ConquerGenerals', 'Test Data Game'),
        ('GUI:PlayerObserver', 'Observer'),
        ('GUI:Cancel', 'Cancel'), ('GUI:Stop', 'Stop'), ('GUI:AttackMove', 'Attack Move'), ('GUI:Sell', 'Sell'),
        ('GUI:SetRallyPoint', 'Rally Point'), ('GUI:Evacuate', 'Evacuate'), ('GUI:Exit', 'Exit'),
        ('GUI:Beacon', 'Beacon'), ('GUI:Communicator', 'Communicator'), ('GUI:Options', 'Options'),
        ('GUI:IdleWorker', 'Idle Builder'), ('GUI:GeneralsExperience', 'Ranks'), ('GUI:UpDown', 'Toggle'),
        ('GUI:BriefingHistory', 'History'),
        ('TOOLTIP:Unit', 'A unit.'), ('TOOLTIP:Structure', 'A structure.'),
        ('SIDE:Observer', 'Observer'), ('SIDE:Civilian', 'Civilian'),
        ('UPGRADE:TestArmor', 'Armor Upgrade'),
        ('COLOR:Blue', 'Blue'), ('COLOR:Red', 'Red'), ('COLOR:Green', 'Green'), ('COLOR:Yellow', 'Yellow'),
        ('COLOR:Orange', 'Orange'), ('COLOR:Purple', 'Purple'), ('COLOR:Cyan', 'Cyan'), ('COLOR:Grey', 'Grey'),
        ('OBJECT:TestSupplyPile', 'Supply Pile'), ('OBJECT:TestRock', 'Rock'),
        ('MAP:TestSkirmish', 'Test Skirmish'), ('MAP:TestVersus', 'Test Versus'), ('MAP:TestWeit', 'Test Wide'), ('MAP:TestShell', 'Test Shell'),
        ('GUI:Closed', 'Closed'), ('GUI:Open', 'Open'), ('GUI:EasyAI', 'Easy AI'), ('GUI:MediumAI', 'Medium AI'),
        ('GUI:HardAI', 'Hard AI'), ('CONTROLBAR:UnderConstructionDesc', 'Under construction'),
        ('0', '0'), ('Test data', 'Test data'), ('Exit', 'Exit'), ('Options', 'Options'), ('Skirmish', 'Skirmish'),
        ('Multiplayer', 'Multiplayer'), ('SinglePlayer', 'Single Player'), ('GUI:ControlBarMoneyDisplay', '$%d'),
        ('SCIENCE:GeneralName', 'General'), ('LABEL:FORMATION', 'Formation'),
        ('CONTROLBAR:ObsPlayerLabel', '%ls (%ls)'), ('Team:AI', 'AI'),
    ]
    for i in range(10):
        rows.append(('NUMBER:%d' % i, '%d' % i))
    import gen_ui
    rows += sorted(gen_ui.command_labels().items())
    # labels the engine builds at run time (OptionsMenu, KeyboardOptionsMenu, ControlBar)
    for i, text in enumerate(('Off', '2x', '4x', '8x', '16x')):
        rows.append(('GUI:AntiAliasing%d' % i, text))
    for cat in ('CONTROL', 'INFORMATION', 'INTERFACE', 'SELECTION', 'TAUNT', 'TEAM', 'MISC', 'DEBUG'):
        rows.append(('GUI:%s' % cat, cat.capitalize()))
    for i in range(1, 9):
        rows.append(('SCIENCE:Rank%d' % i, 'Rank %d' % i))
    # Team:0 is a player without team (team number -1)
    rows.append(('Team:0', 'No Team'))
    for i in range(1, 9):
        rows.append(('Team:%d' % i, 'Team %d' % i))
    for i in range(1, 6):
        rows.append(('RANK:%d' % i, 'Rank %d' % i))
    for side, tmpl, disp, colour in SIDES:
        rows.append(('SIDE:%s' % side, disp))
        rows.append(('TOOLTIP:%s' % side, 'The %s side.' % disp))
        rows.append(('FEATURES:%s' % side, 'Simple units, simple buildings.'))
        for part in ('Headquarters', 'PowerPlant', 'Barracks', 'Factory', 'Dozer', 'Collector', 'Rifleman', 'Tank') + (('Superweapon',) if SUPERWEAPON else ()) + ((VARIETY_UNITS + VARIETY_BUILDINGS) if VARIETY else ()) + (('Outpost',) if REBUILD else ()) + (('Hole', 'Worker') if REBUILD_HOLE else ()):
            rows.append(('OBJECT:%s%s' % (side, part), '%s %s' % (disp, part)))
    # the engine compares labels without case: GUI:TEAM (category) and GUI:Team (window label) are one label
    have = {label.lower() for label, text in rows}
    for label, text in sorted(_ui_labels().items()):
        if label.lower() not in have:
            have.add(label.lower())
            rows.append((label, text))
    out = ['// Test data set of tools/wine/testdaten: own texts, no EA content\n']
    for label, text in rows:
        out.append('%s\n"%s"\nEND\n' % (label, text))
    return '\n'.join(out)


# labels the engine formats with arguments: the text keeps matching placeholders
FORMAT_TEXTS = {
    'CONTROLBAR:OCLTimerDesc': '%d:%02d', 'CONTROLBAR:OCLTimerDescWithPadding': '%d:%02d',
    'GUI:BookmarkXSet': 'Bookmark %d set', 'GUI:TooManyPlayers': 'This map allows only %d players.',
    'SCIENCE:Rank': 'Rank %d', 'TOOLTIP:Cost': 'Cost: $%d', 'TOOLTIP:ScienceCost': 'Cost: %d points',
    'TOOLTIP:StartPositionN': 'Start position %d', 'TOOLTIP:SupplyWarehouse': 'Supplies left: %d',
    'Network:PlayerLeftGame': '%ls left the game.', 'MapTransfer:CurrentFile': '%ls', 'MapTransfer:Timeout': '%d:%02d',
    'GUI:PlayerNoMap': '%ls does not have the map %ls.', 'LAN:NeedMorePlayers': 'At least %d players are needed.',
    'LAN:TooManyPlayers': 'This map allows only %d players.', 'GUI:ChallengeWinText': '%ls is defeated.',
    'GUI:ChallengeLossText': '%ls wins.', 'GUI:StartingMoneyFormat': '$%d',
    'GUI:AddCash': '+$%d', 'GUI:LoseCash': '-$%d', 'GUI:BeaconPlaced': '%ls placed a beacon.',
    'GUI:ErrorLoadingGame': 'Could not load %ls.', 'GUI:ErrorSavingGame': 'Could not save %ls.',
    'GUI:LocalPlayerNoMap': 'You do not have the map %ls.',
    'GUI:LocalPlayerNoMapWillTransfer': 'You do not have the map %ls; it will be transferred.',
    'GUI:PlayerNoMapWillTransfer': '%ls does not have the map %ls; it will be transferred.',
    'GUI:RallyPointSet': 'Rally point set for %ls.', 'INI:MissingDisplayName': '%s',
    'LAN:GameStartTimerPlural': 'The game starts in %d seconds.', 'LAN:GameStartTimerSingular': 'The game starts in %d second.',
    'LAN:PlayerDropped': '%ls was dropped.', 'GUI:PlayerHasBeenDefeated': '%ls has been defeated.',
}
# hand-written wording; every other label gets its words from the label name
UI_TEXTS = {
    'GUI:QuitPopupTitle': 'Leave Game', 'GUI:QuitPopupMessage': 'Do you really want to leave the game?',
    'GUI:InternetDisconnectionMenuBody1': 'A player is not responding. Wait or drop the player.',
    'GUI:SkirmishGameSetup': 'Skirmish Setup', 'GUI:SaveLoad': 'Save / Load', 'GUI:OK': 'OK',
    'GUI:ReturnToGame': 'Return to Game', 'GUI:ExitToMenu': 'Exit to Main Menu', 'GUI:GameMenu': 'Game Menu',
    'GUI:LimitSuperweapons': 'Limit superweapons', 'GUI:SystemMaps': 'Built-in maps', 'GUI:UserMaps': 'My maps',
    'GUI:CheckAlternateMouse': 'Alternate mouse setup', 'GUI:CheckDoubleClickAttackMove': 'Double click attack move',
    'GUI:Retaliation': 'Units retaliate', 'GUI:CheckLanguageFilter': 'Language filter', 'GUI:CheckSendDelay': 'Send delay',
    'GUI:CheckBoxSaveCamera': 'Save camera in replays', 'GUI:CheckBoxUseCamera': 'Use replay camera',
    'GUI:CheckBoxDrawAnchor': 'Show scroll anchor', 'GUI:CheckBoxMoveAnchor': 'Move scroll anchor',
    'GUI:Check2DShadows': 'Shadow decals', 'GUI:Check3DShadows': 'Shadow volumes',
    'GUI:CheckBehindBuilding': 'Units behind buildings', 'GUI:CheckCloudShadows': 'Cloud shadows',
    'GUI:CheckExtraAnimations': 'Extra animations', 'GUI:CheckGroundLighting': 'Ground lighting',
    'GUI:CheckHeatEffects': 'Heat effects', 'GUI:CheckNoDynamicLOD': 'Fixed detail level',
    'GUI:CheckShowProps': 'Show props', 'GUI:CheckSmoothWater': 'Smooth water', 'GUI:CheckUnlockFPS': 'Unlock frame rate',
    'GUI:OverwriteSave': 'Overwrite this save?', 'GUI:LoadSave': 'Load this save?', 'GUI:DeleteSave': 'Delete this save?',
    'GUI:SaveDescription': 'Description', 'GUI:NULL': '', 'GUI:EACopyright': 'Test data, no EA content',
    'GUI:Command&ConquerGenerals': 'Test Data Game', 'GUI:???': 'Random',
}


def _words(label):
    """Readable words from a label name: "GUI:StartingCash" -> "Starting Cash"."""
    import re
    name = label.split(':', 1)[-1].replace('_', ' ')
    name = re.sub(r'(?<=[a-z])(?=[A-Z0-9])|(?<=[0-9])(?=[A-Za-z])|(?<=[A-Z])(?=[A-Z][a-z])', ' ', name)
    return name.strip() or label


def _ui_labels():
    """label -> text for every label the interface code fetches by a fixed name and every TEXT of the
    window layouts; read from the engine sources and gen_wnd, so new menus get texts automatically."""
    import os
    import re
    import gen_wnd
    root = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..', '..', 'Code'))
    labels = set()
    for sub in ('GameEngine', 'Platform'):
        for base, _dirs, files in os.walk(os.path.join(root, *sub.split('/'))):
            for name in sorted(files):
                if name.endswith('.cpp'):
                    with open(os.path.join(base, name), encoding='latin-1') as f:
                        text = re.sub(r'//[^\n]*', '', f.read())
                    # TheGameText->fetch("...") and TheInGameUI->message("...") (fetches the label itself)
                    labels |= set(re.findall(r'(?:fetch|InGameUI->message)\(\s*"([A-Za-z0-9_:&?]+)"', text))
    for text in gen_wnd.window_files().values():
        labels |= {t for t in re.findall(r'TEXT = "([^"]+)"', text) if ':' in t}
    # the game loads the project's own texts (ProjectData/Text); a label defined here as well would hide them
    with open(os.path.join(root, '..', 'Data', 'Text', 'english.str'), encoding='latin-1') as f:
        labels -= set(re.findall(r'(?m)^([A-Za-z0-9_:&?]+)\s*\n"', f.read()))
    out = {}
    for label in labels:
        out[label] = FORMAT_TEXTS.get(label) or UI_TEXTS.get(label) or _words(label)
    out.update({k: v for k, v in FORMAT_TEXTS.items()})
    return out


def logic_files():
    """rel path -> text of all INI files of the game logic (loaded by the harness check, too)."""
    fx, ocl = fx_ocl()
    files = {
        'Data/INI/Default/Object.ini': default_object(),
        'Data/INI/Object/TestObjects.ini': objects(),
        'Data/INI/Weapon.ini': weapons(),
        'Data/INI/Armor.ini': armor(),
        'Data/INI/Locomotor.ini': locomotor(),
        'Data/INI/Upgrade.ini': upgrades_sciences(),
        'Data/INI/Science.ini': science(),
        'Data/INI/Rank.ini': rank(),
        'Data/INI/SpecialPower.ini': special_power(),
        'Data/INI/PlayerTemplate.ini': player_templates(),
        'Data/INI/AIData.ini': ai_data(),
        'Data/INI/Crate.ini': crate(),
        'Data/INI/DamageFX.ini': damage_fx(),
        'Data/INI/FXList.ini': fx,
        'Data/INI/ParticleSystem.ini': particle_systems(),
        'Data/INI/ObjectCreationList.ini': ocl,
    }
    if BEACONS:
        # the notice of a placed beacon plays these events (and its message); an entry without sounds is enough
        files['Data/INI/SoundEffects.ini'] = header('SoundEffects') + ''.join(
            'AudioEvent %s\n  Volume = 0\n  Type = ui player\nEnd\n\n' % n for n in ('BeaconPlaced', 'BeaconPlacementFailed', 'GUIMessageReceived'))
    return files
