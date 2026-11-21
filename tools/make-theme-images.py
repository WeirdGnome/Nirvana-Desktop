#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
#
# The images of Nirvana Desktop's themes drawn (Pillow): volume, drawer and file icons in
# two states, the window gadgets in three, and the bars' and backdrops' gradients. Original
# artwork, drawn here from simple shapes; each theme draws the same shapes in its own way
# (its palette, smoothing, rounding, bevels, shadows). Run again after a change and commit
# the PNGs (the build does not need Pillow).
#
#     tools/make-theme-images.py               every theme, into themes/images/NAME
#     tools/make-theme-images.py classic31     one of them

import math
import os
import sys

from PIL import Image, ImageDraw, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..", "themes", "images")

# ---------------------------------------------------------------- the themes

# The four colours of the first classic look, the three greys of the second, the eight
# of the third, the smooth shades of the fourth; Ambient is today's.
BLUE13, ORANGE13 = (0, 85, 170), (255, 136, 0)
GREY, DGREY, BLUE2 = (170, 170, 170), (102, 102, 102), (102, 136, 187)
BEIGE, ORANGE3, LBLUE = (187, 170, 153), (255, 170, 68), (153, 187, 221)
BLACK, WHITE = (0, 0, 0), (255, 255, 255)

THEMES = {
    "classic13": dict(
        aa=False, radius=0, shadow=False, bevel=False, select="invert",
        bar_h=20, menu_h=18, title_h=20,
        ink=BLUE13, gadget_face=WHITE, gadget_down=ORANGE13, gadget_inactive=BLUE13,
        gadget_ink_inactive=WHITE, gadget_outline=BLACK,
        grads={
            "bar": [(0, WHITE), (1, WHITE)],
            "bar_sel": [(0, BLUE13), (1, BLUE13)],
            "title_active": "stripes",
            "title_inactive": [(0, BLUE13), (1, BLUE13)],
            "menu_sel": [(0, BLUE13), (1, BLUE13)],
            "backdrop": [(0, BLUE13), (1, BLUE13)],
            "dock": [(0, WHITE), (1, WHITE)],
        },
        stripes=(WHITE, BLUE13),
        icons=dict(
            outline=BLACK, disk=(WHITE, WHITE), slot=BLACK, led=ORANGE13, hilite=None,
            roof=ORANGE13, wall=(WHITE, WHITE), door=BLUE13, window=BLUE13,
            board=BLUE13, chip=BLACK, pin=ORANGE13, star=ORANGE13,
            removable=(BLUE13, BLUE13), shutter=WHITE, hole=BLACK, label=WHITE,
            drawer_back=(ORANGE13, ORANGE13), drawer=(ORANGE13, ORANGE13),
            paper=WHITE, fold=WHITE, lines=BLUE13,
            win=(WHITE, WHITE), win_title=BLUE13, glyph=BLACK,
        ),
    ),
    "classic21": dict(
        aa=False, radius=0, shadow=False, bevel=True, select="invert",
        bar_h=22, menu_h=20, title_h=20,
        ink=BLACK, gadget_face=GREY, gadget_down=DGREY, gadget_inactive=GREY,
        gadget_ink_inactive=DGREY, gadget_outline=BLACK,
        grads={
            "bar": "bevel:" + repr(GREY),
            "bar_sel": [(0, BLUE2), (1, BLUE2)],
            "title_active": "bevel:" + repr(BLUE2),
            "title_inactive": "bevel:" + repr(GREY),
            "menu_sel": [(0, BLUE2), (1, BLUE2)],
            "backdrop": [(0, GREY), (1, GREY)],
            "dock": "bevel:" + repr(GREY),
        },
        icons=dict(
            outline=BLACK, disk=(GREY, GREY), slot=BLACK, led=BLUE2, hilite=WHITE,
            roof=BLUE2, wall=(WHITE, WHITE), door=BLACK, window=BLUE2,
            board=BLUE2, chip=BLACK, pin=WHITE, star=WHITE,
            removable=(BLUE2, BLUE2), shutter=GREY, hole=BLACK, label=WHITE,
            drawer_back=(GREY, GREY), drawer=(WHITE, WHITE),
            paper=WHITE, fold=GREY, lines=BLUE2,
            win=(GREY, GREY), win_title=BLUE2, glyph=BLACK,
        ),
    ),
    "classic31": dict(
        aa=False, radius=0, shadow=False, bevel=True, select="bright",
        bar_h=22, menu_h=20, title_h=21,
        ink=BLACK, gadget_face=GREY, gadget_down=DGREY, gadget_inactive=GREY,
        gadget_ink_inactive=DGREY, gadget_outline=BLACK,
        grads={
            "bar": "bevel:" + repr(GREY),
            "bar_sel": [(0, BLUE2), (1, BLUE2)],
            "title_active": "bevel:" + repr(BLUE2),
            "title_inactive": "bevel:" + repr(GREY),
            "menu_sel": [(0, BLUE2), (1, BLUE2)],
            "backdrop": [(0, (110, 140, 160)), (1, (80, 104, 124))],
            "dock": "bevel:" + repr(GREY),
        },
        icons=dict(
            outline=BLACK, disk=(GREY, GREY), slot=DGREY, led=(68, 204, 68), hilite=WHITE,
            roof=ORANGE3, wall=(BEIGE, BEIGE), door=DGREY, window=LBLUE,
            board=(68, 136, 68), chip=BLACK, pin=ORANGE3, star=ORANGE3,
            removable=(BLUE2, BLUE2), shutter=GREY, hole=BLACK, label=WHITE,
            drawer_back=(BEIGE, BEIGE), drawer=(ORANGE3, ORANGE3),
            paper=WHITE, fold=GREY, lines=BLUE2,
            win=(GREY, GREY), win_title=BLUE2, glyph=BLACK,
        ),
    ),
    "classic41": dict(
        aa=True, radius=1, shadow=True, bevel=True, select="bright",
        bar_h=24, menu_h=21, title_h=22,
        ink=(30, 30, 34), gadget_face=(204, 206, 212), gadget_down=(150, 154, 164),
        gadget_inactive=(196, 196, 198), gadget_ink_inactive=(120, 120, 124),
        gadget_outline=(40, 40, 46),
        grads={
            "bar": [(0, (226, 228, 232)), (1, (178, 182, 190))],
            "bar_sel": [(0, (120, 150, 200)), (1, (70, 100, 160))],
            "title_active": [(0, (124, 152, 204)), (1, (64, 92, 150))],
            "title_inactive": [(0, (208, 208, 212)), (1, (168, 170, 176))],
            "menu_sel": [(0, (110, 140, 196)), (1, (72, 102, 160))],
            "backdrop": [(0, (74, 86, 106)), (1, (34, 40, 52))],
            "dock": [(0, (220, 222, 226)), (1, (176, 180, 188))],
        },
        icons=dict(
            outline=(40, 42, 50), disk=((206, 208, 214), (140, 144, 156)), slot=(54, 56, 64),
            led=(80, 200, 110), hilite=(255, 255, 255),
            roof=(170, 70, 56), wall=((236, 228, 210), (196, 184, 160)), door=(110, 76, 50),
            window=(120, 164, 214), board=(50, 120, 70), chip=(30, 32, 36), pin=(210, 172, 70),
            star=(240, 190, 50), removable=((96, 120, 180), (52, 70, 130)), shutter=(196, 200, 210),
            hole=(64, 68, 80), label=(236, 236, 236),
            drawer_back=((220, 186, 100), (196, 156, 66)), drawer=((236, 204, 120), (208, 160, 70)),
            paper=(250, 250, 250), fold=(204, 208, 216), lines=(160, 166, 180),
            win=((226, 228, 234), (190, 194, 204)), win_title=(80, 110, 170), glyph=(60, 64, 74),
        ),
    ),
    "ambient": dict(
        aa=True, radius=4, shadow=True, bevel=False, select="bright",
        bar_h=24, menu_h=20, title_h=22,
        ink=(52, 56, 66), gadget_face=((250, 251, 253), (204, 208, 218)),
        gadget_down=((170, 176, 190), (214, 218, 226)),
        gadget_inactive=((228, 228, 230), (206, 206, 210)), gadget_ink_inactive=(52, 56, 66),
        gadget_outline=(96, 100, 112),
        grads={
            "bar": [(0, (252, 252, 254)), (0.5, (232, 234, 240)), (1, (204, 208, 218))],
            "bar_sel": [(0, (110, 150, 220)), (1, (60, 96, 170))],
            "title_active": [(0, (150, 182, 236)), (0.5, (104, 144, 214)), (1, (78, 114, 186))],
            "title_inactive": [(0, (238, 238, 242)), (1, (206, 208, 216))],
            "menu_sel": [(0, (112, 152, 222)), (1, (70, 108, 182))],
            "backdrop": [(0, (88, 112, 150)), (0.6, (58, 80, 118)), (1, (38, 52, 80))],
            "dock": [(0, (252, 252, 254)), (0.5, (232, 234, 240)), (1, (204, 208, 218))],
        },
        icons=dict(
            outline=(70, 74, 86), disk=((214, 218, 226), (150, 156, 170)), slot=(60, 64, 74),
            led=(60, 170, 90), hilite=(255, 255, 255),
            roof=(200, 80, 60), wall=((246, 236, 214), (206, 190, 160)), door=(120, 80, 50),
            window=(130, 180, 230), board=(40, 130, 70), chip=(30, 32, 36), pin=(220, 180, 60),
            star=(255, 200, 40), removable=((90, 120, 200), (50, 70, 140)), shutter=(200, 205, 215),
            hole=(70, 74, 86), label=(240, 240, 240),
            drawer_back=((240, 200, 90), (220, 170, 60)), drawer=((250, 216, 110), (225, 170, 60)),
            paper=(255, 255, 255), fold=(210, 214, 224), lines=(170, 175, 190),
            win=((236, 238, 244), (200, 204, 214)), win_title=(70, 110, 190), glyph=(70, 74, 86),
        ),
    ),
}

# ---------------------------------------------------------------- drawing

T = None  # the theme being drawn
S = 4     # supersampling (1 for the pixel-sharp themes)


def rgba(c, a=255):
    return tuple(c[:3]) + (a,)


def pair(c):
    """a fill as (top, bottom): a single colour is flat"""
    return c if isinstance(c[0], (tuple, list)) else (c, c)


def canvas(w, h):
    return Image.new("RGBA", (w * S, h * S), (0, 0, 0, 0))


def r(v):
    return [int(x * S) for x in v]


def shadow(img, radius=3, offset=(2, 3), alpha=90):
    if not T["shadow"]:
        return img
    a = img.split()[3]
    sh = Image.new("RGBA", img.size, (0, 0, 0, 0))
    sh.putalpha(a.point(lambda p: p * alpha // 255))
    sh = sh.filter(ImageFilter.GaussianBlur(radius * S))
    out = Image.new("RGBA", img.size, (0, 0, 0, 0))
    out.alpha_composite(sh, (offset[0] * S, offset[1] * S))
    out.alpha_composite(img)
    return out


def box(img, b, fill, outline, radius=None, bevel=None):
    """a filled box (a gradient on the smooth themes), outlined, bevelled where asked"""
    radius = T["radius"] * 1.5 if radius is None else radius
    bevel = T["bevel"] if bevel is None else bevel
    top, bottom = pair(fill)
    x0, y0, x1, y1 = r(b)
    layer = Image.new("RGBA", img.size, (0, 0, 0, 0))
    d = ImageDraw.Draw(layer)
    for i in range(y1 - y0 + 1):
        t = i / max(1, y1 - y0)
        c = tuple(int(top[k] + (bottom[k] - top[k]) * t) for k in range(3)) + (255,)
        d.line([(x0, y0 + i), (x1, y0 + i)], fill=c)
    m = Image.new("L", img.size, 0)
    if radius:
        ImageDraw.Draw(m).rounded_rectangle((x0, y0, x1, y1), int(radius * S), fill=255)
    else:
        ImageDraw.Draw(m).rectangle((x0, y0, x1, y1), fill=255)
    img.paste(layer, (0, 0), m)
    d = ImageDraw.Draw(img)
    if bevel:
        w = S
        d.line([(x0 + w, y0 + w), (x1 - w, y0 + w)], fill=(255, 255, 255, 255), width=w)
        d.line([(x0 + w, y0 + w), (x0 + w, y1 - w)], fill=(255, 255, 255, 255), width=w)
        dark = tuple(max(0, int(c * 0.55)) for c in bottom) + (255,)
        d.line([(x0 + w, y1 - w), (x1 - w, y1 - w)], fill=dark, width=w)
        d.line([(x1 - w, y0 + w), (x1 - w, y1 - w)], fill=dark, width=w)
    if outline is not None:
        if radius:
            d.rounded_rectangle((x0, y0, x1, y1), int(radius * S), outline=rgba(outline), width=S)
        else:
            d.rectangle((x0, y0, x1, y1), outline=rgba(outline), width=S)


def finish(img, w, h):
    if S > 1:
        img = img.resize((w, h), Image.LANCZOS)
    return img


def selected_look(img):
    """an icon's selected state: inverted colours (the classic way) or brightened"""
    px = img.load()
    for y in range(img.size[1]):
        for x in range(img.size[0]):
            cr, cg, cb, ca = px[x, y]
            if not ca:
                continue
            if T["select"] == "invert":
                px[x, y] = (255 - cr, 255 - cg, 255 - cb, ca)
            else:
                px[x, y] = (min(255, cr + (255 - cr) // 4), min(255, cg + (255 - cg) // 4),
                            min(255, cb + (255 - cb) // 3), ca)
    return img


# ---------------------------------------------------------------- icons (64 x 64)

def harddisk():
    P = T["icons"]
    img = canvas(64, 64)
    box(img, (6, 18, 58, 46), P["disk"], P["outline"], radius=T["radius"] * 1.5)
    d = ImageDraw.Draw(img)
    d.rectangle(r((12, 36, 40, 40)), fill=rgba(P["slot"]))
    d.ellipse(r((46, 34, 52, 40)), fill=rgba(P["led"]), outline=rgba(P["outline"]) if not T["aa"] else None)
    if P["hilite"] and not T["bevel"]:
        d.line(r((10, 24, 54, 24)), fill=rgba(P["hilite"], 140), width=S)
    return img


def home():
    P = T["icons"]
    img = canvas(64, 64)
    box(img, (14, 28, 50, 54), P["wall"], P["outline"], radius=T["radius"] * 0.5, bevel=False)
    d = ImageDraw.Draw(img)
    d.polygon(r((8, 31, 32, 9, 56, 31)), fill=rgba(P["roof"]), outline=rgba(P["outline"]))
    d.rectangle(r((27, 38, 37, 54)), fill=rgba(P["door"]), outline=rgba(P["outline"]))
    d.rectangle(r((40, 34, 46, 40)), fill=rgba(P["window"]), outline=rgba(P["outline"]))
    return img


def ram():
    P = T["icons"]
    img = canvas(64, 64)
    box(img, (6, 20, 58, 44), P["board"], P["outline"], radius=T["radius"] * 0.5, bevel=False)
    d = ImageDraw.Draw(img)
    for i in range(4):
        x = 10 + i * 12
        d.rectangle(r((x, 25, x + 8, 37)), fill=rgba(P["chip"]))
    for i in range(12):
        x = 9 + i * 4
        d.rectangle(r((x, 45, x + 2, 49)), fill=rgba(P["pin"]))
    return img


def system():
    P = T["icons"]
    img = harddisk()
    d = ImageDraw.Draw(img)
    cx, cy = 52 * S, 16 * S
    pts = []
    for i in range(10):
        a = math.pi / 2 + i * math.pi / 5
        rad = (9 if i % 2 == 0 else 4) * S
        pts.append((cx + rad * math.cos(a), cy - rad * math.sin(a)))
    d.polygon(pts, fill=rgba(P["star"]), outline=rgba(P["outline"]))
    return img


def removable():
    P = T["icons"]
    img = canvas(64, 64)
    box(img, (10, 14, 54, 50), P["removable"], P["outline"], radius=T["radius"])
    d = ImageDraw.Draw(img)
    d.rectangle(r((20, 14, 44, 28)), fill=rgba(P["shutter"]), outline=rgba(P["outline"]))
    d.rectangle(r((36, 16, 41, 26)), fill=rgba(P["hole"]))
    d.rectangle(r((16, 34, 48, 46)), fill=rgba(P["label"]), outline=rgba(P["outline"]) if not T["aa"] else None)
    return img


def drawer():
    P = T["icons"]
    img = canvas(64, 64)
    box(img, (6, 14, 34, 26), P["drawer_back"], P["outline"], radius=T["radius"] * 0.75, bevel=False)
    box(img, (6, 20, 58, 52), P["drawer"], P["outline"], radius=T["radius"] * 0.75)
    return img


def file_icon():
    P = T["icons"]
    img = canvas(64, 64)
    d = ImageDraw.Draw(img)
    d.polygon(r((14, 6, 42, 6, 52, 16, 52, 58, 14, 58)), fill=rgba(P["paper"]), outline=rgba(P["outline"]))
    d.polygon(r((42, 6, 42, 16, 52, 16)), fill=rgba(P["fold"]), outline=rgba(P["outline"]))
    for i in range(5):
        y = 24 + i * 7
        d.line(r((20, y, 46, y)), fill=rgba(P["lines"]), width=S)
    return img


def program():
    P = T["icons"]
    img = canvas(64, 64)
    box(img, (8, 12, 56, 52), P["win"], P["outline"], radius=T["radius"])
    d = ImageDraw.Draw(img)
    d.rectangle(r((9, 13, 55, 21)), fill=rgba(P["win_title"]))
    d.line(r((8, 21, 56, 21)), fill=rgba(P["outline"]), width=S)
    d.polygon(r((22, 30, 34, 37, 22, 44)), fill=rgba(P["glyph"]))
    d.line(r((36, 44, 46, 44)), fill=rgba(P["glyph"]), width=2 * S)
    return img


def trashcan(full=False):
    P = T["icons"]
    img = canvas(64, 64)
    if full:
        # papers over the rim
        d = ImageDraw.Draw(img)
        d.polygon(r((20, 20, 30, 6, 38, 12, 30, 24)), fill=rgba(P["paper"]), outline=rgba(P["outline"]))
        d.polygon(r((32, 22, 44, 8, 50, 16, 40, 26)), fill=rgba(P["paper"]), outline=rgba(P["outline"]))
    # the bin, a little narrower at the bottom
    d = ImageDraw.Draw(img)
    top, bottom = pair(P["disk"])
    d.polygon(r((14, 22, 50, 22, 46, 58, 18, 58)), fill=rgba(top), outline=rgba(P["outline"]))
    for x in (24, 32, 40):
        d.line(r((x, 28, x - (x - 32) // 6, 52)), fill=rgba(P["slot"]), width=S)
    # the lid, lifted when it is full
    lid = (10, 14, 54, 20) if full else (10, 16, 54, 22)
    box(img, lid, P["disk"], P["outline"], radius=T["radius"] * 0.5)
    d = ImageDraw.Draw(img)
    d.rectangle(r((27, lid[1] - 4, 37, lid[1])), fill=rgba(bottom), outline=rgba(P["outline"]))
    return img


def trashcan_full():
    return trashcan(True)


ICONS = {
    "trashcan": trashcan, "trashcan_full": trashcan_full,
    "harddisk": harddisk, "home": home, "ram": ram, "system": system,
    "removable": removable, "drawer": drawer, "file": file_icon, "program": program,
}

# ---------------------------------------------------------------- gadgets (18 x 18)

def gadget_base(state):
    img = canvas(18, 18)
    face = {"": T["gadget_face"], "_down": T["gadget_down"], "_inactive": T["gadget_inactive"]}[state]
    radius = T["radius"]
    box(img, (1, 1, 17, 17), face, T["gadget_outline"], radius=radius,
        bevel=T["bevel"] and state != "_down")
    ink = T["gadget_ink_inactive"] if state == "_inactive" else T["ink"]
    return img, ImageDraw.Draw(img), rgba(ink), rgba(pair(face)[0])


def lw(k=1.5):
    return max(1, int(k * S)) if T["aa"] else 1


def g_close(d, ink, face):
    if T["aa"]:
        d.ellipse(r((6, 6, 12, 12)), fill=ink)
    else:
        d.rectangle(r((6, 6, 11, 11)), fill=ink)


def g_iconify(d, ink, face):
    d.rectangle(r((5, 11, 10, 14)), outline=ink, width=lw())


def g_zoom(d, ink, face):
    d.rectangle(r((4, 4, 14, 14)), outline=ink, width=lw())
    d.rectangle(r((4, 4, 9, 9)), fill=ink)


def g_depth(d, ink, face):
    d.rectangle(r((3, 3, 11, 11)), outline=ink, width=lw())
    d.rectangle(r((7, 7, 15, 15)), fill=face, outline=ink, width=lw())


def g_jump(d, ink, face):
    d.polygon(r((4, 9, 10, 4, 10, 7, 14, 7, 14, 11, 10, 11, 10, 14)), fill=ink)


def g_prefs(d, ink, face):
    cx, cy = 9 * S, 9 * S
    pts = []
    for i in range(16):
        a = i * math.pi / 8
        rad = (6 if i % 2 == 0 else 4.4) * S
        pts.append((cx + rad * math.cos(a), cy + rad * math.sin(a)))
    d.polygon(pts, fill=ink)
    d.ellipse((cx - 2 * S, cy - 2 * S, cx + 2 * S, cy + 2 * S), fill=face)


def g_resize(d, ink, face):
    for k in (0, 4, 8):
        d.line(r((14 - k, 15, 15, 14 - k)), fill=ink, width=lw(1.3))


GADGETS = {
    "close": g_close, "iconify": g_iconify, "zoom": g_zoom, "depth": g_depth,
    "jump": g_jump, "prefs": g_prefs, "resize": g_resize,
}

# ---------------------------------------------------------------- bars and backdrops

def gradient_image(spec, h, w=4):
    img = Image.new("RGBA", (w, h))
    d = ImageDraw.Draw(img)
    if spec == "stripes":
        a, b = T["stripes"]
        for y in range(h):
            c = a if y in (0, h - 1) or y % 3 else b
            d.line([(0, y), (w, y)], fill=rgba(c))
        return img
    if isinstance(spec, str) and spec.startswith("bevel:"):
        face = eval(spec[6:])  # (a colour written by this file)
        dark = tuple(int(c * 0.45) for c in face)
        for y in range(h):
            c = WHITE if y == 0 else dark if y == h - 1 else face
            d.line([(0, y), (w, y)], fill=rgba(c))
        return img
    for y in range(h):
        t = y / max(1, h - 1)
        for i in range(len(spec) - 1):
            t0, c0 = spec[i]
            t1, c1 = spec[i + 1]
            if t0 <= t <= t1:
                k = (t - t0) / max(1e-9, t1 - t0)
                c = tuple(int(c0[j] + (c1[j] - c0[j]) * k) for j in range(3))
                d.line([(0, y), (w, y)], fill=rgba(c))
                break
    return img


def draw_theme(name):
    global T, S
    T = THEMES[name]
    S = 4 if T["aa"] else 1
    out = os.path.join(ROOT, name)
    os.makedirs(out, exist_ok=True)

    def save(img, file):
        img.save(os.path.join(out, file + ".png"))

    for icon, fn in ICONS.items():
        img = finish(shadow(fn()), 64, 64)
        save(img, "icon_%s" % icon)
        save(selected_look(finish(shadow(fn()), 64, 64)), "icon_%s_sel" % icon)
    for g, fn in GADGETS.items():
        for state in ("", "_down", "_inactive"):
            img, d, ink, face = gadget_base(state)
            fn(d, ink, face)
            save(finish(img, 18, 18), "gadget_%s%s" % (g, state))
    heights = {"bar": T["bar_h"], "bar_sel": T["menu_h"], "title_active": T["title_h"],
               "title_inactive": T["title_h"], "menu_sel": T["menu_h"], "backdrop": 512, "dock": 64}
    for g, spec in T["grads"].items():
        save(gradient_image(spec, heights[g]), "grad_" + g)


def main():
    names = sys.argv[1:] or list(THEMES)
    for n in names:
        if n not in THEMES:
            sys.exit("make-theme-images.py: no theme %s (%s)" % (n, ", ".join(THEMES)))
        draw_theme(n)


if __name__ == "__main__":
    main()
