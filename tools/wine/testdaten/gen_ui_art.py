# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
"""Interface pictures of the test data set and their MappedImage definitions.

Three textures: testui.tga (button pieces in four states, panels, logo, the old placeholder names),
testcameo.tga (cameos of the objects, command and side symbols, 128 x 96 cells) and testload.tga (the
loading screen). Both painters below fill the same names, so layouts and INI files always resolve:

basic()        standard library only: flat colours with bevels, side-coloured tiles (default data set)
full(built)    with Pillow: metal buttons and panels, logo, drawn symbols; cameos and the loading
               screen come from the Blender build (folder of gen_polyhaven.built_dir) when given
"""
import gen_logic
from gen_util import noise_pixels, tga_bytes

SIZE = 1024
UI = 'testui.tga'
CAMEO = 'testcameo.tga'
LOAD = 'testload.tga'
STATES = ('N', 'H', 'P', 'D')          # normal, hilite (mouse over), pushed, disabled
BTN_END, BTN_MID, BTN_H = 12, 32, 32
CELL_W, CELL_H = 128, 96
LOAD_W, LOAD_H = 1024, 768


def ui_rects():
    """name -> (x, y, w, h) in testui.tga"""
    r = {}
    for i, st in enumerate(STATES):
        y = i * BTN_H
        r['BtnL_' + st] = (0, y, BTN_END, BTN_H)
        r['BtnM_' + st] = (BTN_END, y, BTN_MID, BTN_H)
        r['BtnR_' + st] = (BTN_END + BTN_MID, y, BTN_END, BTN_H)
    r['Panel'] = (64, 0, 256, 256)
    r['PanelDark'] = (320, 0, 256, 256)
    r['TitleBar'] = (576, 0, 256, 32)
    r['Field'] = (576, 32, 128, 24)
    r['Logo'] = (0, 256, 512, 160)
    # placeholder names other INI files still use (Animation2D, misc)
    r['TestIcon'] = (576, 64, 64, 48)
    r['TestButton'] = (640, 64, 64, 32)
    r['TestPanel'] = (704, 64, 64, 64)
    r['TestBar'] = (768, 64, 32, 16)
    r['TestCursor'] = (800, 64, 32, 32)
    return r


ICONS = ('Stop', 'AttackMove', 'Sell', 'Rally', 'Cancel', 'Exit', 'Beacon', 'Generic')


def cameo_names():
    """Cameo cells in order: built objects per side, neutral objects, command symbols, side symbols."""
    names = []
    for side, tmpl, disp, colour in gen_logic.SIDES:
        for part in gen_logic.MODEL_PARTS:
            if part != 'SupplyPile':
                names.append(('Cameo_' + gen_logic.model_name(side, part), side, part))
    names.append(('Cameo_TestSupplyPile', None, 'SupplyPile'))
    names += [('Icon_' + i, None, i) for i in ICONS]
    names += [('Icon_Side_' + side, side, 'Side') for side, tmpl, disp, colour in gen_logic.SIDES]
    return names


def cameo_rects():
    cols = SIZE // CELL_W
    return {name: ((i % cols) * CELL_W, (i // cols) * CELL_H, CELL_W, CELL_H)
            for i, (name, side, part) in enumerate(cameo_names())}


def mapped_images():
    """Data/INI/MappedImages/TextureSize_512/TestUI.ini"""
    out = ['; Interface pictures of the test data set (gen_ui_art)\n']
    for tex, rects in ((UI, ui_rects()), (CAMEO, cameo_rects()), (LOAD, {'TestLoadScreen': (0, 0, LOAD_W, LOAD_H)})):
        for name, (x, y, w, h) in rects.items():
            out.append('MappedImage %s\n  Texture = %s\n  TextureWidth = %d\n  TextureHeight = %d\n'
                       '  Coords = Left:%d Top:%d Right:%d Bottom:%d\nEnd\n' % (name, tex, SIZE, SIZE, x, y, x + w, y + h))
    return '\n'.join(out)


def _side_colour(side):
    return {s: c for s, t, d, c in gen_logic.SIDES}.get(side, (150, 150, 150))


# ---------------------------------------------------------------------------------------------
# basic painter: standard library, RGBA canvas as bytearray

class Canvas:
    def __init__(self, w, h):
        self.w, self.h = w, h
        self.px = bytearray(w * h * 4)

    def rect(self, x, y, w, h, rgba):
        row = bytes((rgba[2], rgba[1], rgba[0], rgba[3])) * w
        for yy in range(y, y + h):
            o = (yy * self.w + x) * 4
            self.px[o:o + w * 4] = row

    def bevel(self, x, y, w, h, base, light, dark, edge=1):
        self.rect(x, y, w, h, base)
        self.rect(x, y, w, edge, light)
        self.rect(x, y, edge, h, light)
        self.rect(x, y + h - edge, w, edge, dark)
        self.rect(x + w - edge, y, edge, h, dark)

    def noise(self, x, y, w, h, base, seed):
        n = noise_pixels(64, base, seed, contrast=0.3)
        for yy in range(h):
            for xx in range(w):
                i = ((yy % 64) * 64 + (xx % 64)) * 3
                o = ((y + yy) * self.w + x + xx) * 4
                self.px[o:o + 4] = bytes((n[i], n[i + 1], n[i + 2], 255))

    def tga(self):
        return tga_bytes(self.w, self.h, bytes(self.px), 32)


BASIC_BUTTON = {'N': (60, 76, 110), 'H': (84, 104, 150), 'P': (40, 50, 76), 'D': (50, 52, 58)}


def basic():
    """rel path -> bytes for the three textures, standard library only."""
    ui = Canvas(SIZE, SIZE)
    r = ui_rects()
    for st, c in BASIC_BUTTON.items():
        light = tuple(min(255, v + 60) for v in c) + (255,)
        dark = tuple(v // 2 for v in c) + (255,)
        if st == 'P':
            light, dark = dark, light
        for piece in 'LMR':
            x, y, w, h = r['Btn%s_%s' % (piece, st)]
            ui.rect(x, y, w, h, c + (230,))
            ui.rect(x, y, w, 1, light)
            ui.rect(x, y + h - 1, w, 1, dark)
            if piece == 'L':
                ui.rect(x, y, 1, h, light)
            if piece == 'R':
                ui.rect(x + w - 1, y, 1, h, dark)
    for name, c in (('Panel', (40, 44, 58, 220)), ('PanelDark', (20, 22, 30, 240)), ('TitleBar', (60, 76, 110, 255)),
                    ('Field', (16, 18, 24, 255))):
        x, y, w, h = r[name]
        ui.bevel(x, y, w, h, c, (200, 200, 210, 255), (90, 90, 100, 255))
    for k, (name, base) in enumerate((('TestIcon', (90, 120, 170)), ('TestButton', (70, 90, 140)), ('TestPanel', (44, 48, 64)),
                                      ('TestBar', (80, 170, 90)), ('TestCursor', (240, 240, 240)))):
        ui.noise(*r[name], base, 300 + k)
    cam = Canvas(SIZE, SIZE)
    for k, (name, (x, y, w, h)) in enumerate(cameo_rects().items()):
        side = cameo_names()[k][1]
        base = _side_colour(side) if side else (110, 110, 120)
        cam.noise(x + 2, y + 2, w - 4, h - 4, base, 500 + k)
    load = Canvas(SIZE, SIZE)
    load.noise(0, 0, LOAD_W, LOAD_H, (30, 36, 52), 700)
    return {'Art/Textures/' + UI: ui.tga(), 'Art/Textures/' + CAMEO: cam.tga(), 'Art/Textures/' + LOAD: load.tga()}


# ---------------------------------------------------------------------------------------------
# full painter: Pillow

FONT = '/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf'
FONT_PLAIN = '/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf'


def _font(size, bold=True):
    from PIL import ImageFont
    try:
        return ImageFont.truetype(FONT if bold else FONT_PLAIN, size)
    except OSError:
        return ImageFont.load_default()


def _gradient(w, h, top, bottom, alpha=255):
    from PIL import Image
    img = Image.new('RGBA', (w, h))
    px = img.load()
    for y in range(h):
        t = y / max(1, h - 1)
        c = tuple(int(top[k] + (bottom[k] - top[k]) * t) for k in range(3)) + (alpha,)
        for x in range(w):
            px[x, y] = c
    return img


def _button_strip(state):
    """One button 2 * BTN_END + BTN_MID wide; cut into the three pieces afterwards."""
    from PIL import ImageDraw
    w = 2 * BTN_END + BTN_MID
    top, bottom, edge, glow = {
        'N': ((84, 92, 84), (40, 44, 40), (150, 140, 96), None),
        'H': ((110, 120, 100), (54, 60, 50), (230, 190, 90), (255, 210, 120)),
        'P': ((34, 38, 34), (70, 76, 68), (190, 160, 90), None),
        'D': ((64, 64, 66), (44, 44, 46), (90, 90, 92), None),
    }[state]
    img = _gradient(w, BTN_H, top, bottom)
    d = ImageDraw.Draw(img)
    d.rectangle((0, 0, w - 1, BTN_H - 1), outline=edge + (255,))
    d.line((1, 1, w - 2, 1), fill=tuple(min(255, c + 50) for c in top) + (255,))
    d.line((1, BTN_H - 2, w - 2, BTN_H - 2), fill=tuple(c // 2 for c in bottom) + (255,))
    if glow:
        d.rectangle((1, 1, w - 2, BTN_H - 2), outline=glow + (120,))
    # rivets on the end pieces
    for x in (5, w - 6):
        d.ellipse((x - 2, BTN_H // 2 - 2, x + 2, BTN_H // 2 + 2), fill=edge + (255,), outline=(20, 20, 20, 255))
    return img


def _panel(dark, metal):
    from PIL import Image, ImageDraw, ImageEnhance
    size = 256
    if metal:
        with Image.open(metal) as m:
            base = m.convert('RGB').resize((size, size), Image.LANCZOS)
        base = ImageEnhance.Brightness(ImageEnhance.Color(base).enhance(0.3)).enhance(0.35 if dark else 0.55)
        img = base.convert('RGBA')
        img.putalpha(235 if dark else 225)
    else:
        img = _gradient(size, size, (52, 56, 52), (30, 32, 30), 225)
    d = ImageDraw.Draw(img)
    d.rectangle((0, 0, size - 1, size - 1), outline=(150, 140, 96, 255), width=2)
    d.rectangle((4, 4, size - 5, size - 5), outline=(20, 20, 20, 200), width=1)
    for x, y in ((8, 8), (size - 9, 8), (8, size - 9), (size - 9, size - 9)):
        d.ellipse((x - 3, y - 3, x + 3, y + 3), fill=(150, 140, 96, 255), outline=(20, 20, 20, 255))
    return img


def _logo():
    from PIL import Image, ImageDraw, ImageFilter
    w, h = 512, 160
    img = Image.new('RGBA', (w, h), (0, 0, 0, 0))
    big = _font(92)
    text = 'GZH NEO'
    box = ImageDraw.Draw(img).textbbox((0, 0), text, font=big)
    tx = (w - (box[2] - box[0])) // 2 - box[0]
    mask = Image.new('L', (w, h), 0)
    ImageDraw.Draw(mask).text((tx, 6), text, font=big, fill=255)
    shadow = Image.new('RGBA', (w, h), (0, 0, 0, 0))
    shadow.putalpha(mask.filter(ImageFilter.GaussianBlur(4)))
    img.alpha_composite(shadow, (4, 5))
    outline = mask.filter(ImageFilter.MaxFilter(5))
    edge = Image.new('RGBA', (w, h), (20, 22, 18, 255))
    edge.putalpha(outline)
    img.alpha_composite(edge)
    fill = _gradient(w, h, (240, 236, 210), (150, 130, 70))
    fill.putalpha(mask)
    img.alpha_composite(fill)
    sub = _font(26)
    label = 'T E S T D A T E N'
    box = ImageDraw.Draw(img).textbbox((0, 0), label, font=sub)
    d = ImageDraw.Draw(img)
    sx = (w - (box[2] - box[0])) // 2
    d.text((sx + 2, 118), label, font=sub, fill=(0, 0, 0, 200))
    d.text((sx, 116), label, font=sub, fill=(230, 190, 90, 255))
    return img


def _icon(kind, side=None):
    """A symbol on a dark tile, CELL_W x CELL_H."""
    from PIL import ImageDraw
    img = _gradient(CELL_W, CELL_H, (58, 62, 58), (26, 28, 26))
    d = ImageDraw.Draw(img)
    cx, cy = CELL_W // 2, CELL_H // 2
    white, red, amber = (235, 235, 225, 255), (200, 50, 40, 255), (230, 190, 90, 255)
    if kind == 'Stop':
        import math
        pts = [(cx + 32 * math.cos(math.radians(22.5 + 45 * i)), cy + 32 * math.sin(math.radians(22.5 + 45 * i))) for i in range(8)]
        d.polygon(pts, fill=red, outline=white)
        d.rectangle((cx - 18, cy - 5, cx + 18, cy + 5), fill=white)
    elif kind == 'AttackMove':
        d.ellipse((cx - 26, cy - 26, cx + 26, cy + 26), outline=red, width=5)
        for a, b in (((cx - 36, cy), (cx - 12, cy)), ((cx + 12, cy), (cx + 36, cy)), ((cx, cy - 36), (cx, cy - 12)), ((cx, cy + 12), (cx, cy + 36))):
            d.line((a, b), fill=white, width=5)
    elif kind == 'Sell':
        f = _font(64)
        d.text((cx, cy), '$', font=f, fill=amber, anchor='mm', stroke_width=3, stroke_fill=(20, 20, 20, 255))
    elif kind == 'Rally':
        d.line((cx - 16, cy + 34, cx - 16, cy - 34), fill=white, width=5)
        d.polygon([(cx - 14, cy - 34), (cx + 30, cy - 22), (cx - 14, cy - 8)], fill=amber)
    elif kind == 'Cancel':
        d.line((cx - 26, cy - 26, cx + 26, cy + 26), fill=red, width=10)
        d.line((cx - 26, cy + 26, cx + 26, cy - 26), fill=red, width=10)
    elif kind == 'Exit':
        d.rectangle((cx - 34, cy - 26, cx + 2, cy + 26), outline=white, width=5)
        d.polygon([(cx - 6, cy - 14), (cx + 20, cy - 14), (cx + 20, cy - 26), (cx + 40, cy), (cx + 20, cy + 26),
                   (cx + 20, cy + 14), (cx - 6, cy + 14)], fill=amber)
    elif kind == 'Beacon':
        d.polygon([(cx, cy - 34), (cx + 24, cy), (cx, cy + 34), (cx - 24, cy)], fill=amber, outline=white)
    elif kind == 'Side':
        c = _side_colour(side) + (255,)
        d.polygon([(cx - 30, cy - 34), (cx + 30, cy - 34), (cx + 30, cy + 2), (cx, cy + 38), (cx - 30, cy + 2)],
                  fill=c, outline=white)
        d.text((cx, cy - 4), side[4], font=_font(44), fill=white, anchor='mm', stroke_width=2, stroke_fill=(20, 20, 20, 255))
    else:
        d.ellipse((cx - 14, cy - 14, cx + 14, cy + 14), fill=white)
    d.rectangle((0, 0, CELL_W - 1, CELL_H - 1), outline=(150, 140, 96, 255), width=2)
    return img


def _cameo(path, side):
    from PIL import Image, ImageDraw
    img = _gradient(CELL_W, CELL_H, (70, 76, 82), (30, 32, 36))
    if path:
        with Image.open(path) as pic:
            img.alpha_composite(pic.convert('RGBA').resize((CELL_W, CELL_H), Image.LANCZOS))
    border = _side_colour(side) if side else (150, 140, 96)
    ImageDraw.Draw(img).rectangle((0, 0, CELL_W - 1, CELL_H - 1), outline=border + (255,), width=3)
    return img


def full(built=None, metal=None, supply_cameo=None):
    """rel path -> bytes for the three textures, painted with Pillow. built: folder of the Blender build
    (cameos <part>_<side>_cameo.png, scene.png), metal: a texture for the panels."""
    import os
    from PIL import Image
    ui = Image.new('RGBA', (SIZE, SIZE), (0, 0, 0, 0))
    r = ui_rects()
    for st in STATES:
        strip = _button_strip(st)
        for piece, x0, w in (('L', 0, BTN_END), ('M', BTN_END, BTN_MID), ('R', BTN_END + BTN_MID, BTN_END)):
            x, y, _, _ = r['Btn%s_%s' % (piece, st)]
            ui.paste(strip.crop((x0, 0, x0 + w, BTN_H)), (x, y))
    ui.paste(_panel(False, metal), r['Panel'][:2])
    ui.paste(_panel(True, metal), r['PanelDark'][:2])
    x, y, w, h = r['TitleBar']
    ui.paste(_gradient(w, h, (110, 100, 64), (60, 54, 34)), (x, y))
    x, y, w, h = r['Field']
    ui.paste(_gradient(w, h, (14, 16, 14), (28, 30, 28), 240), (x, y))
    ui.paste(_logo(), r['Logo'][:2])
    for name, base in (('TestIcon', (90, 120, 170)), ('TestButton', (70, 90, 140)), ('TestPanel', (44, 48, 64)),
                       ('TestBar', (80, 170, 90)), ('TestCursor', (240, 240, 240))):
        x, y, w, h = r[name]
        ui.paste(_gradient(w, h, base, tuple(c // 2 for c in base)), (x, y))
    cam = Image.new('RGBA', (SIZE, SIZE), (0, 0, 0, 0))
    rects = cameo_rects()
    for name, side, part in cameo_names():
        if name.startswith('Icon_'):
            pic = _icon(part, side)
        else:
            path = None
            if part == 'SupplyPile':
                path = supply_cameo
            elif built:
                path = os.path.join(built, '%s_%s_cameo.png' % (part, side))
            pic = _cameo(path if path and os.path.exists(path) else None, side)
        cam.paste(pic, rects[name][:2])
    load = Image.new('RGBA', (SIZE, SIZE), (10, 10, 16, 255))
    scene = os.path.join(built, 'scene.png') if built else None
    if scene and os.path.exists(scene):
        with Image.open(scene) as pic:
            load.paste(pic.convert('RGBA').resize((LOAD_W, LOAD_H), Image.LANCZOS), (0, 0))
    else:
        load.paste(_gradient(LOAD_W, LOAD_H, (40, 46, 40), (12, 14, 12)), (0, 0))
    logo = _logo()
    load.alpha_composite(logo, ((LOAD_W - logo.width) // 2, 24))
    out = {}
    for tex, img in ((UI, ui), (CAMEO, cam), (LOAD, load)):
        out['Art/Textures/' + tex] = tga_bytes(SIZE, SIZE, img.tobytes('raw', 'BGRA'), 32)
    return out
