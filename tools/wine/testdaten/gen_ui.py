# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
"""INI files outside the game logic: audio, video, interface, language, detail levels, terrain, water.

The engine loads every file listed here at start and stops with an error dialog if one is missing
(INI::load throws), so the files exist even where they are empty. Values are made up for the
test data set.
"""


def header(name):
    return '; %s -- test data set of tools/wine/testdaten (own values, no EA content)\n\n' % name


EMPTY = '; empty (test data set of tools/wine/testdaten)\n'

# Files that must exist and may stay empty (every INI::load without an existence check).
EMPTY_FILES = [
    'Data/INI/Default/Music.ini', 'Data/INI/Default/SoundEffects.ini', 'Data/INI/Default/Speech.ini',
    'Data/INI/Default/Voice.ini', 'Data/INI/Default/Video.ini', 'Data/INI/Default/Water.ini',
    'Data/INI/Default/Science.ini', 'Data/INI/Default/Multiplayer.ini', 'Data/INI/Default/Terrain.ini',
    'Data/INI/Default/Roads.ini', 'Data/INI/Default/PlayerTemplate.ini', 'Data/INI/Default/FXList.ini',
    'Data/INI/Default/ObjectCreationList.ini', 'Data/INI/Default/SpecialPower.ini', 'Data/INI/Default/Upgrade.ini',
    'Data/INI/Default/AIData.ini', 'Data/INI/Default/Crate.ini', 'Data/INI/Default/CommandButton.ini',
    'Data/INI/Default/ControlBarScheme.ini', 'Data/INI/Default/ShellMenuScheme.ini',
    'Data/INI/GameDataDebug.ini',
    'Data/INI/AudioSettings.ini', 'Data/INI/Music.ini', 'Data/INI/SoundEffects.ini', 'Data/INI/Speech.ini',
    'Data/INI/Voice.ini', 'Data/INI/MiscAudio.ini', 'Data/INI/Video.ini', 'Data/INI/Eva.ini',
    'Data/INI/Campaign.ini', 'Data/INI/Animation2D.ini',
    'Data/INI/WindowTransitions.ini', 'Data/INI/GameLODPresets.ini', 'Data/INI/ControlBarScheme.ini',
    'Data/INI/ControlBarResizer.ini', 'Data/INI/ShellMenuScheme.ini', 'Data/INI/CommandMap.ini',
    'Data/INI/CommandMapDebug.ini', 'Data/INI/Credits.ini', 'Data/INI/DrawGroupInfo.ini',
    'Data/english/HeaderTemplate.ini', 'Data/english/CommandMap.ini',
]


def game_lod():
    out = [header('GameLOD')]
    for name in ('Low', 'Medium', 'High', 'VeryHigh'):
        out.append('StaticGameLOD %s\n  MinimumFPS = 0\n  MaxParticleCount = 1500\n  UseShadowVolumes = No\n  UseShadowDecals = No\n'
                   '  UseCloudMap = No\n  UseLightMap = No\n  ShowSoftWaterEdge = No\n  TextureReductionFactor = 0\nEnd\n' % name)
    for name in ('Low', 'Medium', 'High', 'VeryHigh'):
        out.append('DynamicGameLOD %s\n  MinimumFPS = 0\n  ParticleSkipMask = 0\n  DebrisSkipMask = 0\n  SlowDeathScale = 1.0\nEnd\n' % name)
    return '\n'.join(out)


def language():
    lines = [header('Language'), 'Language']
    for key in ('MessageFont', 'CopyrightFont', 'MilitaryCaptionTitleFont', 'MilitaryCaptionFont',
                'SuperweaponCountdownNormalFont', 'SuperweaponCountdownReadyFont', 'NamedTimerCountdownNormalFont',
                'NamedTimerCountdownReadyFont', 'DrawableCaptionFont', 'DefaultWindowFont', 'DefaultDisplayStringFont',
                'TooltipFontName', 'NativeDebugDisplay', 'DrawGroupInfoFont', 'CreditsTitleFont',
                'CreditsMinorTitleFont', 'CreditsNormalFont'):
        lines.append('  %s = "Arial" 10 No' % key)
    lines += ['  UnicodeFontName = "Arial"', '  MilitaryCaptionSpeed = 1', '  UseHardWordWrap = No',
              '  ResolutionFontAdjustment = 0.7', 'End\n']
    return '\n'.join(lines)


def mouse():
    return header('Mouse') + '''Mouse
  TooltipFontName = Arial
  TooltipFontSize = 10
  TooltipFontIsBold = No
  TooltipAnimateBackground = No
  TooltipFillTime = 50
  TooltipDelayTime = 400
  TooltipTextColor = R:255 G:255 B:255 A:255
  TooltipHighlightColor = R:255 G:255 B:0 A:255
  TooltipShadowColor = R:0 G:0 B:0 A:255
  TooltipBackgroundColor = R:20 G:20 B:30 A:200
  TooltipBorderColor = R:200 G:200 B:200 A:255
  TooltipWidth = 15%
  CursorMode = 0
  OrthoCamera = No
  OrthoZoom = 1.0
  DragTolerance = 12
  DragTolerance3D = 12
  DragToleranceMS = 300
End
'''


def in_game_ui():
    return header('InGameUI') + '''InGameUI
  MaxSelectionSize = 50
  MessageColor1 = R:255 G:255 B:255 A:255
  MessageColor2 = R:180 G:180 B:180 A:255
  MessagePosition = X:10 Y:10
  MessageFont = Arial
  MessagePointSize = 10
  MessageBold = No
  MessageDelayMS = 5000
  MilitaryCaptionColor = R:200 G:200 B:30 A:255
  MilitaryCaptionPosition = X:10 Y:380
  MilitaryCaptionTitleFont = Arial
  MilitaryCaptionTitlePointSize = 12
  MilitaryCaptionTitleBold = Yes
  MilitaryCaptionFont = Arial
  MilitaryCaptionPointSize = 12
  MilitaryCaptionBold = No
  MilitaryCaptionRandomizeTyping = No
  MilitaryCaptionSpeed = 1
  SuperweaponCountdownPosition = X:0.7 Y:0.7
  SuperweaponCountdownNormalFont = Arial
  SuperweaponCountdownNormalPointSize = 14
  SuperweaponCountdownNormalBold = Yes
  SuperweaponCountdownReadyFont = Arial
  SuperweaponCountdownReadyPointSize = 14
  SuperweaponCountdownReadyBold = Yes
  NamedTimerCountdownPosition = X:0.7 Y:0.6
  NamedTimerCountdownNormalFont = Arial
  NamedTimerCountdownNormalPointSize = 14
  NamedTimerCountdownNormalBold = Yes
  NamedTimerCountdownReadyFont = Arial
  NamedTimerCountdownReadyPointSize = 14
  NamedTimerCountdownReadyBold = Yes
  FloatingTextTimeOut = 1000
  FloatingTextMoveUpSpeed = 1.0
  FloatingTextVanishRate = 1.0
  PopupMessageColor = R:255 G:255 B:255 A:255
  DrawableCaptionFont = Arial
  DrawableCaptionPointSize = 10
  DrawableCaptionBold = No
  DrawableCaptionColor = R:255 G:255 B:255 A:255
End
'''


def draw_group_info():
    return header('DrawGroupInfo') + '''DrawGroupInfo
  UsePlayerColor = Yes
  ColorForText = R:255 G:255 B:255 A:255
  ColorForTextDropShadow = R:0 G:0 B:0 A:255
  FontName = "Arial"
  FontSize = 10
  FontIsBold = No
  DropShadowOffsetX = -1
  DropShadowOffsetY = -1
  DrawPositionXPixel = 1
  DrawPositionYPixel = -8
End
'''


def water():
    lines = [header('Water')]
    for tod in ('MORNING', 'AFTERNOON', 'EVENING', 'NIGHT'):
        lines += ['WaterSet %s' % tod, '  SkyTexture = testsky.tga', '  WaterTexture = testwater.tga',
                  '  Vertex00Color = R:255 G:255 B:255', '  Vertex10Color = R:255 G:255 B:255',
                  '  Vertex01Color = R:255 G:255 B:255', '  Vertex11Color = R:255 G:255 B:255',
                  '  DiffuseColor = R:255 G:255 B:255 A:255', '  TransparentDiffuseColor = R:255 G:255 B:255 A:160',
                  '  UScrollPerMS = 0.001', '  VScrollPerMS = 0.001', '  SkyTexelsPerUnit = 0.1',
                  '  WaterRepeatCount = 8', 'End', '']
    lines += ['WaterTransparency', '  TransparentWaterDepth = 3.0', '  TransparentWaterMinOpacity = 0.6',
              '  StandingWaterColor = R:255 G:255 B:255', '  StandingWaterTexture = testwater.tga', 'End', '']
    return '\n'.join(lines)


TERRAIN = [
    # name, texture size in 64 pixel tiles per side, base colour, class
    ('TestGrass', 4, (92, 124, 58), 'GRASS'),
    ('TestDirt', 2, (128, 104, 74), 'DIRT'),
    ('TestRock', 2, (116, 112, 104), 'ROCK'),
    ('TestSand', 2, (196, 176, 128), 'SAND'),
]


def terrain():
    lines = [header('Terrain')]
    for name, side, base, cls in TERRAIN:
        lines += ['Terrain %s' % name, '  Texture = %s.tga' % name.lower(), '  Class = %s' % cls, 'End', '']
    return '\n'.join(lines)


def roads():
    return header('Roads') + '''Road TestRoad
  Texture = testroad.tga
  RoadWidth = 30.0
  RoadWidthInTexture = 0.8
End
'''


def audio_settings():
    return header('AudioSettings') + '''AudioSettings
  AudioRoot = Data\\Audio
  SoundsFolder = Sounds
  MusicFolder = Tracks
  StreamingFolder = Speech
  SoundsExtension = wav
  UseDigital = Yes
  UseMidi = No
  OutputRate = 22050
  OutputBits = 16
  OutputChannels = 2
  SampleCount2D = 8
  SampleCount3D = 16
  StreamCount = 2
  MinSampleVolume = 10%
  GlobalMinRange = 50
  GlobalMaxRange = 400
  TimeBetweenDrawableSounds = 500
  TimeToFadeAudio = 1000
  AudioFootprintInBytes = 1000000
  Relative2DVolume = -20%
  DefaultSoundVolume = 100%
  Default3DSoundVolume = 100%
  DefaultSpeechVolume = 100%
  DefaultMusicVolume = 100%
  MicrophoneDesiredHeightAboveTerrain = 100.0
  MicrophoneMaxPercentageBetweenGroundAndCamera = 50%
  ZoomMinDistance = 30.0
  ZoomMaxDistance = 300.0
  ZoomSoundVolumePercentageAmount = 20%
End
'''


# The engine ends the start when no music track file exists (AudioManager::isMusicAlreadyLoaded).
MUSIC_FILE = 'Data/Audio/Tracks/testtrack.wav'


def music():
    return header('Music') + 'MusicTrack TestTrack\n  Filename = testtrack.wav\n  Volume = 100%\nEnd\n'


# Every group name the engine asks for; findGroup gives null for an unknown name and the callers
# (reverse, remove, skip) dereference it.
TRANSITION_GROUPS = (
    'ChallengeButtonsIntro ChallengeMenuFade ControlBarArrow FadeWholeScreen GenExpFade LanGameOptionsFade '
    'LanLobbyFade MainMenuDefaultMenu MainMenuDefaultMenuBack MainMenuDefaultMenuLogoFade '
    'MainMenuDifficultyMenuBack MainMenuDifficultyMenuChina MainMenuDifficultyMenuChinaBack '
    'MainMenuDifficultyMenuGLA MainMenuDifficultyMenuGLABack MainMenuDifficultyMenuTraining '
    'MainMenuDifficultyMenuTrainingBack MainMenuDifficultyMenuUS MainMenuDifficultyMenuUSBack '
    'MainMenuFactionChina MainMenuFactionGLA MainMenuFactionSkirmish MainMenuFactionTraining '
    'MainMenuFactionUS MainMenuFade MainMenuLoadReplayMenu MainMenuLoadReplayMenuBack '
    'MainMenuLoadReplayMenuBackTransition MainMenuMultiPlayerMenu MainMenuMultiPlayerMenuReverse '
    'MainMenuMultiPlayerMenuTransitionToNext MainMenuSinglePlayerChinaMenuFromDiff '
    'MainMenuSinglePlayerGLAMenuFromDiff MainMenuSinglePlayerMenu MainMenuSinglePlayerMenuBack '
    'MainMenuSinglePlayerMenuBackChina MainMenuSinglePlayerMenuBackGLA MainMenuSinglePlayerMenuBackSkirmish '
    'MainMenuSinglePlayerMenuBackTraining MainMenuSinglePlayerMenuBackUS '
    'MainMenuSinglePlayerTrainingMenuFromDiff MainMenuSinglePlayerUSAMenuFromDiff NetworkDirectConnectFade '
    'QuitFull QuitFullBack QuitNoSave QuitNoSaveBack ReplayMenuFade SaveLoadMenuFade ScoreScreenShow '
    'SkirmishGameOptionsMenuFade').split()


# windows a transition group shows: the menus hide them in their init and wait for the group
# (SkirmishGameOptionsMenuInit hides SubParent, SkirmishGameOptionsMenuUpdate starts the group)
TRANSITION_WINDOWS = {
    'SkirmishGameOptionsMenuFade': [('SkirmishGameOptionsMenu.wnd:SubParent', 'WINFADE')],
    'LanLobbyFade': [('LanLobbyMenu.wnd:GadgetParent', 'WINFADE')],
    # main menu groups (MainMenuSystem): forward shows a group, reverse hides it
    'MainMenuDefaultMenu': [('MainMenu.wnd:MapBorder2', 'WINFADE')],
    'MainMenuDefaultMenuBack': [('MainMenu.wnd:MapBorder2', 'WINFADE')],
    # MainMenuUpdate starts it on every return to the main menu (from skirmish, the LAN lobby, ...)
    'MainMenuDefaultMenuLogoFade': [('MainMenu.wnd:MapBorder2', 'WINFADE')],
    'MainMenuMultiPlayerMenu': [('MainMenu.wnd:MapBorder1', 'WINFADE')],
    'MainMenuMultiPlayerMenuReverse': [('MainMenu.wnd:MapBorder1', 'WINFADE')],
    'MainMenuMultiPlayerMenuTransitionToNext': [('MainMenu.wnd:MapBorder1', 'WINFADE')],
    # game menu: ToggleQuitMenu shows it with QuitNoSave/QuitFull and hides it only through the reverse of *Back
    'QuitNoSave': [('QuitNoSave.wnd:QuitMenuParent', 'WINFADE')],
    'QuitNoSaveBack': [('QuitNoSave.wnd:QuitMenuParent', 'WINFADE')],
    'QuitFull': [('QuitMenu.wnd:QuitMenuParent', 'WINFADE')],
    'QuitFullBack': [('QuitMenu.wnd:QuitMenuParent', 'WINFADE')],
}


def window_transitions():
    text = header('WindowTransitions')
    for name in TRANSITION_GROUPS:
        text += 'WindowTransition %s\n  FireOnce = Yes\n' % name
        for win, style in TRANSITION_WINDOWS.get(name, ()):
            text += '  Window\n    WinName = %s\n    Style = %s\n    FrameDelay = 0\n  End\n' % (win, style)
        text += 'End\n'
    return text


# Icon animations Drawable::drawIconUI creates on demand; a missing template crashes the drawing.
ICON_ANIMATIONS = ('DefaultHeal StructureHeal VehicleHeal Demoralized_OBSOLETE BombTimed BombRemote Disabled '
                   'BattlePlanIcon_Bombard BattlePlanIcon_HoldTheLine BattlePlanIcon_SeekAndDestroy Emoticon '
                   'Enthusiastic Subliminal CarBomb').split()


def animation_2d():
    text = header('Animation2D')
    for name in ICON_ANIMATIONS:
        text += ('Animation %s\n  NumberImages = 1\n  Image = TestIcon\n  AnimationMode = LOOP\n'
                 '  AnimationDelay = 100\nEnd\n' % name)
    return text


def control_bar_scheme():
    # ControlBarSchemeManager asserts on an empty list; "Default" is the fallback by name
    text = header('ControlBarScheme')
    for name, side in (('Default', 'Observer'), ('TestAlpha', 'TestAlpha'), ('TestBravo', 'TestBravo'),
                       ('Civilian', 'Civilian')):
        text += 'ControlBarScheme %s\n  ScreenCreationRes = X:800 Y:600\n  Side = %s\nEnd\n' % (name, side)
    return text


# key bindings (MetaEvent.cpp, CommandXlat.cpp): command, key, category, name, description[, transition,
# modifiers]; the keyboard options list a binding under its category by its name. Force attack follows the
# modifier alone (KEY_NONE: MetaEventTranslator's "mods-only change"): Ctrl down begins it, Ctrl up ends it.
COMMAND_KEYS = [
    ('OPTIONS', 'KEY_ESC', 'INTERFACE', 'Game menu', 'Opens the game menu.'),
    ('STOP', 'KEY_S', 'CONTROL', 'Stop', 'The selected units stop.'),
    ('SCATTER', 'KEY_X', 'CONTROL', 'Scatter', 'The selected units spread out.'),
    ('SELECT_ALL', 'KEY_Q', 'SELECTION', 'Select all', 'Selects all your units.'),
    ('VIEW_COMMAND_CENTER', 'KEY_H', 'INTERFACE', 'Headquarters', 'Moves the view to your headquarters.'),
    ('BEGIN_FORCEATTACK', 'KEY_NONE', 'CONTROL', 'Force attack', 'Hold to attack anything, also your own.', 'DOWN', 'CTRL'),
    ('END_FORCEATTACK', 'KEY_NONE', 'CONTROL', 'Force attack end', 'Releasing ends the force attack.', 'UP', 'CTRL'),
]


def command_labels():
    """label -> text of the names and descriptions in CommandMap.ini"""
    out = {}
    for command, key, category, name, desc, *_ in COMMAND_KEYS:
        out['GUI:Key%s' % command] = name
        out['GUI:Key%sDesc' % command] = desc
    return out


def command_map():
    text = header('CommandMap')
    for command, key, category, name, desc, *rest in COMMAND_KEYS:
        transition, modifiers = rest or ('DOWN', 'NONE')
        text += ('CommandMap %s\n  Key = %s\n  Transition = %s\n  Modifiers = %s\n  UseableIn = GAME\n'
                 '  Category = %s\n  DisplayName = GUI:Key%s\n  Description = GUI:Key%sDesc\nEnd\n\n'
                 % (command, key, transition, modifiers, category, command, command))
    return text


def ini_files():
    files = {f: EMPTY for f in EMPTY_FILES}
    files.update({
        'Data/INI/AudioSettings.ini': audio_settings(),
        'Data/INI/Music.ini': music(),
        'Data/INI/Animation2D.ini': animation_2d(),
        'Data/INI/ControlBarScheme.ini': control_bar_scheme(),
        'Data/INI/CommandMap.ini': command_map(),
        'Data/INI/WindowTransitions.ini': window_transitions(),
        'Data/INI/GameLOD.ini': game_lod(),
        'Data/english/Language.ini': language(),
        'Data/INI/Mouse.ini': mouse(),
        'Data/INI/InGameUI.ini': in_game_ui(),
        'Data/INI/DrawGroupInfo.ini': draw_group_info(),
        'Data/INI/Water.ini': water(),
        'Data/INI/Terrain.ini': terrain(),
        'Data/INI/Roads.ini': roads(),
    })
    return files
