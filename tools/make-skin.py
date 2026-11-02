#!/usr/bin/env python3
"""Draws PowerPlay's own Winamp 2.x skin, "Power Station": a brushed-steel
control panel with amber displays, status lamps, bakelite push buttons, LED
faders, a lightning-bolt badge and a hazard-striped kick plate.

    tools/make-skin.py data/PowerStation.wsz [preview.png]

Writes the classic skin files (main, titlebar, cbuttons, numbers, text,
playpaus, monoster, volume, balance, posbar, shufrep .bmp and viscolor.txt)
into a .wsz. Everything is drawn here, so the skin is PowerPlay's own (MIT
licence). Shapes are drawn 4x larger and scaled down; digits, lettering and
LEDs are drawn pixel by pixel. Needs Pillow; the title lettering uses DejaVu
Sans Condensed Bold when it can find it.
"""
import io
import os
import random
import sys
import zipfile

from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageFont

S = 4                       # supersampling
W, H = 275, 116             # the main window

LCD_BG = (16, 11, 6)
GHOST = (40, 27, 12)
AMBER = (255, 176, 48)
AMBER_HALO = (78, 46, 12)
GREEN = (90, 235, 100)
RED = (255, 72, 50)
IVORY = (226, 222, 208)
ENGRAVE_DARK = (52, 54, 58)
ENGRAVE_LIGHT = (206, 209, 213)


# ---------------------------------------------------------------- canvas

class Canvas:
    """A supersampled RGBA drawing of part of the window, at (ox, oy) in
    window pixels; every coordinate given to it is a window pixel."""

    def __init__(self, w, h, ox=0, oy=0, background=None):
        self.w, self.h, self.ox, self.oy = w, h, ox, oy
        if background is None:
            self.img = Image.new("RGBA", (w * S, h * S), (0, 0, 0, 255))
        else:
            self.img = background.copy()

    def box(self, x0, y0, x1, y1):
        return [(x0 - self.ox) * S, (y0 - self.oy) * S,
                (x1 - self.ox) * S - 1, (y1 - self.oy) * S - 1]

    def pt(self, x, y):
        return ((x - self.ox) * S, (y - self.oy) * S)

    def mask(self):
        return Image.new("L", self.img.size, 0)

    def fill(self, mask, source):
        if isinstance(source, tuple):
            source = Image.new("RGBA", self.img.size, rgba(source))
        layer = source.copy()
        layer.putalpha(ImageChops.multiply(mask, source.getchannel("A")))
        self.img.alpha_composite(layer)

    def rrect_mask(self, x0, y0, x1, y1, r):
        m = self.mask()
        ImageDraw.Draw(m).rounded_rectangle(self.box(x0, y0, x1, y1),
                                            radius=r * S, fill=255)
        return m

    def ellipse_mask(self, cx, cy, r):
        m = self.mask()
        ImageDraw.Draw(m).ellipse(self.box(cx - r, cy - r, cx + r, cy + r),
                                  fill=255)
        return m

    def polygon_mask(self, points):
        m = self.mask()
        ImageDraw.Draw(m).polygon([self.pt(x, y) for x, y in points],
                                  fill=255)
        return m

    def vgrad(self, y0, y1, top, bottom):
        """A vertical gradient over the canvas, top at y0, bottom at y1."""
        return gradient(self.img.size, (y0 - self.oy) * S,
                        (y1 - self.oy) * S, top, bottom, vertical=True)

    def rgrad(self, cx, cy, r, inner, outer):
        """A radial gradient around (cx, cy)."""
        size = self.img.size
        g = Image.radial_gradient("L").resize((int(2 * r * S),) * 2)
        lut = [rgba(lerp(inner, outer, min(1, v / 255 * 1.42)))
               for v in range(256)]
        out = Image.new("RGBA", size, rgba(outer))
        tile = Image.merge("RGBA", [g.point([c[i] for c in lut])
                                    for i in range(4)])
        out.paste(tile, (int((cx - r - self.ox) * S),
                         int((cy - r - self.oy) * S)))
        return out

    def down(self):
        return self.img.resize((self.w, self.h), Image.BOX).convert("RGB")


def rgba(c):
    return tuple(int(v) for v in c) + ((255,) if len(c) == 3 else ())


def lerp(a, b, t):
    return tuple(a[i] + (b[i] - a[i]) * t for i in range(len(a)))


def gradient(size, start, end, top, bottom, vertical=True):
    w, h = size
    length = h if vertical else w
    line = Image.new("RGBA", (1, length) if vertical else (length, 1))
    span = max(1, end - start)
    for i in range(length):
        t = min(1, max(0, (i - start) / span))
        line.putpixel((0, i) if vertical else (i, 0), rgba(lerp(top, bottom, t)))
    return line.resize(size, Image.NEAREST)


def brushed(w, h, top, bottom, seed):
    """Brushed steel at 1x: long horizontal streaks over a gradient."""
    rng = random.Random(seed)
    noise = Image.new("L", (max(1, w // 10), h))
    noise.putdata([clamp(128 + rng.gauss(0, 60))
                   for _ in range(noise.width * h)])
    streaks = noise.resize((w, h), Image.BICUBIC)
    fine = Image.new("L", (w, h))
    fine.putdata([clamp(128 + rng.gauss(0, 20)) for _ in range(w * h)])
    base = gradient((w, h), 0, h, top, bottom).convert("RGB")
    px = base.load()
    sp, fp = streaks.load(), fine.load()
    for y in range(h):
        for x in range(w):
            d = (sp[x, y] - 128) * 0.10 + (fp[x, y] - 128) * 0.06
            r, g, b = px[x, y]
            px[x, y] = (clamp(r + d), clamp(g + d), clamp(b + d + 1))
    return base


def clamp(v):
    return max(0, min(255, int(round(v))))


# ------------------------------------------------------------ pixel font

# 4 x 5 letters in 5 x 6 cells: the skin's text.bmp, and labels.
FONT = {
    "a": ".##.|#..#|####|#..#|#..#", "b": "###.|#..#|###.|#..#|###.",
    "c": ".###|#...|#...|#...|.###", "d": "###.|#..#|#..#|#..#|###.",
    "e": "####|#...|###.|#...|####", "f": "####|#...|###.|#...|#...",
    "g": ".###|#...|#.##|#..#|.###", "h": "#..#|#..#|####|#..#|#..#",
    "i": "###.|.#..|.#..|.#..|###.", "j": "..##|...#|...#|#..#|.##.",
    "k": "#..#|#.#.|##..|#.#.|#..#", "l": "#...|#...|#...|#...|####",
    "m": "#..#|####|####|#..#|#..#", "n": "#..#|##.#|#.##|#..#|#..#",
    "o": ".##.|#..#|#..#|#..#|.##.", "p": "###.|#..#|###.|#...|#...",
    "q": ".##.|#..#|#..#|#.#.|.#.#", "r": "###.|#..#|###.|#.#.|#..#",
    "s": ".###|#...|.##.|...#|###.", "t": "###.|.#..|.#..|.#..|.#..",
    "u": "#..#|#..#|#..#|#..#|.##.", "v": "#..#|#..#|#..#|.##.|.##.",
    "w": "#..#|#..#|####|####|#..#", "x": "#..#|#..#|.##.|#..#|#..#",
    "y": "#.#.|#.#.|.#..|.#..|.#..", "z": "####|..#.|.#..|#...|####",
    "0": ".##.|#.##|##.#|#..#|.##.", "1": ".#..|##..|.#..|.#..|###.",
    "2": "###.|...#|.##.|#...|####", "3": "###.|...#|.##.|...#|###.",
    "4": "#..#|#..#|####|...#|...#", "5": "####|#...|###.|...#|###.",
    "6": ".##.|#...|###.|#..#|.##.", "7": "####|...#|..#.|.#..|.#..",
    "8": ".##.|#..#|.##.|#..#|.##.", "9": ".##.|#..#|.###|...#|.##.",
    '"': "#.#.|#.#.|....|....|....", "@": ".##.|#.##|#.##|#...|.##.",
    "\x85": "....|....|....|....|#.#.", ".": "....|....|....|....|.#..",
    ":": "....|.#..|....|.#..|....", "(": "..#.|.#..|.#..|.#..|..#.",
    ")": ".#..|..#.|..#.|..#.|.#..", "-": "....|....|###.|....|....",
    "'": ".#..|.#..|....|....|....", "!": ".#..|.#..|.#..|....|.#..",
    "_": "....|....|....|....|####", "+": "....|.#..|###.|.#..|....",
    "\\": "#...|.#..|.#..|..#.|..#.", "/": "..#.|..#.|.#..|.#..|#...",
    "[": "##..|#...|#...|#...|##..", "]": "##..|.#..|.#..|.#..|##..",
    "^": ".#..|#.#.|....|....|....", "&": ".#..|#.#.|.#..|#.#.|.#.#",
    "%": "#..#|..#.|.#..|#...|#..#", ",": "....|....|....|.#..|#...",
    "=": "....|###.|....|###.|....", "$": ".###|##..|.##.|..##|###.",
    "#": "#.#.|####|#.#.|####|#.#.", "\x86": ".##.|.##.|#..#|####|#..#",
    "\x87": "#..#|.##.|#..#|#..#|.##.", "\x88": "#..#|.##.|#..#|####|#..#",
    "?": "###.|...#|.##.|....|.#..", "*": "....|#.#.|.#..|#.#.|....",
    " ": "....|....|....|....|....",
}

# 3 x 5 letters for tight spaces (mono/stereo, L/R)
NARROW = {
    "s": "###|#..|###|..#|###", "t": "###|.#.|.#.|.#.|.#.",
    "e": "###|#..|##.|#..|###", "r": "##.|#.#|##.|#.#|#.#",
    "o": "###|#.#|#.#|#.#|###", "n": "#.#|###|###|#.#|#.#",
    "m": "#...#|##.##|#.#.#|#...#|#...#", "l": "#..|#..|#..|#..|###",
    "k": "#.#|#.#|##.|#.#|#.#", "b": "##.|#.#|##.|#.#|##.",
    "p": "##.|#.#|##.|#..|#..", "h": "#.#|#.#|###|#.#|#.#",
    "z": "###|..#|.#.|#..|###",
    "a": ".#.|#.#|###|#.#|#.#", "i": "###|.#.|.#.|.#.|###",
    "d": "##.|#.#|#.#|#.#|##.", "v": "#.#|#.#|#.#|#.#|.#.",
}

# The clutter bar's buttons: letter, top (from the bar's top at y 22) and
# height; lit, each comes from titlebar.bmp at x 304 + 8 * index, y 44 + top
CLUTTER = [("o", 3, 8), ("a", 11, 7), ("i", 18, 7), ("d", 25, 8),
           ("v", 33, 7)]

TEXT_ROWS = ["abcdefghijklmnopqrstuvwxyz\"@  ",
             "0123456789\x85.:()-'!_+\\/[]^&%,=$#",
             "\x86\x87\x88?*"]


def glyph(font, c):
    return font.get(c, font.get(" ", "")).split("|")


def text_pixels(text, font=FONT, spacing=1):
    """The lit pixels of a line of text, and its width."""
    pixels, x = [], 0
    for c in text.lower():
        rows = glyph(font, c)
        for y, row in enumerate(rows):
            for dx, v in enumerate(row):
                if v == "#":
                    pixels.append((x + dx, y))
        x += len(rows[0]) + spacing
    return pixels, x - spacing


def draw_text(img, text, x, y, color, font=FONT, halo=None):
    px = img.load()
    pixels, _ = text_pixels(text, font)
    lit = {(x + dx, y + dy) for dx, dy in pixels}
    if halo:
        for (lx, ly) in lit:
            for nx, ny in ((lx + 1, ly), (lx - 1, ly), (lx, ly + 1),
                           (lx, ly - 1)):
                if (nx, ny) not in lit and in_img(img, nx, ny):
                    px[nx, ny] = mix(px[nx, ny], halo, 0.6)
    for (lx, ly) in lit:
        if in_img(img, lx, ly):
            px[lx, ly] = color


def engrave(img, text, x, y, font=FONT):
    draw_text(img, text, x, y + 1, ENGRAVE_LIGHT, font)
    draw_text(img, text, x, y, ENGRAVE_DARK, font)


def in_img(img, x, y):
    return 0 <= x < img.width and 0 <= y < img.height


def mix(a, b, t):
    return tuple(clamp(a[i] + (b[i] - a[i]) * t) for i in range(3))


# ------------------------------------------------------------ the panel

def screw(c, cx, cy, r):
    c.fill(c.ellipse_mask(cx + 0.3, cy + 0.4, r + 0.4), (30, 30, 32, 160))
    c.fill(c.ellipse_mask(cx, cy, r),
           c.rgrad(cx - r * 0.4, cy - r * 0.4, r * 1.6, (235, 236, 238),
                   (95, 97, 102)))
    m = c.mask()
    ImageDraw.Draw(m).line([c.pt(cx - r * 0.7, cy - r * 0.35),
                            c.pt(cx + r * 0.7, cy + r * 0.35)],
                           fill=255, width=int(S * 0.8))
    c.fill(m, (45, 45, 48))


def recess(c, x0, y0, x1, y1, r, fill):
    """A window set into the panel: a light lip below, a dark rim, the
    fill, and a shadow along its top."""
    c.fill(c.rrect_mask(x0 - 1, y0 - 1, x1 + 1, y1 + 1, r + 1),
           (235, 237, 240, 150))
    c.fill(c.rrect_mask(x0 - 1, y0 - 1, x1 + 1, y1, r + 1), (36, 37, 40))
    c.fill(c.rrect_mask(x0, y0, x1, y1, r), fill)


def lcd_shadow(c, x0, y0, x1):
    """The glass's inner shadow, in the top two rows only so the rows
    text and digits go in stay flat."""
    c.fill(c.rrect_mask(x0, y0, x1, y0 + 2, 1),
           c.vgrad(y0, y0 + 2, (0, 0, 0, 200), (0, 0, 0, 0)))


def hazard(c, x0, y0, x1, y1):
    c.fill(c.rrect_mask(x0 - 1, y0 - 1, x1 + 1, y1 + 1, 1), (28, 27, 25))
    stripes = Image.new("RGBA", c.img.size, rgba((240, 186, 28)))
    d = ImageDraw.Draw(stripes)
    for x in range(x0 - 10, x1 + 10, 6):
        d.polygon([c.pt(x, y1), c.pt(x + 3, y1), c.pt(x + 3 + (y1 - y0), y0),
                   c.pt(x + (y1 - y0), y0)], fill=rgba((26, 25, 24)))
    c.fill(c.rrect_mask(x0, y0, x1, y1, 0.5), stripes)
    c.fill(c.rrect_mask(x0, y0, x1, y0 + 1.5, 0.5),
           c.vgrad(y0, y0 + 1.5, (255, 255, 255, 90), (255, 255, 255, 0)))


def badge(c, cx, cy, r):
    """The lightning-bolt badge."""
    c.fill(c.ellipse_mask(cx + 0.4, cy + 0.6, r + 0.6), (20, 20, 22, 150))
    c.fill(c.ellipse_mask(cx, cy, r),
           c.rgrad(cx - r * 0.5, cy - r * 0.6, r * 2, (245, 246, 248),
                   (80, 82, 88)))
    c.fill(c.ellipse_mask(cx, cy, r - 2.2), (24, 24, 27))
    c.fill(c.ellipse_mask(cx, cy, r - 2.6),
           c.rgrad(cx, cy - 2, r * 1.3, (58, 54, 50), (16, 15, 15)))
    bolt = [(1.6, -8.2), (-4.6, 1.0), (-0.6, 1.0), (-2.2, 8.2), (4.6, -1.6),
            (0.6, -1.6), (3.2, -8.2)]
    c.fill(c.polygon_mask([(cx + x + 0.4, cy + y + 0.5) for x, y in bolt]),
           (0, 0, 0, 170))
    c.fill(c.polygon_mask([(cx + x, cy + y) for x, y in bolt]),
           c.vgrad(cy - 8, cy + 8, (255, 230, 90), (235, 140, 20)))
    # The glaze's highlight
    m = c.mask()
    ImageDraw.Draw(m).ellipse(c.box(cx - r + 3, cy - r + 2.6, cx + r - 3,
                                    cy + 1), fill=60)
    c.fill(m, (255, 255, 255))


def hex_bolt(c, cx, cy, r):
    import math
    points = [(cx + r * math.cos(math.radians(30 + 60 * i)),
               cy + r * math.sin(math.radians(30 + 60 * i))) for i in range(6)]
    c.fill(c.polygon_mask([(x + 0.3, y + 0.4) for x, y in points]),
           (20, 20, 20, 150))
    c.fill(c.polygon_mask(points),
           c.rgrad(cx - r * 0.5, cy - r * 0.5, r * 1.8, (230, 231, 233),
                   (90, 92, 96)))
    c.fill(c.ellipse_mask(cx, cy, r * 0.45), (70, 72, 76))


def face_canvas():
    c = Canvas(W, H)
    steel = brushed(W, H, (178, 182, 187), (128, 131, 136), 7)
    c.img = steel.resize((W * S, H * S), Image.NEAREST).convert("RGBA")
    # The panel's edges
    edge = c.mask()
    d = ImageDraw.Draw(edge)
    d.rectangle(c.box(0, 0, W, H), outline=255, width=S)
    c.fill(edge, (34, 35, 38))
    hl = c.mask()
    d = ImageDraw.Draw(hl)
    d.line([c.pt(1, H - 1.5), c.pt(1, 14), c.pt(W - 1.5, 14)], fill=150,
           width=S // 2)
    c.fill(hl, (255, 255, 255))
    sh = c.mask()
    ImageDraw.Draw(sh).line([c.pt(1.5, H - 1.5), c.pt(W - 1.5, H - 1.5),
                             c.pt(W - 1.5, 14)], fill=120, width=S // 2)
    c.fill(sh, (0, 0, 0))

    # Displays
    recess(c, 6, 17, 105, 69, 2, LCD_BG)                # time, levels
    lcd_shadow(c, 6, 17, 105)
    recess(c, 108, 23, 268, 37, 1.5, LCD_BG)            # title
    lcd_shadow(c, 108, 23, 268)
    recess(c, 108, 40, 129, 53, 1, LCD_BG)              # kbps
    recess(c, 152, 40, 169, 53, 1, LCD_BG)              # kHz
    recess(c, 211, 40, 267, 54, 1, LCD_BG)              # mono, stereo

    # Slots for the faders and the position lever
    recess(c, 108, 59, 175, 68, 1.5, (22, 22, 24))
    recess(c, 178, 59, 215, 68, 1.5, (22, 22, 24))
    recess(c, 17, 74, 264, 80, 2, (20, 20, 22))

    # Trays the buttons sit in
    recess(c, 15, 87, 131, 107, 2.5, (26, 26, 28))
    recess(c, 135, 88, 159, 106, 2.5, (26, 26, 28))
    recess(c, 163, 88, 239, 105, 2.5, (26, 26, 28))

    badge(c, 255.5, 96.5, 12)
    hazard(c, 12, 110, 263, 114)
    hex_bolt(c, 6.5, 112, 2.6)
    hex_bolt(c, 268.5, 112, 2.6)
    return c


def face_details(img):
    """Pixel details on the drawn panel: the time display's unlit
    segments, labels, and the clutter bar's dots."""
    for x in (48, 60, 78, 90):
        draw_segments(img, x, 26, 8, ghost_only=True)
    px = img.load()
    for y in (29, 30, 34, 35):      # the colon
        for x in (72, 73):
            px[x, y] = mix(AMBER, LCD_BG, 0.25)
    for x in range(39, 44):         # the minus, unlit
        px[x, 32] = GHOST
    # The level display's unlit LEDs
    for row in (44, 52):
        for x in range(24, 100):
            if x % 3 != 2:
                for y in range(row, row + 6):
                    px[x, y] = GHOST
    draw_text(img, "l", 19, 44, mix(AMBER, LCD_BG, 0.55), NARROW)
    draw_text(img, "r", 19, 52, mix(AMBER, LCD_BG, 0.55), NARROW)
    for letter, top, height in CLUTTER:     # clutter bar, unlit
        draw_text(img, letter, 12, 22 + top + (height - 5) // 2, GHOST,
                  NARROW)
    engrave(img, "kbps", 133, 44, NARROW)
    engrave(img, "khz", 173, 44, NARROW)


# 7-segment digits, 9 x 13
SEGMENTS = {
    "a": [(x, 0) for x in range(2, 7)] + [(x, 1) for x in range(3, 6)],
    "g": [(x, 6) for x in range(2, 7)] + [(x, y) for x in range(3, 6)
                                          for y in (5, 7)],
    "d": [(x, 12) for x in range(2, 7)] + [(x, 11) for x in range(3, 6)],
    "f": [(0, y) for y in range(1, 6)] + [(1, y) for y in range(2, 5)],
    "b": [(8, y) for y in range(1, 6)] + [(7, y) for y in range(2, 5)],
    "e": [(0, y) for y in range(7, 12)] + [(1, y) for y in range(8, 11)],
    "c": [(8, y) for y in range(7, 12)] + [(7, y) for y in range(8, 11)],
}
DIGITS = ["abcdef", "bc", "abged", "abgcd", "fgbc", "afgcd", "afgedc", "abc",
          "abcdefg", "abfgcd", ""]


def draw_segments(img, x0, y0, digit, ghost_only=False):
    px = img.load()
    for name, pixels in SEGMENTS.items():
        lit = not ghost_only and name in DIGITS[digit]
        for x, y in pixels:
            px[x0 + x, y0 + y] = AMBER if lit else GHOST
        if lit:
            # A faint glow beside the lit segment
            for x, y in pixels:
                for nx, ny in ((x + 1, y), (x - 1, y), (x, y + 1),
                               (x, y - 1)):
                    if 0 <= nx < 9 and 0 <= ny < 13 \
                            and px[x0 + nx, y0 + ny] in (LCD_BG,):
                        px[x0 + nx, y0 + ny] = AMBER_HALO


# --------------------------------------------------------- the title bar

def title_font():
    for path in ("/usr/share/fonts/TTF/DejaVuSansCondensed-Bold.ttf",
                 "/usr/share/fonts/truetype/dejavu/DejaVuSansCondensed-Bold.ttf",
                 "/boot/system/data/fonts/ttfonts/DejaVuSansCondensed-Bold.ttf"):
        if os.path.exists(path):
            return ImageFont.truetype(path, 8 * S)
    return None


def title_bar(active, pressed=None):
    c = Canvas(W, 14)
    c.img = brushed(W, 14, (92, 95, 101), (52, 54, 59), 3).resize(
        (W * S, 14 * S), Image.NEAREST).convert("RGBA")
    c.fill(c.rrect_mask(0, 0, W, 1, 0), (150, 153, 158))
    c.fill(c.rrect_mask(0, 13, W, 14, 0), (22, 22, 24))
    c.fill(c.rrect_mask(0, 0, 1, 14, 0), (34, 35, 38))
    c.fill(c.rrect_mask(W - 1, 0, W, 14, 0), (34, 35, 38))

    # Lettering, embossed, with a small bolt
    label = "POWERPLAY"
    font = title_font()
    text_w = 0
    if font is not None:
        text_w = font.getbbox(label)[2] / S
    else:
        text_w = text_pixels(label)[1]
    tx = (W - text_w) / 2 + 4
    bolt = [(1.2, -4.2), (-2.4, 0.6), (-0.3, 0.6), (-1.2, 4.2), (2.4, -0.9),
            (0.3, -0.9), (1.8, -4.2)]
    bx = tx - 7
    c.fill(c.polygon_mask([(bx + x, 7 + y) for x, y in bolt]),
           (255, 196, 60) if active else (130, 112, 80))
    lines_color = (30, 31, 34) if active else (44, 45, 48)
    for y in (5, 8):
        for x0, x1 in ((22, bx - 6), (tx + text_w + 5, 236)):
            c.fill(c.rrect_mask(x0, y, x1, y + 1, 0), lines_color)
            c.fill(c.rrect_mask(x0, y + 1, x1, y + 2, 0), (120, 123, 128, 160))
    if font is not None:
        for (dx, dy, color) in ((0, 0.6, (20, 20, 22, 255)),
                                (0, 0, (226, 228, 232, 255) if active
                                 else (138, 140, 145, 255))):
            layer = Image.new("RGBA", c.img.size, (0, 0, 0, 0))
            ImageDraw.Draw(layer).text(c.pt(tx + dx, 2.4 + dy), label,
                                       font=font, fill=color)
            c.img.alpha_composite(layer)
    for x in (17, 240 - 3):          # rivets
        c.fill(c.ellipse_mask(x + 0.3, 7.3, 1.6), (20, 20, 20, 160))
        c.fill(c.ellipse_mask(x, 7, 1.5),
               c.rgrad(x - 0.6, 6.4, 2.4, (230, 230, 232), (90, 92, 96)))

    # The pilot lamp, which is also the menu button
    lit = active
    c.fill(c.ellipse_mask(10.5, 7.5, 4.4),
           c.rgrad(9.5, 6.5, 6, (240, 240, 242), (70, 72, 76)))
    if lit:
        inner = (255, 235, 200) if pressed == "menu" else (255, 140, 110)
        c.fill(c.ellipse_mask(10.5, 7.5, 3.3),
               c.rgrad(9.8, 6.6, 4.4, inner, (200, 30, 20)))
    else:
        c.fill(c.ellipse_mask(10.5, 7.5, 3.3),
               c.rgrad(9.8, 6.6, 4.4, (130, 50, 40), (60, 14, 10)))
    c.fill(c.ellipse_mask(9.6, 6.3, 1.0), (255, 255, 255, 150))

    # Minimize, shade, close
    for x, kind in ((244, "minimize"), (254, "shade"), (264, "close")):
        down = pressed == kind
        c.fill(c.rrect_mask(x, 3, x + 9, 12, 1.5), (18, 18, 20))
        c.fill(c.rrect_mask(x + 0.6, 3.6, x + 8.4, 11.4, 1),
               c.vgrad(3.6, 11.4, (66, 68, 73) if down else (176, 179, 184),
                       (40, 41, 45) if down else (96, 99, 104)))
        sym = (255, 190, 80) if down else (28, 29, 32)
        o = 0.4 if down else 0
        if kind == "minimize":
            c.fill(c.rrect_mask(x + 2.5, 8.5 + o, x + 6.5, 9.8 + o, 0), sym)
        elif kind == "shade":
            c.fill(c.rrect_mask(x + 2.5, 5.2 + o, x + 6.5, 6.4 + o, 0), sym)
            c.fill(c.rrect_mask(x + 2.5, 7.8 + o, x + 6.5, 9.0 + o, 0), sym)
        else:
            m = c.mask()
            d = ImageDraw.Draw(m)
            d.line([c.pt(x + 2.6, 5.4 + o), c.pt(x + 6.4, 9.6 + o)], fill=255,
                   width=int(1.3 * S))
            d.line([c.pt(x + 6.4, 5.4 + o), c.pt(x + 2.6, 9.6 + o)], fill=255,
                   width=int(1.3 * S))
            c.fill(m, sym)
    return c.down()


# ------------------------------------------------------------- controls

def control(face, x, y, w, h):
    """A canvas over part of the panel, for drawing a control on it."""
    bg = face.img.crop((x * S, y * S, (x + w) * S, (y + h) * S))
    return Canvas(w, h, x, y, bg)


def push_button(face, x, y, w, h, symbol, down, glow):
    c = control(face, x, y, w, h)
    c.fill(c.rrect_mask(x + 0.5, y + 0.5, x + w - 0.5, y + h - 0.5, 2.2),
           (10, 10, 11))
    top, bottom = ((44, 45, 48), (24, 24, 26)) if down else \
        ((92, 94, 99), (30, 31, 34))
    c.fill(c.rrect_mask(x + 1.2, y + 1.2, x + w - 1.2, y + h - 1.2, 1.8),
           c.vgrad(y + 1, y + h - 1, top, bottom))
    if not down:
        c.fill(c.rrect_mask(x + 2, y + 1.6, x + w - 2, y + 3.2, 1),
               (255, 255, 255, 50))
    cx, cy = x + w / 2, y + h / 2 + (0.6 if down else 0)
    color = glow if down else IVORY
    for points in symbol:
        pts = [(cx + px, cy + py) for px, py in points]
        if down:
            halo = c.mask()
            ImageDraw.Draw(halo).polygon([c.pt(*p) for p in pts], fill=255)
            halo = halo.filter(ImageFilter.GaussianBlur(S * 1.2))
            c.fill(halo, glow[:3] + (150,))
        c.fill(c.polygon_mask(pts), color)
    return c.down()


def rect(x0, y0, x1, y1):
    return [(x0, y0), (x1, y0), (x1, y1), (x0, y1)]


SYMBOLS = {
    "previous": [rect(-5, -3.5, -3.6, 3.5), [(-3.4, 0), (0.6, -3.5), (0.6, 3.5)],
                 [(0.6, 0), (4.6, -3.5), (4.6, 3.5)]],
    "play": [[(-2.6, -4), (4, 0), (-2.6, 4)]],
    "pause": [rect(-3.4, -3.6, -0.9, 3.6), rect(0.9, -3.6, 3.4, 3.6)],
    "stop": [rect(-3.4, -3.4, 3.4, 3.4)],
    "next": [[(-4.6, -3.5), (-0.6, 0), (-4.6, 3.5)],
             [(-0.6, -3.5), (3.4, 0), (-0.6, 3.5)], rect(3.6, -3.5, 5, 3.5)],
    "eject": [[(-4, 0.6), (0, -3.6), (4, 0.6)], rect(-4, 1.8, 4, 3.4)],
}


def toggle_button(face, x, y, w, h, label, on, down, font=FONT):
    """Shuffle, repeat, EQ, PL: a steel key with an LED."""
    c = control(face, x, y, w, h)
    c.fill(c.rrect_mask(x + 0.5, y + 0.5, x + w - 0.5, y + h - 0.5, 2),
           (14, 14, 15))
    top, bottom = ((118, 121, 126), (82, 84, 89)) if down else \
        ((214, 216, 220), (140, 143, 148))
    c.fill(c.rrect_mask(x + 1.2, y + 1.2, x + w - 1.2, y + h - 1.2, 1.5),
           c.vgrad(y + 1, y + h - 1, top, bottom))
    led_x, led_y = x + 4.5, y + h / 2 + (0.4 if down else 0)
    c.fill(c.ellipse_mask(led_x, led_y, 1.9), (30, 20, 12))
    if on:
        halo = c.ellipse_mask(led_x, led_y, 2.6).filter(
            ImageFilter.GaussianBlur(S))
        c.fill(halo, AMBER + (200,))
        c.fill(c.ellipse_mask(led_x, led_y, 1.5),
               c.rgrad(led_x - 0.4, led_y - 0.4, 2, (255, 240, 190), AMBER))
    else:
        c.fill(c.ellipse_mask(led_x, led_y, 1.4), (82, 52, 20))
    img = c.down()
    tw = text_pixels(label, font)[1]
    tx = x + 8 + (w - 9 - tw) // 2 - x
    ty = (h - 5) // 2 + (1 if down else 0)
    draw_text(img, label, tx, ty, (30, 31, 34), font)
    return img


def fader_frame(face, x, w, level, centred):
    """A fader slot's background with LEDs lit to level (0..1)."""
    c = control(face, x, 57, w, 13)
    img = c.down()
    px = img.load()
    leds = list(range(2, w - 2, 3))
    n = len(leds)
    for i, lx in enumerate(leds):
        if centred:
            # Balance: lit outwards from the middle
            t = abs((i + 0.5) / n * 2 - 1)
            lit = level > 0 and t <= level + 0.5 / n
            position = t
        else:
            t = (i + 0.5) / n
            lit = t <= level
            position = t
        color = GREEN if position < 0.6 else AMBER if position < 0.85 else RED
        for dx in (0, 1):
            for yy in range(61, 66):
                if lit:
                    bright = yy in (62, 63)
                    px[lx + dx, yy - 57] = mix(color, (255, 255, 255),
                                               0.25 if bright else 0)
                else:
                    px[lx + dx, yy - 57] = mix(color, (22, 22, 24), 0.82)
    if centred:
        for yy in (59, 67):
            px[(w - 1) // 2, yy - 57] = (230, 232, 235)
    return img


def fader_thumb(down):
    c = Canvas(14, 11)
    c.fill(c.rrect_mask(0, 0, 14, 11, 1.5), (16, 16, 17))
    c.fill(c.rrect_mask(0.8, 0.8, 13.2, 10.2, 1),
           c.vgrad(0.8, 10.2, (236, 238, 241) if not down else (196, 198, 202),
                   (120, 123, 128) if not down else (96, 98, 102)))
    for x in (3, 5, 9, 11):
        c.fill(c.rrect_mask(x, 2.5, x + 0.8, 8.5, 0), (70, 72, 76, 200))
        c.fill(c.rrect_mask(x + 0.8, 2.5, x + 1.4, 8.5, 0),
               (255, 255, 255, 120))
    c.fill(c.rrect_mask(6.4, 1.6, 7.6, 9.4, 0),
           (255, 220, 140) if down else AMBER)
    return c.down()


def position_thumb(down):
    c = Canvas(29, 10)
    c.fill(c.rrect_mask(0, 0, 29, 10, 2), (14, 14, 15))
    c.fill(c.rrect_mask(0.8, 0.8, 28.2, 9.2, 1.5),
           c.vgrad(0.8, 9.2, (232, 234, 238) if not down else (190, 192, 196),
                   (112, 115, 120) if not down else (90, 92, 96)))
    for x in range(4, 26, 2):
        if 12 <= x <= 16:
            continue
        c.fill(c.rrect_mask(x, 2.6, x + 0.7, 7.4, 0), (80, 82, 86, 160))
    c.fill(c.rrect_mask(13.6, 1.4, 15.4, 8.6, 0.5),
           (255, 220, 140) if down else AMBER)
    return c.down()


def position_groove(face):
    img = control(face, 16, 72, 248, 10).down()
    px = img.load()
    for i in range(0, 11):
        x = 1 + i * 245 // 10
        for y in (0, 9):
            px[x, y] = ENGRAVE_DARK
    return img


def lamp_label(face, x, y, w, h, label, lit):
    img = control(face, x, y, w, h).down()
    tw = text_pixels(label, NARROW)[1]
    draw_text(img, label, (w - tw) // 2, (h - 5) // 2,
              AMBER if lit else GHOST, NARROW,
              halo=AMBER_HALO if lit else None)
    return img


def status_icons(face1):
    sheet = Image.new("RGB", (42, 9), LCD_BG)
    bg = face1.crop((26, 28, 35, 37))
    for i in range(4):
        sheet.paste(bg, (i * 9, 0))
    c = Canvas(42, 9)
    c.img = sheet.resize((42 * S, 9 * S), Image.NEAREST).convert("RGBA")
    shapes = [([(2, 1), (8, 4.5), (2, 8)], GREEN),
              (rect(10.5, 1.5, 12.5, 7.5) + [], AMBER),
              (rect(19.5, 1.5, 25.5, 7.5), RED)]
    c.fill(c.polygon_mask(rect(14.5, 1.5, 16.5, 7.5)), AMBER)
    for points, color in shapes:
        halo = c.polygon_mask(points).filter(ImageFilter.GaussianBlur(S))
        c.fill(halo, color + (110,))
        c.fill(c.polygon_mask(points), color)
    halo = c.polygon_mask(rect(14.5, 1.5, 16.5, 7.5)).filter(
        ImageFilter.GaussianBlur(S))
    c.fill(halo, AMBER + (110,))
    return c.down()


# ---------------------------------------------------------------- sheets

def build():
    face = face_canvas()
    face1_base = face.down()
    face1 = face1_base.copy()
    face_details(face1)
    files = {}

    # Title bar: active and inactive strips, pressed buttons, clutter bar
    titlebar = Image.new("RGB", (344, 87), (0, 0, 0))
    titlebar.paste(title_bar(True), (27, 0))
    titlebar.paste(title_bar(False), (27, 15))
    for name, (sx, sy), (x, y) in (("menu", (0, 9), (6, 3)),
                                   ("minimize", (9, 9), (244, 3)),
                                   ("shade", (9, 27), (254, 3)),
                                   ("close", (18, 9), (264, 3))):
        titlebar.paste(title_bar(True, name).crop((x, y, x + 9, y + 9)),
                       (sx, sy))
    titlebar.paste(face1.crop((10, 22, 18, 65)), (304, 0))
    lit = face1_base.crop((10, 22, 18, 65))
    for i, (letter, top, height) in enumerate(CLUTTER):
        draw_text(lit, letter, 2, top + (height - 5) // 2, AMBER, NARROW,
                  halo=AMBER_HALO)
        titlebar.paste(lit.crop((0, top, 8, top + height)),
                       (304 + 8 * i, 44 + top))
    files["titlebar.bmp"] = titlebar

    # Transport buttons
    cbuttons = Image.new("RGB", (136, 36), (0, 0, 0))
    for sx, (name, x, w, glow) in zip(
            (0, 23, 46, 69, 92),
            (("previous", 16, 23, AMBER), ("play", 39, 23, GREEN),
             ("pause", 62, 23, AMBER), ("stop", 85, 23, RED),
             ("next", 108, 22, AMBER))):
        for down in (False, True):
            cbuttons.paste(push_button(face, x, 88, w, 18, SYMBOLS[name], down,
                                       glow), (sx, 18 if down else 0))
    for down in (False, True):
        cbuttons.paste(push_button(face, 136, 89, 22, 16, SYMBOLS["eject"],
                                   down, AMBER), (114, 16 if down else 0))
    files["cbuttons.bmp"] = cbuttons

    # Time digits
    numbers = Image.new("RGB", (99, 13), LCD_BG)
    cell = face1_base.crop((48, 26, 57, 39))
    for d in range(11):
        numbers.paste(cell, (d * 9, 0))
        draw_segments(numbers, d * 9, 0, d, ghost_only=(d == 10))
    files["numbers.bmp"] = numbers

    # Text
    text = Image.new("RGB", (155, 18), LCD_BG)
    for row, chars in enumerate(TEXT_ROWS):
        for col, ch in enumerate(chars):
            if ch == " " and not (row == 0 and col == 30):
                continue
            cell_img = Image.new("RGB", (5, 6), LCD_BG)
            draw_text(cell_img, ch, 0, 0, AMBER, halo=AMBER_HALO)
            text.paste(cell_img, (col * 5, row * 6))
    files["text.bmp"] = text

    files["playpaus.bmp"] = status_icons(face1_base)

    monoster = Image.new("RGB", (56, 24), LCD_BG)
    monoster.paste(lamp_label(face, 212, 41, 29, 12, "stereo", True), (0, 0))
    monoster.paste(lamp_label(face, 212, 41, 29, 12, "stereo", False), (0, 12))
    monoster.paste(lamp_label(face, 239, 41, 27, 12, "mono", True), (29, 0))
    monoster.paste(lamp_label(face, 239, 41, 27, 12, "mono", False), (29, 12))
    files["monoster.bmp"] = monoster

    volume = Image.new("RGB", (68, 433), (22, 22, 24))
    balance = Image.new("RGB", (47, 433), (22, 22, 24))
    for i in range(28):
        volume.paste(fader_frame(face, 107, 68, i / 27, False), (0, i * 15))
        balance.paste(fader_frame(face, 177, 38, i / 27, True), (9, i * 15))
    for sheet in (volume, balance):
        sheet.paste(fader_thumb(True), (0, 422))
        sheet.paste(fader_thumb(False), (15, 422))
    files["volume.bmp"] = volume
    files["balance.bmp"] = balance

    posbar = Image.new("RGB", (307, 10), (0, 0, 0))
    posbar.paste(position_groove(face), (0, 0))
    posbar.paste(position_thumb(False), (248, 0))
    posbar.paste(position_thumb(True), (278, 0))
    files["posbar.bmp"] = posbar

    shufrep = Image.new("RGB", (92, 85), (0, 0, 0))
    for on in (False, True):
        for down in (False, True):
            y = (30 if on else 0) + (15 if down else 0)
            shufrep.paste(toggle_button(face, 210, 89, 28, 15, "rep", on,
                                        down), (0, y))
            shufrep.paste(toggle_button(face, 164, 89, 47, 15, "shuffle", on,
                                        down), (28, y))
            y = 73 if on else 61
            shufrep.paste(toggle_button(face, 219, 58, 23, 12, "eq", on, down),
                          (46 if down else 0, y))
            shufrep.paste(toggle_button(face, 242, 58, 23, 12, "pl", on, down),
                          (69 if down else 23, y))
    files["shufrep.bmp"] = shufrep

    # The window as it looks stopped, for skin viewers
    main = face1.copy()
    main.paste(titlebar.crop((27, 0, 302, 14)), (0, 0))
    main.paste(titlebar.crop((304, 0, 312, 43)), (10, 22))
    main.paste(cbuttons.crop((0, 0, 114, 18)), (16, 88))
    main.paste(cbuttons.crop((114, 0, 136, 16)), (136, 89))
    main.paste(volume.crop((0, 22 * 15, 68, 22 * 15 + 13)), (107, 57))
    main.paste(balance.crop((9, 0, 47, 13)), (177, 57))
    main.paste(shufrep.crop((28, 0, 75, 15)), (164, 89))
    main.paste(shufrep.crop((0, 0, 28, 15)), (210, 89))
    main.paste(shufrep.crop((0, 61, 46, 73)), (219, 58))
    main.paste(posbar.crop((0, 0, 248, 10)), (16, 72))
    main.paste(monoster.crop((0, 12, 56, 24)), (212, 41))
    main.paste(face1.crop((0, 14, W, 15)), (0, 14))
    files["main.bmp"] = main

    viscolor = [LCD_BG, GHOST]
    for i in range(16):         # 2 (top, red) .. 17 (bottom, green)
        t = i / 15
        viscolor.append(lerp(RED, AMBER, t / 0.35) if t < 0.35 else
                        lerp(AMBER, GREEN, (t - 0.35) / 0.3) if t < 0.65 else
                        GREEN)
    viscolor += [(255, 240, 200)] + [lerp(AMBER, LCD_BG, i / 5)
                                     for i in range(5)]
    files["viscolor.txt"] = "".join(
        "%d,%d,%d\n" % tuple(clamp(v) for v in c) for c in viscolor)
    return files


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    files = build()
    with zipfile.ZipFile(sys.argv[1], "w", zipfile.ZIP_DEFLATED) as wsz:
        for name, content in sorted(files.items()):
            if isinstance(content, str):
                data = content.encode()
            else:
                buffer = io.BytesIO()
                content.save(buffer, "BMP")
                data = buffer.getvalue()
            info = zipfile.ZipInfo(name, (2026, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            wsz.writestr(info, data)
    if len(sys.argv) > 2:
        files["main.bmp"].resize((W * 3, H * 3), Image.NEAREST).save(
            sys.argv[2])


if __name__ == "__main__":
    main()
