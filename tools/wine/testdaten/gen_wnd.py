# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
"""Window files (.wnd) of the test data set.

The engine reads the layouts in Window\\ (GameWindowManagerScript.cpp). The code looks up the
windows by name ("File.wnd:Name") and treats each as a gadget of the type the name suggests
(static text, button, progress bar ...). The names below are the ones the engine code asks for; the
positions, sizes and colours are made up here. Rectangles are screen coordinates in the 800 x 600
layout, also for child windows (the reader makes them relative to the parent). Menu buttons are drawn
from three pieces per state and panels from one picture (names from gen_ui_art); small buttons and
the command buttons of the control bar stay coloured rectangles, the engine puts its own pictures on
them.
"""

DRAW_STATES = 9   # MAX_DRAW_DATA


class Win:
    """One window: name without file prefix, gadget type, rectangle in 800x600 layout units."""

    def __init__(self, name, kind, rect, children=(), text='', colour=(40, 44, 58, 200), status=None,
                 system='[None]', input='[None]', draw='[None]', data=None, border=True, text_colour=(230, 230, 230, 255),
                 image=None, plain=False, font=(10, 0)):
        self.name = name
        self.kind = kind
        self.rect = rect
        self.children = list(children)
        self.text = text
        self.colour = colour
        self.status = status
        self.system = system
        self.input = input
        self.draw = draw
        self.data = data or {}
        self.border = border
        self.text_colour = text_colour
        self.image = image          # one picture for the window (panels, logo)
        self.plain = plain          # buttons: coloured rectangle instead of the picture pieces
        self.font = font            # (size, bold)


def kind_of(name):
    """The gadget type the engine code expects behind a window name."""
    n = name
    if n.startswith('StaticText') or n.endswith('StaticText') or n.endswith('Desc') or n.startswith('Label'):
        return 'STATICTEXT'
    if n.startswith('RadioButton'):
        return 'RADIOBUTTON'
    if n.startswith('Button') or n.endswith('Button'):
        return 'PUSHBUTTON'
    if 'ProgressBar' in n or n.startswith('ProgressLoad'):
        return 'PROGRESSBAR'
    if n.startswith('Edit') or n.startswith('TextEntry') or n == 'EntryDesc':
        return 'ENTRYFIELD'
    if n.startswith('Listbox') or n.startswith('ListBox'):
        return 'SCROLLLISTBOX'
    if n.startswith('Check') or n == 'Retaliation':
        return 'CHECKBOX'
    if n.startswith('Combo'):
        return 'COMBOBOX'
    if 'Slider' in n:
        return 'HORZSLIDER'
    return 'USER'


def _draw_block(label, colour, border, images=None, slots=None):
    """slots: index -> (colour, border) for slots that differ (the check box of a CHECKBOX)."""
    images = list(images or []) + ['NoImage'] * DRAW_STATES
    slots = slots or {}
    one = [('IMAGE: %s, COLOR: %d %d %d %d, BORDERCOLOR: %d %d %d %d' % ((images[i],) + slots.get(i, (colour, border))[0]
                                                                       + slots.get(i, (colour, border))[1]))
           for i in range(DRAW_STATES)]
    return '  %s = %s;' % (label, ',\n                    '.join(one))


def _button_images(state):
    """Draw data slots of a three-piece push button (GadgetPushButton.h): 0 left, 5 middle, 6 right;
    1, 3, 4 the same pieces while selected (pressed)."""
    return ['BtnL_' + state, 'BtnL_P', 'NoImage', 'BtnM_P', 'BtnR_P', 'BtnM_' + state, 'BtnR_' + state]


# Child windows report to their parent's system callback; containers between a gadget and the menu's
# own callback therefore pass messages on (PassMessagesToParentSystem).

# draw data of the parts of lists, sliders and combo boxes, as (key, colour, border)
_PARTS = {
    'SCROLLLISTBOX': [('LISTBOX%sUPBUTTONDRAWDATA', (70, 76, 70, 255), (150, 140, 96, 255)),
                      ('LISTBOX%sDOWNBUTTONDRAWDATA', (70, 76, 70, 255), (150, 140, 96, 255)),
                      ('LISTBOX%sSLIDERDRAWDATA', (30, 32, 30, 255), (90, 90, 80, 255))],
    'HORZSLIDER': [('SLIDERTHUMB%sDRAWDATA', (150, 140, 96, 255), (40, 40, 30, 255))],
    'COMBOBOX': [('COMBOBOXDROPDOWNBUTTON%sDRAWDATA', (70, 76, 70, 255), (150, 140, 96, 255)),
                 ('COMBOBOXEDITBOX%sDRAWDATA', (16, 18, 16, 240), (150, 140, 96, 255)),
                 ('COMBOBOXLISTBOX%sDRAWDATA', (20, 22, 20, 245), (150, 140, 96, 255))],
}


def _emit(win, prefix, depth, out):
    ind = '  ' * depth
    x1, y1, x2, y2 = win.rect
    kind = win.kind
    lines = []
    lines.append('WINDOW' if depth == 0 else 'WINDOW')
    lines.append('  WINDOWTYPE = %s;' % kind)
    lines.append('  SCREENRECT = UPPERLEFT: %d %d,\n               BOTTOMRIGHT: %d %d,\n               CREATIONRESOLUTION: 800 600;' % (x1, y1, x2, y2))
    lines.append('  NAME = "%s:%s";' % (prefix, win.name))
    pieces = kind == 'PUSHBUTTON' and not win.plain and x2 - x1 >= 48 and y2 - y1 >= 18
    status = win.status
    if status is None:
        status = 'ENABLED'
        if win.border and not pieces and not win.image:
            status += '+BORDER'
        if kind in ('STATICTEXT',):
            status += '+NOINPUT'
    if (pieces or win.image) and 'IMAGE' not in status:
        status += '+IMAGE'
    lines.append('  STATUS = %s;' % status)
    # a check box toggles only when the mouse release finds it hilited, which needs MOUSETRACK (GadgetCheckBoxInput)
    lines.append('  STYLE = %s;' % (kind + '+MOUSETRACK' if kind == 'CHECKBOX' else kind))
    lines.append('  SYSTEMCALLBACK = "%s";' % win.system)
    lines.append('  INPUTCALLBACK = "%s";' % win.input)
    lines.append('  TOOLTIPCALLBACK = "[None]";')
    lines.append('  DRAWCALLBACK = "%s";' % win.draw)
    lines.append('  FONT = NAME: "Arial", SIZE: %d, BOLD: %d;' % win.font)
    if win.text:
        lines.append('  TEXT = "%s";' % win.text)
    lines.append('  TOOLTIPDELAY = -1;')
    col = win.colour
    hi = tuple(min(255, c + 40) for c in col[:3]) + (col[3],)
    edge = (150, 140, 96, 255) if win.border else (0, 0, 0, 0)
    img = {st: _button_images(st) for st in 'NHD'} if pieces else {st: [win.image] if win.image else None for st in 'NHD'}
    # check box (GadgetCheckBox.h): slot 1 the unchecked box, slot 2 the checked one
    box = {1: ((16, 18, 16, 255), (150, 140, 96, 255)), 2: ((230, 190, 90, 255), (150, 140, 96, 255))} \
        if kind == 'CHECKBOX' else None
    lines.append(_draw_block('ENABLEDDRAWDATA', col, edge, img['N'], box))
    lines.append(_draw_block('DISABLEDDRAWDATA', tuple(c // 2 for c in col[:3]) + (col[3],), (0, 0, 0, 0), img['D'], box))
    lines.append(_draw_block('HILITEDRAWDATA', hi, (230, 190, 90, 255) if win.border else (0, 0, 0, 0), img['H'], box))
    for key, c, b in _PARTS.get(kind, ()):
        for st, f in (('ENABLED', 1.0), ('DISABLED', 0.5), ('HILITE', 1.3)):
            cc = tuple(min(255, int(v * f)) for v in c[:3]) + (c[3],)
            lines.append(_draw_block(key % st, cc, b))
    t = win.text_colour
    lines.append('  TEXTCOLOR = ENABLED: %d %d %d %d, ENABLEDBORDER: 0 0 0 255,\n              DISABLED: 128 128 128 255, DISABLEDBORDER: 0 0 0 255,\n              HILITE: 255 255 255 255, HILITEBORDER: 0 0 0 255;' % t)
    if kind == 'STATICTEXT':
        lines.append('  STATICTEXTDATA = CENTERED: %d;' % win.data.get('centered', 1))
    elif kind == 'SCROLLLISTBOX':
        lines.append('  LISTBOXDATA = LENGTH: %d, AUTOSCROLL: 0, SCROLLIFATEND: 0, AUTOPURGE: 0, SCROLLBAR: %d, MULTISELECT: 0, COLUMNS: 1, FORCESELECT: %d;'
                     % (win.data.get('length', 30), win.data.get('scrollbar', 0), win.data.get('forceselect', 0)))
    elif kind == 'ENTRYFIELD':
        lines.append('  TEXTENTRYDATA = MAXLEN: 64, SECRETTEXT: 0, NUMERICALONLY: 0, ALPHANUMERICALONLY: 0, ASCIIONLY: 0;')
    elif kind == 'RADIOBUTTON':
        lines.append('  RADIOBUTTONDATA = GROUP: %d;' % win.data.get('group', 0))
    elif kind == 'COMBOBOX':
        lines.append('  COMBOBOXDATA = ISEDITABLE: 0, MAXCHARS: 32, MAXDISPLAY: %d, ASCIIONLY: 0, LETTERSANDNUMBERS: 0;'
                     % win.data.get('display', 6))
    elif kind in ('HORZSLIDER', 'VERTSLIDER'):
        lines.append('  SLIDERDATA = MINVALUE: %d, MAXVALUE: %d;' % win.data.get('range', (0, 100)))
    if win.children:
        lines.append('  CHILD')
        out.append('\n'.join(lines))
        for c in win.children:
            _emit(c, prefix, depth + 1, out)
        out.append('  ENDALLCHILDREN')
        out.append('END')
    else:
        lines.append('END')
        out.append('\n'.join(lines))


def wnd_text(file_name, windows, init='[None]', update='[None]', shutdown='[None]'):
    out = ['FILE_VERSION = 2;',
           'STARTLAYOUTBLOCK',
           '  LAYOUTINIT = %s;' % init,
           '  LAYOUTUPDATE = %s;' % update,
           '  LAYOUTSHUTDOWN = %s;' % shutdown,
           'ENDLAYOUTBLOCK']
    for w in windows:
        _emit(w, file_name, 0, out)
    return '\n'.join(out) + '\n'


def plain(windows):
    """Marks windows and all their children as plain (no picture pieces); returns them."""
    for w in windows:
        w.plain = True
        plain(w.children)
    return windows


def W(name, rect, children=(), text='', **kw):
    kind = kw.pop('kind', None) or kind_of(name)
    return Win(name, kind, rect, children, text, **kw)


def inside(parent_rect, kids):
    """Children written relative to the parent, moved to absolute coordinates (SCREENRECT is absolute)."""
    dx, dy = parent_rect[0], parent_rect[1]
    for k in kids:
        k.rect = (k.rect[0] + dx, k.rect[1] + dy, k.rect[2] + dx, k.rect[3] + dy)
        inside((dx, dy), k.children)
    return kids


def numbered(pattern, indexes, rects, **kw):
    return [W(pattern % i, r, **kw) for i, r in zip(indexes, rects)]


def row(n, x, y, w, h, gap):
    return [(x + i * (w + gap), y, x + i * (w + gap) + w, y + h) for i in range(n)]


def grid(cols, n, x, y, w, h, gap):
    out = []
    for i in range(n):
        cx, cy = i % cols, i // cols
        out.append((x + cx * (w + gap), y + cy * (h + gap), x + cx * (w + gap) + w, y + cy * (h + gap) + h))
    return out


# ---------------------------------------------------------------------------------------------
# The files

def control_bar():
    """The control bar along the bottom edge: radar left, money and power, the command buttons (6 x 3; the
    engine puts the cameos and symbols on them), the selected object's portrait right. Production queue,
    construction, timer, beacon and observer windows share the command area."""
    area = (210, 484, 592, 596)
    ax, ay = area[0], area[1]
    # IMAGE: ControlBar puts the button pictures on (GadgetButtonSetEnabledImage) but does not switch on drawing them
    cmd = numbered('ButtonCommand%02d', range(1, 19), grid(6, 18, ax + 4, ay + 2, 60, 34, 3),
                   system='PassSelectedButtonsToParentSystem', colour=(40, 44, 40, 230), status='ENABLED+IMAGE')
    queue = numbered('ButtonQueue%02d', range(1, 10), row(9, ax + 4, ay + 4, 38, 30, 3), system='PassSelectedButtonsToParentSystem',
                     status='ENABLED+IMAGE')
    upgrades = numbered('UnitUpgrade%d', range(1, 6), row(5, 630, 560, 26, 26, 3), kind='USER')
    obs_buttons = numbered('ButtonPlayer%d', range(0, 8), grid(4, 8, ax + 4, ay + 4, 90, 18, 3))
    obs_texts = numbered('StaticTextPlayer%d', range(0, 8), grid(4, 8, ax + 4, ay + 48, 90, 18, 3), kind='STATICTEXT')
    sub = lambda kids, **kw: W(kw.pop('name'), area, kids, border=False, colour=(0, 0, 0, 0), **kw)
    children = [
        W('BackgroundMarker', (0, 476, 800, 600), image='PanelDark', border=False, status='ENABLED+IMAGE+NOINPUT'),
        W('LeftHUD', (8, 482, 122, 596), input='LeftHUDInput', colour=(10, 24, 12, 255)),
        W('PopupCommunicator', (126, 482, 144, 498), kind='PUSHBUTTON', system='PassSelectedButtonsToParentSystem'),
        W('ButtonOptions', (126, 500, 144, 516), system='PassSelectedButtonsToParentSystem'),
        W('ButtonIdleWorker', (126, 518, 144, 534), system='PassSelectedButtonsToParentSystem'),
        W('ButtonPlaceBeacon', (126, 536, 144, 552), system='PassSelectedButtonsToParentSystem'),
        W('ButtonGeneral', (126, 554, 144, 570), system='PassSelectedButtonsToParentSystem'),
        W('ButtonLarge', (126, 572, 144, 588), system='PassSelectedButtonsToParentSystem'),
        W('MoneyDisplay', (148, 482, 206, 500), kind='STATICTEXT', text='0', border=False, colour=(0, 0, 0, 0),
          font=(12, 1), text_colour=(230, 190, 90, 255)),
        W('PowerWindow', (148, 504, 206, 514), colour=(30, 90, 40, 255)),
        W('GeneralsExp', (148, 518, 206, 532)),
        W('ExpBarForeground', (148, 536, 206, 546)),
        W('WinFlag', (148, 550, 206, 574)),
        W('CommandWindow', area, cmd, system='PassMessagesToParentSystem', border=False, colour=(0, 0, 0, 0)),
        sub([W('UnderConstructionDesc', (ax + 6, ay + 6, ax + 370, ay + 40), kind='STATICTEXT'),
             W('ButtonCancelConstruction', (ax + 6, ay + 50, ax + 120, ay + 80), system='PassSelectedButtonsToParentSystem')],
            name='UnderConstructionWindow'),
        sub([W('OCLTimerStaticText', (ax + 6, ay + 6, ax + 370, ay + 30), kind='STATICTEXT'),
             W('OCLTimerProgressBar', (ax + 6, ay + 36, ax + 370, ay + 56), kind='PROGRESSBAR'),
             W('OCLTimerSellButton', (ax + 6, ay + 62, ax + 120, ay + 86), system='PassSelectedButtonsToParentSystem')],
            name='OCLTimerWindow'),
        sub([W('EditBeaconText', (ax + 6, ay + 6, ax + 370, ay + 30), kind='ENTRYFIELD'),
             W('StaticTextBeaconLabel', (ax + 6, ay + 36, ax + 370, ay + 56), kind='STATICTEXT'),
             W('ButtonClearBeaconText', (ax + 6, ay + 62, ax + 120, ay + 86), system='PassSelectedButtonsToParentSystem'),
             W('ButtonDeleteBeacon', (ax + 126, ay + 62, ax + 240, ay + 86), system='PassSelectedButtonsToParentSystem')],
            name='BeaconWindow', input='BeaconWindowInput'),
        sub(queue + [W('ButtonCancel', (ax + 6, ay + 44, ax + 120, ay + 70), system='PassSelectedButtonsToParentSystem')],
            name='ProductionQueueWindow', system='PassMessagesToParentSystem'),
        sub(obs_buttons + obs_texts, name='ObserverPlayerListWindow', system='PassMessagesToParentSystem'),
        sub([W('StaticTextNumberOfUnits', (ax + 6, ay + 6, ax + 200, ay + 22), kind='STATICTEXT'),
             W('StaticTextNumberOfBuildings', (ax + 6, ay + 26, ax + 200, ay + 42), kind='STATICTEXT'),
             W('StaticTextNumberOfUnitsKilled', (ax + 6, ay + 46, ax + 200, ay + 62), kind='STATICTEXT'),
             W('StaticTextNumberOfUnitsLost', (ax + 6, ay + 66, ax + 200, ay + 82), kind='STATICTEXT'),
             W('StaticTextPlayerName', (ax + 210, ay + 6, ax + 370, ay + 22), kind='STATICTEXT'),
             W('WinGeneralPortrait', (ax + 210, ay + 26, ax + 260, ay + 76))], name='ObserverPlayerInfoWindow'),
        W('RightHUD', (600, 482, 794, 596),
          [W('WinUnitSelected', (604, 486, 724, 558), [W('CameoWindow', (606, 488, 722, 556), kind='USER')]),
           W('WinUAttack', (728, 486, 790, 504), kind='USER')] + upgrades, border=False, colour=(0, 0, 0, 0)),
    ]
    return wnd_text('ControlBar.wnd', plain([W('ControlBarParent', (0, 476, 800, 600), children, system='ControlBarSystem',
                                               input='ControlBarInput', colour=(0, 0, 0, 0), border=False)]))


def replay_control():
    return wnd_text('ReplayControl.wnd', [W('ParentReplayControl', (0, 0, 1, 1), status='HIDDEN', border=False)])


def general_exp_points():
    # SCREENRECT is absolute, also for the children
    kids = [W('StaticTextTitle', (206, 104, 500, 124), kind='STATICTEXT'),
            W('StaticTextLevel', (206, 128, 350, 148), kind='STATICTEXT'),
            W('StaticTextRankPointsAvailable', (356, 128, 500, 148), kind='STATICTEXT'),
            W('ProgressBarExperience', (206, 152, 500, 168), kind='PROGRESSBAR'),
            W('ButtonExit', (500, 104, 540, 124), system='PassSelectedButtonsToParentSystem')]
    kids += numbered('ButtonRank1Number%d', range(4), row(4, 206, 180, 40, 30, 4), system='PassSelectedButtonsToParentSystem')
    kids += numbered('ButtonRank3Number%d', range(15), grid(8, 15, 206, 220, 40, 30, 4), system='PassSelectedButtonsToParentSystem')
    kids += numbered('ButtonRank8Number%d', range(4), row(4, 206, 300, 40, 30, 4), system='PassSelectedButtonsToParentSystem')
    return wnd_text('GeneralsExpPoints.wnd', [W('GenExpParent', (200, 100, 560, 420), kids, system='GeneralsExpPointsSystem',
                                                 input='GeneralsExpPointsInput')])


def build_popup():
    """Tooltip of the command buttons, above the right end of the control bar; the code grows it upwards with the
    description."""
    kids = [W('StaticTextName', (544, 364, 790, 382), kind='STATICTEXT'),
            W('StaticTextCost', (544, 384, 790, 402), kind='STATICTEXT'),
            W('StaticTextDescription', (544, 404, 790, 462), kind='STATICTEXT')]
    return wnd_text('ControlBarPopupDescription.wnd', [W('ParentPopup', (540, 362, 796, 472), kids, status='HIDDEN')])


def multiplayer_load_screen():
    n = 8
    kids = [W('WinMapPreview', (520, 60, 780, 260)),
            ]
    kids += numbered('ProgressLoad%d', range(n), [(20, 60 + i * 30, 240, 76 + i * 30) for i in range(n)], kind='PROGRESSBAR')
    kids += numbered('StaticTextPlayer%d', range(n), [(250, 60 + i * 30, 380, 76 + i * 30) for i in range(n)], kind='STATICTEXT')
    kids += numbered('StaticTextSide%d', range(n), [(384, 60 + i * 30, 470, 76 + i * 30) for i in range(n)], kind='STATICTEXT')
    kids += numbered('StaticTextTeam%d', range(n), [(474, 60 + i * 30, 510, 76 + i * 30) for i in range(n)], kind='STATICTEXT')
    kids += numbered('ButtonMapStartPosition%d', range(n), [(530 + i * 20, 270, 546 + i * 20, 286) for i in range(n)])
    return wnd_text('MultiplayerLoadScreen.wnd', [W('MultiplayerLoadScreenParent', (0, 0, 800, 600), kids, colour=(10, 10, 16, 255))])


def shell_game_load_screen():
    kids = [W('ProgressLoad', (100, 540, 700, 556), kind='PROGRESSBAR'),
            W('StaticTextLegal', (100, 500, 700, 530), kind='STATICTEXT'),
            W('EAGamesLogo', (10, 10, 100, 40))]
    return wnd_text('ShellGameLoadScreen.wnd', [W('ShellGameLoadScreenParent', (0, 0, 800, 600), kids, colour=(10, 10, 16, 255))])


def single_player_load_screen():
    kids = [W('ProgressLoad', (100, 560, 700, 576), kind='PROGRESSBAR'),
            W('Percent', (710, 560, 790, 576), kind='STATICTEXT'),
            W('ObjectivesWin', (40, 100, 400, 300), [W('StaticTextLine%d' % i, (44, 104 + i * 20, 390, 122 + i * 20), kind='STATICTEXT')
                                                      for i in range(6)]),
            W('StaticTextCameoText', (420, 100, 780, 120), kind='STATICTEXT'),
            W('StaticTextCameoText3', (420, 130, 780, 150), kind='STATICTEXT'),
            W('WindowCameo1', (420, 160, 500, 240)), W('WindowCameo2', (510, 160, 590, 240)), W('WindowCameo3', (600, 160, 680, 240)),
            W('WindowHead', (40, 320, 200, 480)), W('WindowHiliteCameo', (420, 250, 500, 260))]
    kids += [W('StaticTextCameoText%d' % i, (420 + i * 90, 244, 500 + i * 90, 260), kind='STATICTEXT') for i in (1, 2)]
    kids += [W('StaticTextLine%d' % i, (40, 310 + i * 20, 400, 328 + i * 20), kind='STATICTEXT') for i in range(6, 12)]
    return wnd_text('SinglePlayerLoadScreen.wnd', [W('ParentSinglePlayerLoadScreen', (0, 0, 800, 600), kids, colour=(10, 10, 16, 255))])


def blank_window():
    return wnd_text('BlankWindow.wnd', [W('BlankWindow', (0, 0, 800, 600), border=False, colour=(0, 0, 0, 255))])


def in_game_chat():
    kids = [W('TextEntryChat', (60, 4, 400, 24), kind='ENTRYFIELD'),
            W('StaticTextChatType', (4, 4, 56, 24), kind='STATICTEXT'),
            W('ButtonClear', (404, 4, 450, 24))]
    return wnd_text('InGameChat.wnd', [W('InGameChatParent', (100, 400, 560, 440), inside((100, 400), kids), status='HIDDEN')])


def in_game_popup_message():
    kids = [W('StaticTextMessage', (10, 10, 290, 100), kind='STATICTEXT'), W('ButtonOk', (110, 110, 190, 134))]
    return wnd_text('InGamePopupMessage.wnd', [W('InGamePopupMessageParent', (250, 200, 550, 350), inside((250, 200), kids),
                                                 system='InGamePopupMessageSystem')])


def diplomacy():
    n = 8
    kids = []
    kids += numbered('StaticTextPlayer%d', range(n), [(10, 40 + i * 24, 140, 58 + i * 24) for i in range(n)], kind='STATICTEXT')
    kids += numbered('StaticTextSide%d', range(n), [(144, 40 + i * 24, 230, 58 + i * 24) for i in range(n)], kind='STATICTEXT')
    kids += numbered('StaticTextTeam%d', range(n), [(234, 40 + i * 24, 270, 58 + i * 24) for i in range(n)], kind='STATICTEXT')
    kids += numbered('StaticTextStatus%d', range(n), [(274, 40 + i * 24, 340, 58 + i * 24) for i in range(n)], kind='STATICTEXT')
    kids += numbered('ButtonMute%d', range(n), [(344, 40 + i * 24, 380, 58 + i * 24) for i in range(n)])
    kids += numbered('ButtonUnMute%d', range(n), [(384, 40 + i * 24, 420, 58 + i * 24) for i in range(n)])
    parent = [W('InGameParent', (0, 0, 430, 260), kids), W('BuddiesParent', (0, 0, 430, 260), status='HIDDEN'),
              W('SoloParent', (0, 0, 430, 260), [W('ListboxSolo', (10, 10, 420, 250), kind='SCROLLLISTBOX')], status='HIDDEN'),
              W('RadioButtonInGame', (10, 4, 100, 24), kind='RADIOBUTTON'), W('RadioButtonBuddies', (110, 4, 200, 24), kind='RADIOBUTTON', data={'group': 1}),
              W('ButtonHide', (390, 4, 426, 24))]
    return wnd_text('Diplomacy.wnd', [W('DiplomacyParent', (180, 120, 610, 380), inside((180, 120), parent), status='HIDDEN')])


def popup_communicator():
    return wnd_text('PopupCommunicator.wnd', [W('PopupCommunicator', (250, 200, 550, 350), inside((250, 200), [W('ButtonOk', (110, 110, 190, 134))]),
                                                status='HIDDEN')])


def idle_worker():
    return wnd_text('IdleWorker.wnd', [W('IdleWorkerParent', (0, 0, 40, 40), [W('ButtonSelectNextIdleWorker', (0, 0, 40, 40))], status='HIDDEN')])


def motd():
    return wnd_text('MOTD.wnd', [W('MOTD', (200, 150, 600, 450), inside((200, 150), [W('ListboxMOTD', (10, 10, 390, 260), kind='SCROLLLISTBOX'),
                                                                                   W('CloseMOTD', (150, 268, 250, 292))]),
                                   status='HIDDEN')])


def ime_candidate():
    return wnd_text('IMECandidateWindow.wnd', [W('IMECandidateWindow', (0, 0, 100, 60), [
        W('TextArea', (0, 0, 100, 40), kind='USER'), W('UpArrow', (80, 0, 100, 10)),
        W('DownArrow', (80, 40, 100, 50))], status='HIDDEN')])


def ime_status():
    return wnd_text('IMEStatusWindow.wnd', [W('IMEStatusWindow', (0, 0, 40, 20), status='HIDDEN')])


# columns of the score table: window pattern, heading; seven columns of 72 from x 236 on. The score is not shown
# (populatePlayerInfo has it commented out), its windows stay hidden behind the name.
SCORE_COLUMNS = (('StaticTextUnitsBuilt%d', 'GUI:UnitsBuilt'), ('StaticTextUnitsLost%d', 'GUI:UnitsLost'),
                 ('StaticTextUnitsDestroyed%d', 'GUI:UnitsDestroyed'), ('StaticTextBuildingsBuilt%d', 'GUI:BuildingsBuilt'),
                 ('StaticTextBuildingsLost%d', 'GUI:BuildingsLost'),
                 ('StaticTextBuildingsDestroyed%d', 'GUI:BuildingsDestroyed'), ('StaticTextResources%d', 'GUI:Resources'))
SCORE_X = [(236 + k * 76, 308 + k * 76) for k in range(len(SCORE_COLUMNS))]


def score_screen():
    """Score screen after a game: a table with one row per player (ScoreScreen.cpp fills rows 0 to 7 in the
    player's colour and shows only the used ones; observers get StaticTextObserver over the numbers, the winner
    marker the side icon), the chat for LAN and internet games, the Academy advice, the buttons. In challenge
    games the code hides GadgetParent and MainBackdrop and shows the portrait and the remarks instead, so the
    buttons lie outside GadgetParent."""
    table = [title('StaticTextTitleScore', (40, 40, 500, 70), 'GUI:GameStatistics'),
             label('StaticTextPlayerHeader', (42, 98, 230, 116), 'GUI:Player')]
    # headings of two words take two lines
    table += [label('StaticText%sHeader' % pat[len('StaticText'):-2], (x0, 80, x1, 116), head, centered=1)
              for (pat, head), (x0, x1) in zip(SCORE_COLUMNS, SCORE_X)]
    for i in range(8):
        y = 122 + i * 26
        table += [W('GameWindowWinner%d' % i, (20, y + 1, 38, y + 19), status='ENABLED+HIDDEN+IMAGE', border=False,
                    colour=(0, 0, 0, 0)),
                  W('StaticTextPlayer%d' % i, (42, y, 230, y + 20), kind='STATICTEXT', status='ENABLED+HIDDEN', border=False,
                    colour=(0, 0, 0, 0), data={'centered': 0}),
                  W('StaticTextScore%d' % i, (42, y, 230, y + 20), kind='STATICTEXT', status='ENABLED+HIDDEN', border=False,
                    colour=(0, 0, 0, 0)),
                  W('StaticTextObserver%d' % i, (236, y, 764, y + 20), kind='STATICTEXT', status='ENABLED+HIDDEN',
                    border=False, colour=(0, 0, 0, 0), data={'centered': 1}),
                  W('ButtonAdd%d' % i, (774, y + 2, 792, y + 18), status='ENABLED+HIDDEN')]
        table += [W(pat % i, (x0, y, x1, y + 20), kind='STATICTEXT', status='ENABLED+HIDDEN', border=False,
                    colour=(0, 0, 0, 0), data={'centered': 1}) for (pat, head), (x0, x1) in zip(SCORE_COLUMNS, SCORE_X)]
    # chat (LAN, internet) on the left, Academy advice (single player, internet) on the right
    table += [W('ChatBoxBorder', (40, 344, 470, 524), image='PanelDark', border=False, status='ENABLED+HIDDEN+IMAGE+NOINPUT'),
              W('ListboxChatWindowScoreScreen', (46, 350, 464, 490), data={'length': 100, 'scrollbar': 1},
                colour=(16, 18, 16, 220), status='ENABLED+HIDDEN'),
              W('TextEntryChat', (46, 494, 396, 518), kind='ENTRYFIELD', colour=(16, 18, 16, 240), status='ENABLED+HIDDEN'),
              W('ButtonEmote', (400, 494, 464, 518), text='GUI:Emote', status='ENABLED+HIDDEN', **BTN),
              label('StaticTextWarSchool', (490, 344, 760, 362), 'GUI:WarSchool', status='ENABLED+HIDDEN'),
              W('ListboxWarschoolAdvice', (490, 366, 760, 524), data={'length': 20}, colour=(16, 18, 16, 220),
                status='ENABLED+HIDDEN')]
    kids = [W('MainBackdrop', (0, 0, 800, 600), image='PanelDark', border=False, status='ENABLED+IMAGE+NOINPUT'),
            W('GadgetParent', (0, 0, 800, 540), table, border=False, colour=(0, 0, 0, 0),
              system='PassMessagesToParentSystem'),
            W('BigPortrait', (40, 120, 240, 360), status='ENABLED+HIDDEN+IMAGE', border=False),
            label('ChallengeWinLossText', (260, 120, 760, 160), status='ENABLED+HIDDEN'),
            label('GeneralRemarks', (260, 170, 760, 360), status='ENABLED+HIDDEN'),
            label('StaticTextGameSaveComplete', (40, 528, 400, 546), 'GUI:GameSaveComplete', status='ENABLED+HIDDEN'),
            W('ButtonSaveReplay', (40, 552, 220, 586), text='GUI:SaveReplay', **BTN),
            W('ButtonRehost', (230, 552, 410, 586), text='GUI:Rehost', status='ENABLED+HIDDEN', **BTN),
            W('ButtonBuddy', (230, 552, 410, 586), text='GUI:Buddies', status='ENABLED+HIDDEN', **BTN),
            W('ButtonContinue', (420, 552, 600, 586), text='GUI:Continue', **BTN),
            W('ButtonOk', (610, 552, 760, 586), text='GUI:OK', **BTN)]
    return wnd_text('ScoreScreen.wnd', [W('ParentScoreScreen', (0, 0, 800, 600), kids, system='ScoreScreenSystem',
                                          input='ScoreScreenInput', colour=(0, 0, 0, 0), border=False)],
                    init='ScoreScreenInit', update='ScoreScreenUpdate', shutdown='ScoreScreenShutdown')


def popup_replay():
    kids = [W('MenuButtonFrame', (0, 0, 300, 200), colour=(30, 34, 48, 220)),
            W('ButtonBack', (10, 160, 110, 184)), W('ButtonSave', (150, 160, 250, 184)),
            W('ListboxGames', (10, 40, 290, 120)), W('TextEntryReplayName', (10, 125, 290, 150)),
            W('PopupReplaySaved', (10, 10, 290, 30), kind='STATICTEXT')]
    return wnd_text('PopupReplay.wnd', [W('PopupReplayMenu', (250, 200, 550, 400), inside((250, 200), kids), system='PopupReplaySystem',
                                          input='PopupReplayInput', status='ENABLED+HIDDEN')],
                    init='PopupReplayInit', update='PopupReplayUpdate', shutdown='PopupReplayShutdown')


GOLD = (230, 190, 90, 255)
# menu buttons keep the gadget's own system callback (it sets the label, GGM_SET_LABEL) and report a click
# to their parent
BTN = dict(font=(12, 1), text_colour=(235, 230, 210, 255))


def title(name, rect, text, size=16):
    return W(name, rect, kind='STATICTEXT', text=text, border=False, colour=(0, 0, 0, 0), font=(size, 1),
             text_colour=GOLD, data={'centered': 0})


def label(name, rect, text='', centered=0, status=None):
    return W(name, rect, kind='STATICTEXT', text=text, border=False, colour=(0, 0, 0, 0), data={'centered': centered},
             status=status)


def main_menu():
    """The main menu: logo, a panel with the buttons MainMenuInit and MainMenuSystem look up. The campaign
    is disabled, the data set has none; multiplayer leads to the LAN lobby."""
    buttons = [('ButtonSinglePlayer', 'SinglePlayer', False), ('ButtonMultiplayer', 'Multiplayer', True),
               ('ButtonSkirmish', 'Skirmish', True), ('ButtonOptions', 'Options', True), ('ButtonExit', 'Exit', True)]
    # MapBorder2 is the drop-down of the main buttons: MainMenuInit hides it and MainMenuInput shows it on
    # the first mouse move or key press; MapBorder1 holds the multiplayer group, MapBorder, 3 and 4 (single
    # player, load/replay, difficulty) stay empty, see-through and without input
    main = [W(name, (540, 196 + i * 52, 760, 232 + i * 52), text=text, status=None if enabled else 'IMAGE', **BTN)
            for i, (name, text, enabled) in enumerate(buttons)]
    kids = [W('LogoWindow', (20, 20, 404, 140), image='Logo', border=False, status='ENABLED+IMAGE+NOINPUT'),
            W('MapBorder2', (520, 170, 780, 470), main, image='Panel', border=False, status='ENABLED+HIDDEN+IMAGE',
              system='PassMessagesToParentSystem')]
    # multiplayer group: LAN (ButtonNetwork) and back; online play needs services the data set does not have
    multi = [W('ButtonNetwork', (540, 196, 760, 232), text='GUI:LAN', **BTN),
             W('ButtonOnline', (540, 248, 760, 284), text='GZH:MainMenuInternet', **BTN),
             W('ButtonMultiBack', (540, 404, 760, 440), text='GUI:Back', **BTN)]
    kids.append(W('MapBorder1', (520, 170, 780, 470), multi, image='Panel', border=False, status='ENABLED+HIDDEN+IMAGE',
                  system='PassMessagesToParentSystem'))
    names = ['ButtonSingleBack', 'ButtonLoadReplayBack',
             'ButtonReplay', 'ButtonLoadReplay', 'ButtonLoadGame', 'ButtonCredits', 'ButtonEasy', 'ButtonMedium',
             'ButtonHard', 'ButtonDiffBack', 'ButtonMOTD', 'ButtonWorldBuilder', 'ButtonGetUpdate', 'ButtonChallenge',
             'ButtonUSA', 'ButtonGLA', 'ButtonChina', 'ButtonUSARecentSave', 'ButtonUSALoadGame', 'ButtonGLARecentSave',
             'ButtonGLALoadGame', 'ButtonChinaRecentSave', 'ButtonChinaLoadGame', 'ButtonTRAINING', 'ButtonCampaign']
    for name in names:
        kids.append(W(name, (540, 420, 760, 440), system='PassSelectedButtonsToParentSystem', status='ENABLED+HIDDEN'))
    for name in ('MapBorder', 'MapBorder3', 'MapBorder4'):
        kids.append(W(name, (530, 190, 770, 440), status='ENABLED+HIDDEN+NOINPUT', border=False, colour=(0, 0, 0, 0)))
    for name in ('WinFactionGLA', 'WinFactionChina', 'WinFactionUS', 'WinGrowMarker', 'WinFactionTraining',
                 'WinFactionTrainingSmall', 'WinFactionTrainingMedium', 'WinFactionSkirmish', 'WinFactionSkirmishSmall',
                 'WinFactionSkirmishMedium', 'WinFactionUSSmall', 'WinFactionUSMedium', 'WinFactionGLASmall',
                 'WinFactionGLAMedium', 'WinFactionChinaSmall', 'WinFactionChinaMedium', 'MainMenuRuler'):
        kids.append(W(name, (0, 0, 10, 10), status='ENABLED+HIDDEN', border=False))
    kids.append(label('LabelVersion', (10, 570, 300, 590), 'Test data'))
    kids.append(label('StaticTextRandom1', (20, 150, 404, 170)))
    kids.append(label('StaticTextRandom2', (20, 172, 404, 192)))
    return wnd_text('MainMenu.wnd', [W('MainMenuParent', (0, 0, 800, 600), kids, system='MainMenuSystem', input='MainMenuInput',
                                       colour=(0, 0, 0, 0), border=False)],
                    init='MainMenuInit', update='MainMenuUpdate', shutdown='MainMenuShutdown')


def skirmish_options():
    """Skirmish setup: eight player rows (name, side, colour, team), map preview with start positions,
    starting cash, game speed; SkirmishGameOptionsMenuInit looks up every name."""
    kids = [title('StaticTextTitleSkirmish', (54, 44, 500, 74), 'GUI:SkirmishGameSetup'),
            label('StaticTextPlayerHeader', (54, 84, 200, 102), 'GUI:Player'),
            label('StaticTextFaction', (206, 84, 330, 102), 'GUI:Faction'),
            label('StaticTextColor', (336, 84, 436, 102), 'GUI:Color'),
            label('StaticTextTeam', (442, 84, 540, 102), 'GUI:Team')]
    for i in range(8):
        y = 106 + i * 30
        if i == 0:
            kids.append(W('TextEntryPlayerName', (54, y, 200, y + 22), kind='ENTRYFIELD', colour=(16, 18, 16, 240),
                          text='Commander'))
        kids.append(W('ComboBoxPlayer%d' % i, (54, y, 200, y + 22), status='ENABLED+HIDDEN' if i == 0 else None))
        kids.append(W('ComboBoxPlayerTemplate%d' % i, (206, y, 330, y + 22)))
        kids.append(W('ComboBoxColor%d' % i, (336, y, 436, y + 22)))
        kids.append(W('ComboBoxTeam%d' % i, (442, y, 540, y + 22)))
    # the start position buttons lie on the map preview; the menu moves them onto the start spots
    starts = [W('ButtonMapStartPosition%d' % i, (560 + i * 16, 250, 574 + i * 16, 264)) for i in range(8)]
    kids += [W('MapWindow', (560, 84, 746, 270), starts, colour=(10, 12, 10, 255), system='PassMessagesToParentSystem'),
             label('StaticTextMapPreview', (560, 274, 746, 292), centered=1),
             W('TextEntryMapDisplay', (560, 296, 746, 318), kind='ENTRYFIELD', status='ENABLED+NOINPUT', colour=(16, 18, 16, 240)),
             W('ButtonSelectMap', (560, 324, 746, 356), text='GUI:SelectMap', **BTN),
             label('StaticTextStartingCash', (560, 366, 746, 384), 'GUI:StartingCash'),
             W('ComboBoxStartingCash', (560, 386, 746, 408)),
             label('StaticTextGameSpeed', (560, 414, 746, 432), 'GUI:GameSpeed'),
             W('SliderGameSpeed', (560, 436, 746, 452), data={'range': (15, 60)}),
             W('CheckboxLimitSuperweapons', (560, 460, 746, 480), text='GUI:LimitSuperweapons')]
    kids += [W('ListboxInfo', (54, 352, 540, 470), data={'length': 50, 'scrollbar': 1}, colour=(16, 18, 16, 220)),
             label('StaticTextWinsValue', (54, 476, 170, 494)), label('StaticTextLossesValue', (174, 476, 290, 494)),
             label('StaticTextStreakValue', (294, 476, 410, 494)), label('StaticTextBestStreakValue', (414, 476, 540, 494)),
             W('ButtonBack', (54, 512, 214, 548), text='GUI:Back', **BTN),
             W('ButtonReset', (230, 512, 390, 548), text='GUI:Reset', **BTN),
             W('ButtonStart', (586, 512, 746, 548), text='GUI:Start', **BTN)]
    # the init hides SubParent, the transition SkirmishGameOptionsMenuFade shows it with everything inside
    sub = [W('SubParent', (30, 30, 770, 570), kids, image='Panel', border=False, system='PassMessagesToParentSystem')]
    return wnd_text('SkirmishGameOptionsMenu.wnd', [W('SkirmishGameOptionsMenuParent', (0, 0, 800, 600), sub,
                                                      system='SkirmishGameOptionsMenuSystem', input='SkirmishGameOptionsMenuInput',
                                                      colour=(0, 0, 0, 120), border=False)],
                    init='SkirmishGameOptionsMenuInit', update='SkirmishGameOptionsMenuUpdate',
                    shutdown='SkirmishGameOptionsMenuShutdown')


def map_select(file_name, parent, prefix):
    """Map selection over the game setup (skirmish and LAN); the setup menu hides its own map controls
    meanwhile and shows them again on OK."""
    kids = [W('MapSelectPanel', (120, 80, 680, 520), image='Panel', border=False),
            title('StaticTextTitleMaps', (144, 94, 600, 124), 'GUI:SelectMap'),
            W('RadioButtonSystemMaps', (144, 132, 300, 154), text='GUI:SystemMaps', data={'group': 7}),
            W('RadioButtonUserMaps', (310, 132, 466, 154), text='GUI:UserMaps', data={'group': 7}),
            # FORCESELECT: a click on the selected map must not clear the selection, OK needs one
            W('ListboxMap', (144, 162, 420, 450), data={'length': 100, 'scrollbar': 1, 'forceselect': 1}, colour=(16, 18, 16, 220)),
            W('WinMapPreview', (440, 162, 656, 378), [W('ButtonMapStartPosition%d' % i, (440 + i * 16, 360, 454 + i * 16, 374))
                                                      for i in range(8)],
              colour=(10, 12, 10, 255), system='PassMessagesToParentSystem'),
            W('ButtonBack', (144, 466, 304, 502), text='GUI:Back', **BTN),
            W('ButtonOK', (496, 466, 656, 502), text='GUI:OK', **BTN)]
    return wnd_text(file_name, [W(parent, (0, 0, 800, 600), kids, system=prefix + 'System', input=prefix + 'Input',
                                  colour=(0, 0, 0, 120), border=False)],
                    init=prefix + 'Init', update=prefix + 'Update', shutdown=prefix + 'Shutdown')


def lan_lobby():
    """LAN lobby: player name, list of games and players, chat; host, join, direct connection. The init
    hides GadgetParent, the transition LanLobbyFade shows it with everything inside."""
    kids = [title('StaticTextTitleLan', (54, 44, 500, 74), 'GUI:LANLobby'),
            label('StaticTextNameLabel', (54, 84, 200, 102), 'GUI:PlayerName'),
            W('TextEntryPlayerName', (54, 104, 300, 126), kind='ENTRYFIELD', colour=(16, 18, 16, 240)),
            label('StaticTextGamesLabel', (54, 136, 300, 154), 'GUI:Games'),
            W('ListboxGames', (54, 156, 520, 330), data={'length': 50, 'scrollbar': 1, 'forceselect': 1}, colour=(16, 18, 16, 220)),
            label('StaticTextPlayersLabel', (540, 136, 746, 154), 'GUI:Players'),
            W('ListboxPlayers', (540, 156, 746, 330), data={'length': 50, 'scrollbar': 1}, colour=(16, 18, 16, 220)),
            label('StaticTextGameInfo', (54, 334, 746, 352)),
            W('ListboxChatWindowLanLobby', (54, 358, 746, 470), data={'length': 100, 'scrollbar': 1}, colour=(16, 18, 16, 220)),
            W('TextEntryChat', (54, 474, 640, 496), colour=(16, 18, 16, 240)),
            W('ButtonClear', (646, 470, 746, 500), text='GUI:Clear', **BTN),
            W('ButtonEmote', (646, 470, 746, 500), text='GUI:Emote', status='ENABLED+HIDDEN', **BTN),
            W('StaticToolTip', (54, 500, 746, 512), kind='STATICTEXT', status='ENABLED+HIDDEN', border=False, colour=(0, 0, 0, 0)),
            W('ButtonBack', (54, 514, 214, 550), text='GUI:Back', **BTN),
            W('ButtonDirectConnect', (230, 514, 410, 550), text='GUI:DirectConnect', **BTN),
            W('ButtonJoin', (426, 514, 586, 550), text='GUI:Join', **BTN),
            W('ButtonHost', (602, 514, 746, 550), text='GUI:Host', **BTN)]
    sub = [W('GadgetParent', (30, 30, 770, 570), kids, image='Panel', border=False, system='PassMessagesToParentSystem')]
    return wnd_text('LanLobbyMenu.wnd', [W('LanLobbyMenuParent', (0, 0, 800, 600), sub, system='LanLobbyMenuSystem',
                                           input='LanLobbyMenuInput', colour=(0, 0, 0, 120), border=False)],
                    init='LanLobbyMenuInit', update='LanLobbyMenuUpdate', shutdown='LanLobbyMenuShutdown')


def lan_game_options():
    """LAN game setup: eight slots (player, side, colour, team, ready mark), map with start positions,
    starting cash, chat; the host starts, the others accept."""
    kids = [title('StaticTextTitleLanGame', (54, 44, 500, 74), 'GUI:LANGameSetup'),
            label('StaticTextPlayerHeader', (54, 84, 190, 102), 'GUI:Player'),
            label('StaticTextFactionHeader', (196, 84, 310, 102), 'GUI:Faction'),
            label('StaticTextColorHeader', (316, 84, 410, 102), 'GUI:Color'),
            label('StaticTextTeamHeader', (416, 84, 520, 102), 'GUI:Team')]
    for i in range(8):
        y = 106 + i * 28
        kids += [W('ComboBoxPlayer%d' % i, (54, y, 190, y + 22)),
                 W('ComboBoxPlayerTemplate%d' % i, (196, y, 310, y + 22)),
                 W('ComboBoxColor%d' % i, (316, y, 410, y + 22)),
                 W('ComboBoxTeam%d' % i, (416, y, 520, y + 22)),
                 W('ButtonAccept%d' % i, (526, y + 2, 544, y + 20))]
    starts = [W('ButtonMapStartPosition%d' % i, (560 + i * 16, 250, 574 + i * 16, 264)) for i in range(8)]
    kids += [W('MapWindow', (560, 84, 746, 270), starts, colour=(10, 12, 10, 255), system='PassMessagesToParentSystem'),
             W('TextEntryMapDisplay', (560, 276, 746, 298), kind='ENTRYFIELD', status='ENABLED+NOINPUT', colour=(16, 18, 16, 240)),
             W('ButtonSelectMap', (560, 304, 746, 336), text='GUI:SelectMap', **BTN),
             label('StaticTextStartingCash', (560, 344, 746, 362), 'GUI:StartingCash'),
             W('ComboBoxStartingCash', (560, 364, 746, 386)),
             W('CheckboxLimitSuperweapons', (560, 394, 746, 414), text='GUI:LimitSuperweapons'),
             W('CheckboxListPublicly', (560, 418, 746, 438), text='GZH:InternetListPublicly', status='ENABLED+HIDDEN'),
             W('ListboxChatWindowLanGame', (54, 340, 544, 470), data={'length': 100, 'scrollbar': 1}, colour=(16, 18, 16, 220)),
             W('TextEntryChat', (54, 474, 544, 496), colour=(16, 18, 16, 240)),
             W('ButtonEmote', (560, 470, 746, 500), text='GUI:Emote', status='ENABLED+HIDDEN', **BTN),
             W('ButtonBack', (54, 512, 214, 548), text='GUI:Back', **BTN),
             W('ButtonStart', (586, 512, 746, 548), text='GUI:Start', **BTN)]
    sub = [W('LanGamePanel', (30, 30, 770, 570), kids, image='Panel', border=False, system='PassMessagesToParentSystem')]
    return wnd_text('LanGameOptionsMenu.wnd', [W('LanGameOptionsMenuParent', (0, 0, 800, 600), sub,
                                                 system='LanGameOptionsMenuSystem', input='LanGameOptionsMenuInput',
                                                 colour=(0, 0, 0, 120), border=False)],
                    init='LanGameOptionsMenuInit', update='LanGameOptionsMenuUpdate', shutdown='LanGameOptionsMenuShutdown')


def network_direct_connect():
    kids = [W('DirectPanel', (180, 140, 620, 460), image='Panel', border=False),
            title('StaticTextTitleDirect', (204, 154, 600, 184), 'GUI:DirectConnect'),
            label('StaticTextNameLabel', (204, 194, 596, 212), 'GUI:PlayerName'),
            W('EditPlayerName', (204, 214, 596, 236), kind='ENTRYFIELD', colour=(16, 18, 16, 240)),
            label('StaticTextRemoteLabel', (204, 246, 596, 264), 'GUI:RemoteAddress'),
            W('ComboboxRemoteIP', (204, 266, 596, 288)),
            label('StaticTextLocalLabel', (204, 298, 400, 316), 'GUI:LocalAddress'),
            label('StaticLocalIP', (404, 298, 596, 316)),
            W('ButtonBack', (204, 404, 334, 438), text='GUI:Back', **BTN),
            W('ButtonJoin', (336, 404, 466, 438), text='GUI:Join', **BTN),
            W('ButtonHost', (468, 404, 596, 438), text='GUI:Host', **BTN)]
    return wnd_text('NetworkDirectConnect.wnd', [W('NetworkDirectConnectParent', (0, 0, 800, 600), kids,
                                                   system='NetworkDirectConnectSystem', input='NetworkDirectConnectInput',
                                                   colour=(0, 0, 0, 120), border=False)],
                    init='NetworkDirectConnectInit', update='NetworkDirectConnectUpdate', shutdown='NetworkDirectConnectShutdown')


def game_info_window():
    """Details of the game under the mouse in the LAN lobby; the code positions and shows it."""
    kids = [label('StaticTextGameName', (8, 6, 232, 24)), label('StaticTextMapName', (8, 26, 232, 44)),
            W('ListBoxPlayers', (8, 48, 232, 150), data={'length': 8}, colour=(16, 18, 16, 220)),
            W('WinCrates', (8, 154, 40, 170)), W('WinSuperWeapons', (44, 154, 76, 170)), W('WinFreeForAll', (80, 154, 112, 170))]
    return wnd_text('GameInfoWindow.wnd', [W('ParentGameInfo', (0, 0, 240, 176), kids, image='PanelDark', border=False,
                                             system='GameInfoWindowSystem')], init='GameInfoWindowInit')


def disconnect_screen():
    """Shown when players stop answering: names, time-outs, votes to drop them, quit."""
    kids = [W('DisconnectPanel', (150, 100, 650, 500), image='Panel', border=False),
            title('StaticTextTitleDisconnect', (174, 114, 600, 142), 'GUI:WaitingForPlayers', 14)]
    for i in range(1, 8):
        y = 150 + (i - 1) * 30
        kids += [label('StaticPlayer%dName' % i, (174, y, 360, y + 20)), label('StaticPlayer%dTimeout' % i, (364, y, 440, y + 20)),
                 label('StaticPlayer%dVotes' % i, (444, y, 500, y + 20)),
                 W('ButtonKickPlayer%d' % i, (504, y - 2, 626, y + 22), text='GUI:Drop', **BTN)]
    kids += [label('StaticPacketRouterTimeoutLabel', (174, 364, 400, 382), 'GUI:PacketRouterTimeout'),
             label('StaticPacketRouterTimeout', (404, 364, 500, 382)),
             W('ListboxTextDisplay', (174, 386, 626, 450), data={'length': 50}, colour=(16, 18, 16, 220)),
             W('TextEntry', (174, 454, 500, 476), kind='ENTRYFIELD', colour=(16, 18, 16, 240)),
             W('ButtonQuitGame', (504, 450, 626, 482), text='GUI:QuitGame', **BTN)]
    return wnd_text('DisconnectScreen.wnd', [W('DisconnectParent', (0, 0, 800, 600), kids, system='DisconnectControlSystem',
                                               input='DisconnectControlInput', colour=(0, 0, 0, 0), border=False)])


def map_transfer_screen():
    kids = [title('StaticTextTitleTransfer', (100, 60, 700, 90), 'GUI:MapTransfer'),
            label('StaticTextCurrentFile', (100, 96, 700, 114)), label('StaticTextTimeout', (100, 116, 700, 134))]
    for i in range(8):
        y = 150 + i * 34
        kids += [label('StaticTextPlayer%d' % i, (100, y, 300, y + 20)),
                 W('ProgressLoad%d' % i, (304, y, 600, y + 20), kind='PROGRESSBAR'),
                 label('StaticTextProgress%d' % i, (604, y, 700, y + 20))]
    return wnd_text('MapTransferScreen.wnd', [W('MapTransferParent', (0, 0, 800, 600), kids, image='PanelDark', border=False)])


def crc_mismatch():
    kids = [W('StaticTextCRCMismatch', (220, 260, 580, 340), kind='STATICTEXT', text='GUI:CRCMismatch', border=False,
              colour=(0, 0, 0, 0), font=(14, 1), text_colour=GOLD, data={'centered': 1})]
    return wnd_text('CRCMismatch.wnd', [W('CRCMismatchParent', (200, 240, 600, 360), kids, image='Panel', border=False)])


def options_menu():
    """Options, grouped like the game's layout: display, sound and control groups in a frame, a network group
    the menu hides, check boxes the file keeps hidden, and the detail panel (WinAdvancedDisplayOptions). The
    tab pages of the menu (OptionsMenuTabs.cpp) move these groups; positions here are made up."""
    PASS = 'PassMessagesToParentSystem'
    HID = 'ENABLED+HIDDEN'

    def head(rect, text):
        return W('', rect, kind='STATICTEXT', text=text, border=False, colour=(0, 0, 0, 0), font=(14, 0),
                 text_colour=GOLD, data={'centered': 1})

    def lab(name, rect, text, status=None):
        return W(name, rect, kind='STATICTEXT', text=text, border=False, colour=(0, 0, 0, 0), font=(14, 0),
                 data={'centered': 0}, status=status)

    def line(name, rect):
        return W(name, rect, colour=(150, 140, 96, 255), border=False)

    def box(name, rect, text, font=14, status=None):
        return W(name, rect, text=text, font=(font, 0), status=status)

    video = [head((156, 70, 360, 96), 'GUI:DisplayOptions'),
             lab('ResolutionLabel', (156, 104, 300, 126), 'GUI:Resolution'),
             W('ComboBoxResolution', (160, 128, 304, 150)),
             lab('DetailLabel', (156, 158, 300, 180), 'GUI:Detail'),
             W('ComboBoxDetail', (160, 182, 304, 204)),
             lab('GammaLabel', (156, 218, 352, 240), 'GUI:GammaAdjustment'),
             W('SliderGamma', (164, 244, 372, 266)),
             lab('', (180, 240, 344, 266), 'GUI:AntiAliasing', status=HID),
             W('ComboBoxAntiAliasing', (200, 268, 340, 292), status=HID)]
    audio = [head((398, 70, 610, 96), 'GUI:AudioOptions'),
             lab('MusicVolumeLabel', (400, 104, 584, 126), 'GUI:MusicVolume'),
             lab('SFXVolumeLabel', (400, 158, 620, 180), 'GUI:SFXVolume'),
             lab('VoiceVolumeLabel', (400, 218, 584, 240), 'GUI:VoiceVolume'),
             W('SliderVoiceVolume', (404, 244, 612, 266))]
    scroll = [head((160, 274, 636, 296), 'GUI:ControlOptions'),
              box('CheckAlternateMouse', (160, 302, 352, 324), 'GUI:AlternateMouse'),
              box('Retaliation', (160, 326, 352, 348), 'GUI:RetaliationTitle'),
              box('CheckDoubleClickAttackMove', (388, 302, 632, 324), 'GUI:DoubleClickAttackMove'),
              lab('ScrollSpeedLabel', (160, 348, 624, 370), 'GUI:ScrollSpeed'),
              W('SliderScrollSpeed', (168, 372, 610, 394), data={'range': (10, 145)})]
    network = [head((160, 400, 572, 422), 'GUI:NetworkOptions'),
               box('CheckSendDelay', (480, 496, 608, 518), 'GUI:SendDelay'),
               W('ComboBoxIP', (480, 432, 608, 454)),
               W('ComboBoxOnlineIP', (240, 432, 368, 454)),
               W('TextEntryFirewallPortOverride', (304, 464, 448, 486), kind='ENTRYFIELD'),
               W('TextEntryHTTPProxy', (304, 488, 448, 510), kind='ENTRYFIELD'),
               W('ButtonFirewallRefresh', (480, 464, 596, 488), text='GUI:FirewallRefresh', plain=True)]
    toggles = [('Check3DShadows', 'GUI:Shadows3D'), ('Check2DShadows', 'GUI:Shadows2D'),
               ('CheckCloudShadows', 'GUI:CloudShadows'), ('CheckGroundLighting', 'GUI:GroundLighting'),
               ('CheckSmoothWater', 'GUI:SmoothWater'), ('CheckBehindBuilding', 'GUI:BehindBuilding'),
               ('CheckShowProps', 'GUI:ShowProps'), ('CheckExtraAnimations', 'GUI:ExtraAnimations'),
               ('CheckNoDynamicLOD', 'GUI:NoDynamicLOD'), ('CheckHeatEffects', 'GUI:HeatEffects'),
               ('CheckUnlockFPS', 'GUI:UnlockMaxFPS')]
    inner = [head((168, 112, 606, 138), 'GUI:AdvancedOptionsToggleOnOff'), line('ToggleOnOffLine', (160, 139, 622, 140))]
    for i, (name, text) in enumerate(toggles):
        x = 168 if i < 5 else 384
        y = 144 + (i % 5 if i < 5 else i - 5) * 24
        inner.append(box(name, (x, y, x + 200, y + 22), text, font=10,
                         status=HID if name == 'CheckUnlockFPS' else None))
    advanced = [head((162, 74, 576, 100), 'GUI:AdvanceDisplayOptions'),
                line('AdvanceOptionsLine', (152, 104, 636, 105)),
                W('', (160, 112, 622, 300), inner, colour=(0, 0, 0, 0), border=False, system=PASS),
                W('LowResWin', (160, 312, 624, 398), [lab('', (172, 314, 460, 338), 'GUI:LowResSlider'),
                                                      W('LowResSlider', (240, 354, 526, 378), data={'range': (0, 2)})],
                  colour=(0, 0, 0, 0), border=False, system=PASS),
                W('ParticleCapWin', (160, 416, 624, 502), [lab('', (172, 418, 460, 442), 'GUI:ParticleCap'),
                                                           W('ParticleCapSlider', (240, 458, 526, 482),
                                                             data={'range': (100, 5000)})],
                  colour=(0, 0, 0, 0), border=False, system=PASS),
                W('ButtonAdvanceAccept', (296, 520, 454, 552), text='GUI:Accept', **BTN),
                W('ButtonAdvanceBack', (466, 520, 624, 552), text='GUI:Cancel', **BTN)]
    frame = [W('', (152, 22, 420, 54), kind='STATICTEXT', text='GUI:Options', border=False, colour=(0, 0, 0, 0),
               font=(16, 1), text_colour=GOLD, data={'centered': 0}),
             line('Line', (136, 56, 648, 57)),
             W('VideoParent', (152, 70, 388, 270), video, colour=(0, 0, 0, 0), border=False, system=PASS),
             line('Line1', (152, 97, 386, 98)),
             W('AudioParent', (392, 70, 636, 270), audio, colour=(0, 0, 0, 0), border=False, system=PASS),
             W('SliderMusicVolume', (404, 128, 612, 152)),
             W('SliderSFXVolume', (404, 182, 612, 206)),
             line('Line2', (392, 97, 636, 98)),
             W('ScrollParent', (152, 274, 636, 398), scroll, colour=(0, 0, 0, 0), border=False, system=PASS),
             line('Line3', (152, 297, 636, 298)),
             line('Line4', (152, 422, 636, 423)),
             W('NetworkParent', (152, 400, 636, 520), network, colour=(0, 0, 0, 0), border=False, system=PASS),
             box('CheckLanguageFilter', (720, 156, 930, 190), 'GUI:LanguageFilter', font=10, status=HID),
             W('ButtonKeyboardOptions', (740, 128, 920, 166), text='GUI:KeyboardOptions', status=HID, **BTN),
             box('CheckBoxSaveCamera', (648, 488, 920, 510), 'GUI:SaveCamera', font=10, status=HID),
             box('CheckBoxUseCamera', (648, 512, 920, 534), 'GUI:UseCamera', font=10, status=HID),
             box('CheckBoxDrawAnchor', (648, 536, 920, 558), 'GUI:DrawScrollAnchor', font=10, status=HID),
             box('CheckBoxMoveAnchor', (648, 560, 920, 582), 'GUI:MoveScrollAnchor', font=10, status=HID),
             label('LabelVersion', (152, 562, 632, 578)),
             W('ButtonDefaults', (152, 528, 308, 560), text='GUI:Defaults', **BTN),
             W('ButtonAccept', (312, 528, 470, 560), text='GUI:Accept', **BTN),
             W('ButtonBack', (476, 528, 634, 560), text='GUI:Cancel', **BTN),
             W('WinAdvancedDisplayOptions', (152, 68, 636, 560), advanced, image='PanelDark', border=False,
               status='ENABLED+HIDDEN+IMAGE', system=PASS)]
    kids = [W('OptionsMenuParentOld', (136, 20, 648, 584), frame, image='Panel', border=False, system=PASS)]
    return wnd_text('OptionsMenu.wnd', [W('OptionsMenuParent', (120, 10, 660, 598), kids, system='OptionsMenuSystem',
                                          input='OptionsMenuInput', colour=(0, 0, 0, 0), border=False)],
                    init='OptionsMenuInit', update='OptionsMenuUpdate', shutdown='OptionsMenuShutdown')


def keyboard_options():
    kids = [W('KeyboardPanel', (80, 60, 720, 540), image='Panel', border=False),
            title('StaticTextTitleKeys', (104, 74, 500, 104), 'GUI:KeyboardOptions'),
            W('ComboBoxCategoryList', (104, 114, 400, 136)),
            W('ListBoxCommandList', (104, 144, 400, 470), data={'length': 200, 'scrollbar': 1}, colour=(16, 18, 16, 220)),
            label('StaticTextDescription', (420, 144, 696, 260)),
            label('StaticTextCurrentHotkey', (420, 270, 696, 290)),
            W('TextEntryAssignHotkey', (420, 300, 560, 322), kind='ENTRYFIELD', colour=(16, 18, 16, 240)),
            W('ButtonAssign', (570, 296, 696, 326), text='GUI:Assign', **BTN),
            W('ButtonResetAll', (420, 340, 696, 372), text='GUI:ResetAll', **BTN),
            W('ButtonBack', (104, 486, 264, 522), text='GUI:Back', **BTN)]
    return wnd_text('KeyboardOptionsMenu.wnd', [W('ParentKeyboardOptionsMenu', (0, 0, 800, 600), kids,
                                                  system='KeyboardOptionsMenuSystem', input='KeyboardOptionsMenuInput',
                                                  colour=(0, 0, 0, 120), border=False)],
                    init='KeyboardOptionsMenuInit', shutdown='KeyboardOptionsMenuShutdown')


def quit_menu(save=True):
    """The in-game menu (Esc): back to the game, options, save/load, restart, leave; QuitNoSave.wnd (games
    that cannot be saved) without save/load."""
    entries = [('ButtonReturn', 'GUI:ReturnToGame'), ('ButtonOptions', 'GUI:Options'), ('ButtonSaveLoad', 'GUI:SaveLoad'),
               ('ButtonRestart', 'GUI:Restart'), ('ButtonExit', 'GUI:ExitToMenu')]
    if not save:
        entries = [e for e in entries if e[0] != 'ButtonSaveLoad']
    top = 300 - len(entries) * 24 - 30
    kids = [W('QuitPanel', (280, top, 520, top + 60 + len(entries) * 48), image='Panel', border=False),
            title('StaticTextTitleQuit', (300, top + 12, 500, top + 40), 'GUI:GameMenu', 14)]
    for i, (name, text) in enumerate(entries):
        kids.append(W(name, (300, top + 50 + i * 48, 500, top + 86 + i * 48), text=text, **BTN))
    file_name = 'QuitMenu.wnd' if save else 'QuitNoSave.wnd'
    return wnd_text(file_name, [W('QuitMenuParent', (0, 0, 800, 600), kids, system='QuitMenuSystem',
                                  colour=(0, 0, 0, 120), border=False)])


def message_box(file_name, system):
    """Yes/No/OK/Cancel box; the code shows the buttons it needs and sets title and message."""
    kids = [W('MessageBoxPanel', (220, 200, 580, 400), image='Panel', border=False),
            W('StaticTextTitle', (240, 212, 560, 240), kind='STATICTEXT', border=False, colour=(0, 0, 0, 0), font=(14, 1),
              text_colour=GOLD, data={'centered': 1}),
            W('StaticTextMessage', (240, 246, 560, 340), kind='STATICTEXT', border=False, colour=(0, 0, 0, 0),
              data={'centered': 1})]
    # gogoMessageBox shows the buttons it needs and moves them to three places: OK or Yes at the first (where ButtonOk
    # is), No at the second (ButtonNo) or alone at the third, Cancel at the third (ButtonCancel); all start hidden
    for name, x, text in (('ButtonYes', 240, 'GUI:Yes'), ('ButtonOk', 240, 'GUI:OK'), ('ButtonNo', 350, 'GUI:No'),
                          ('ButtonCancel', 460, 'GUI:Cancel')):
        kids.append(W(name, (x, 350, x + 100, 384), text=text, status='ENABLED+HIDDEN', **BTN))
    return wnd_text(file_name, [W('MessageBoxParent', (0, 0, 800, 600), kids, system=system, colour=(0, 0, 0, 140), border=False)])


def popup_save_load():
    """Save and load in the game: list of saves, description entry and the four confirmation panels."""
    def confirm(parent, yes, no, text):
        return W(parent, (250, 230, 550, 370), [
            W(parent + 'Text', (270, 250, 530, 300), kind='STATICTEXT', text=text, border=False, colour=(0, 0, 0, 0),
              data={'centered': 1}),
            W(yes, (270, 316, 390, 350), text='GUI:Yes', **BTN), W(no, (410, 316, 530, 350), text='GUI:No', **BTN)],
                 image='PanelDark', border=False, status='ENABLED+HIDDEN+IMAGE', system='PassMessagesToParentSystem')
    kids = [W('MenuButtonFrame', (150, 80, 650, 520), image='Panel', border=False),
            title('StaticTextTitleSaveLoad', (174, 94, 600, 124), 'GUI:SaveLoad'),
            W('ListboxGames', (174, 134, 626, 420), data={'length': 100, 'scrollbar': 1}, colour=(16, 18, 16, 220)),
            W('ButtonSave', (174, 432, 290, 466), text='GUI:Save', **BTN),
            W('ButtonLoad', (294, 432, 410, 466), text='GUI:Load', **BTN),
            W('ButtonDelete', (414, 432, 530, 466), text='GUI:Delete', **BTN),
            W('ButtonBack', (534, 432, 626, 466), text='GUI:Back', **BTN),
            W('SaveDescParent', (220, 220, 580, 380), [
                label('SaveDescLabel', (240, 236, 560, 256), 'GUI:SaveDescription'),
                W('EntryDesc', (240, 262, 560, 286), kind='ENTRYFIELD', colour=(16, 18, 16, 240)),
                W('ButtonSaveDescConfirm', (240, 320, 390, 354), text='GUI:Save', **BTN),
                W('ButtonSaveDescCancel', (410, 320, 560, 354), text='GUI:Cancel', **BTN)],
              image='PanelDark', border=False, status='ENABLED+HIDDEN+IMAGE', system='PassMessagesToParentSystem'),
            confirm('OverwriteConfirmParent', 'ButtonOverwriteConfirm', 'ButtonOverwriteCancel', 'GUI:OverwriteSave'),
            confirm('LoadConfirmParent', 'ButtonLoadConfirm', 'ButtonLoadCancel', 'GUI:LoadSave'),
            confirm('DeleteConfirmParent', 'ButtonDeleteConfirm', 'ButtonDeleteCancel', 'GUI:DeleteSave')]
    return wnd_text('PopupSaveLoad.wnd', [W('SaveLoadMenu', (0, 0, 800, 600), kids, system='SaveLoadMenuSystem',
                                            input='SaveLoadMenuInput', colour=(0, 0, 0, 140), border=False)],
                    init='SaveLoadMenuInit', update='SaveLoadMenuUpdate', shutdown='SaveLoadMenuShutdown')


def window_files():
    """rel path below Window/ -> text"""
    return {
        'ControlBar.wnd': control_bar(),
        'ReplayControl.wnd': replay_control(),
        'GeneralsExpPoints.wnd': general_exp_points(),
        'ControlBarPopupDescription.wnd': build_popup(),
        'InGameChat.wnd': in_game_chat(),
        'InGamePopupMessage.wnd': in_game_popup_message(),
        'Diplomacy.wnd': diplomacy(),
        'IdleWorker.wnd': idle_worker(),
        'MOTD.wnd': motd(),
        'IMECandidateWindow.wnd': ime_candidate(),
        'IMEStatusWindow.wnd': ime_status(),
        'Menus/BlankWindow.wnd': blank_window(),
        'Menus/MainMenu.wnd': main_menu(),
        'Menus/ScoreScreen.wnd': score_screen(),
        'Menus/PopupReplay.wnd': popup_replay(),
        'Menus/MultiplayerLoadScreen.wnd': multiplayer_load_screen(),
        'Menus/ShellGameLoadScreen.wnd': shell_game_load_screen(),
        'Menus/SinglePlayerLoadScreen.wnd': single_player_load_screen(),
        'Menus/PopupCommunicator.wnd': popup_communicator(),
        'Menus/SkirmishGameOptionsMenu.wnd': skirmish_options(),
        'Menus/SkirmishMapSelectMenu.wnd': map_select('SkirmishMapSelectMenu.wnd', 'SkrimishMapSelectMenuParent',
                                                      'SkirmishMapSelectMenu'),
        'Menus/LanMapSelectMenu.wnd': map_select('LanMapSelectMenu.wnd', 'LanMapSelectMenuParent', 'LanMapSelectMenu'),
        'Menus/LanLobbyMenu.wnd': lan_lobby(),
        'Menus/LanGameOptionsMenu.wnd': lan_game_options(),
        'Menus/NetworkDirectConnect.wnd': network_direct_connect(),
        'Menus/GameInfoWindow.wnd': game_info_window(),
        'Menus/DisconnectScreen.wnd': disconnect_screen(),
        'Menus/MapTransferScreen.wnd': map_transfer_screen(),
        'Menus/CRCMismatch.wnd': crc_mismatch(),
        'Menus/OptionsMenu.wnd': options_menu(),
        'Menus/KeyboardOptionsMenu.wnd': keyboard_options(),
        'Menus/QuitMenu.wnd': quit_menu(),
        'Menus/QuitNoSave.wnd': quit_menu(False),
        'Menus/QuitMessageBox.wnd': message_box('QuitMessageBox.wnd', 'QuitMessageBoxSystem'),
        'Menus/MessageBox.wnd': message_box('MessageBox.wnd', 'MessageBoxSystem'),
        'Menus/PopupSaveLoad.wnd': popup_save_load(),
    }
