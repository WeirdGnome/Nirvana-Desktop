#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
#
# The images of the Ambient theme drawn (Pillow): volume, drawer and file icons in two
# states, and the window gadgets. Original artwork, drawn here from simple shapes; run
# again after a change and commit the PNGs (the build does not need Pillow).
#
#     tools/make-theme-images.py themes/images/ambient

import os
import sys

from PIL import Image, ImageDraw, ImageFilter

OUT = sys.argv[1] if len(sys.argv) > 1 else "themes/images/ambient"
S = 4  # supersampling


def canvas(w, h):
    return Image.new("RGBA", (w * S, h * S), (0, 0, 0, 0))


def save(img, name, w, h):
    img = img.resize((w, h), Image.LANCZOS)
    img.save(os.path.join(OUT, name + ".png"))


def r(v):
    return [int(x * S) for x in v]


def shadow(img, radius=3, offset=(2, 3), alpha=90):
    a = img.split()[3]
    sh = Image.new("RGBA", img.size, (0, 0, 0, 0))
    sh.putalpha(a.point(lambda p: p * alpha // 255))
    sh = sh.filter(ImageFilter.GaussianBlur(radius * S))
    out = Image.new("RGBA", img.size, (0, 0, 0, 0))
    out.alpha_composite(sh, (offset[0] * S, offset[1] * S))
    out.alpha_composite(img)
    return out


def gradient_box(d, box, top, bottom, radius):
    x0, y0, x1, y1 = box
    h = y1 - y0
    for i in range(int(h)):
        t = i / max(1, h - 1)
        c = tuple(int(top[k] + (bottom[k] - top[k]) * t) for k in range(3)) + (255,)
        d.line([(x0, y0 + i), (x1, y0 + i)], fill=c)


def rounded_mask(size, box, radius):
    m = Image.new("L", size, 0)
    ImageDraw.Draw(m).rounded_rectangle(box, radius, fill=255)
    return m


def shaded(size, box, radius, top, bottom, outline):
    layer = Image.new("RGBA", size, (0, 0, 0, 0))
    g = Image.new("RGBA", size, (0, 0, 0, 0))
    gradient_box(ImageDraw.Draw(g), box, top, bottom, radius)
    layer.paste(g, (0, 0), rounded_mask(size, box, radius))
    ImageDraw.Draw(layer).rounded_rectangle(box, radius, outline=outline, width=S)
    return layer


# ---------------------------------------------------------------- icons (64 x 64)

def harddisk(selected):
    img = canvas(64, 64)
    hi = 40 if selected else 0
    body = shaded(img.size, r((6, 18, 58, 46)), 6 * S, (214 + hi // 3, 218, 226), (150, 156, 170), (70, 74, 86, 255))
    img.alpha_composite(body)
    d = ImageDraw.Draw(img)
    d.rounded_rectangle(r((12, 36, 40, 40)), 2 * S, fill=(60, 64, 74, 255))
    led = (90, 230, 120, 255) if selected else (60, 170, 90, 255)
    d.ellipse(r((46, 34, 52, 40)), fill=led)
    d.line(r((10, 24, 54, 24)), fill=(255, 255, 255, 140), width=S)
    return shadow(img)


def home(selected):
    img = canvas(64, 64)
    d = ImageDraw.Draw(img)
    roof = (200, 80, 60, 255) if not selected else (235, 110, 80, 255)
    wall = shaded(img.size, r((14, 28, 50, 54)), 2 * S, (246, 236, 214), (206, 190, 160), (110, 90, 60, 255))
    img.alpha_composite(wall)
    d.polygon(r((8, 31, 32, 9, 56, 31)), fill=roof, outline=(110, 40, 30, 255))
    d.rectangle(r((27, 38, 37, 54)), fill=(120, 80, 50, 255))
    d.rectangle(r((40, 34, 46, 40)), fill=(130, 180, 230, 255))
    return shadow(img)


def ram(selected):
    img = canvas(64, 64)
    d = ImageDraw.Draw(img)
    board = (40, 130, 70, 255) if not selected else (60, 170, 95, 255)
    d.rounded_rectangle(r((6, 20, 58, 44)), 2 * S, fill=board, outline=(20, 70, 35, 255), width=S)
    for i in range(4):
        x = 10 + i * 12
        d.rectangle(r((x, 25, x + 8, 37)), fill=(30, 32, 36, 255))
    for i in range(12):
        x = 9 + i * 4
        d.rectangle(r((x, 44, x + 2, 49)), fill=(220, 180, 60, 255))
    return shadow(img)


def system(selected):
    img = harddisk(selected)
    d = ImageDraw.Draw(img)
    # a small star: the system's volume
    cx, cy = 52 * S, 16 * S
    pts = []
    import math
    for i in range(10):
        a = math.pi / 2 + i * math.pi / 5
        rad = (9 if i % 2 == 0 else 4) * S
        pts.append((cx + rad * math.cos(a), cy - rad * math.sin(a)))
    d.polygon(pts, fill=(255, 200, 40, 255), outline=(150, 100, 10, 255))
    return img


def removable(selected):
    img = canvas(64, 64)
    hi = 30 if selected else 0
    body = shaded(img.size, r((10, 14, 54, 50)), 4 * S, (90 + hi, 120 + hi, 200), (50, 70, 140), (20, 30, 70, 255))
    img.alpha_composite(body)
    d = ImageDraw.Draw(img)
    d.rectangle(r((20, 14, 44, 28)), fill=(200, 205, 215, 255), outline=(90, 95, 110, 255))
    d.rectangle(r((36, 16, 41, 26)), fill=(70, 74, 86, 255))
    d.rectangle(r((16, 34, 48, 46)), fill=(240, 240, 240, 255))
    return shadow(img)


def drawer(selected):
    img = canvas(64, 64)
    hi = 25 if selected else 0
    back = shaded(img.size, r((6, 14, 34, 26)), 3 * S, (240, 200, 90), (220, 170, 60), (130, 90, 20, 255))
    img.alpha_composite(back)
    front = shaded(img.size, r((6, 20, 58, 52)), 3 * S, (250, 216 + hi // 2, 110 + hi), (225, 170, 60), (130, 90, 20, 255))
    img.alpha_composite(front)
    return shadow(img)


def file_icon(selected):
    img = canvas(64, 64)
    d = ImageDraw.Draw(img)
    paper = (255, 255, 255, 255) if not selected else (220, 232, 255, 255)
    d.polygon(r((14, 6, 42, 6, 52, 16, 52, 58, 14, 58)), fill=paper, outline=(110, 115, 130, 255))
    d.polygon(r((42, 6, 42, 16, 52, 16)), fill=(210, 214, 224, 255), outline=(110, 115, 130, 255))
    for i in range(5):
        y = 24 + i * 7
        d.line(r((20, y, 46, y)), fill=(170, 175, 190, 255), width=S)
    return shadow(img)


def program(selected):
    img = canvas(64, 64)
    hi = 30 if selected else 0
    win = shaded(img.size, r((8, 12, 56, 52)), 4 * S, (236, 238, 244), (200, 204, 214), (80, 84, 96, 255))
    img.alpha_composite(win)
    d = ImageDraw.Draw(img)
    d.rounded_rectangle(r((8, 12, 56, 21)), 4 * S, fill=(70 + hi, 110 + hi, 190, 255))
    d.polygon(r((22, 30, 34, 37, 22, 44)), fill=(70, 74, 86, 255))
    d.line(r((36, 44, 46, 44)), fill=(70, 74, 86, 255), width=2 * S)
    return shadow(img)


ICONS = {
    "harddisk": harddisk, "home": home, "ram": ram, "system": system,
    "removable": removable, "drawer": drawer, "file": file_icon, "program": program,
}

# ---------------------------------------------------------------- gadgets (18 x 18)

def gadget_base(pressed, active):
    img = canvas(18, 18)
    top, bottom = ((250, 251, 253), (204, 208, 218)) if not pressed else ((170, 176, 190), (214, 218, 226))
    if not active:
        top, bottom = (228, 228, 230), (206, 206, 210)
    layer = shaded(img.size, r((1, 1, 17, 17)), 4 * S, top, bottom, (96, 100, 112, 255))
    img.alpha_composite(layer)
    return img, ImageDraw.Draw(img)


INK = (52, 56, 66, 255)


def g_close(d):
    d.ellipse(r((6, 6, 12, 12)), fill=INK)


def g_iconify(d):
    d.rectangle(r((5, 11, 10, 14)), outline=INK, width=int(1.5 * S))


def g_zoom(d):
    d.rectangle(r((4, 4, 14, 14)), outline=INK, width=int(1.5 * S))
    d.rectangle(r((4, 4, 9, 9)), fill=INK)


def g_depth(d):
    d.rectangle(r((3, 3, 11, 11)), outline=INK, width=int(1.5 * S))
    d.rectangle(r((7, 7, 15, 15)), fill=(250, 251, 253, 255), outline=INK, width=int(1.5 * S))


def g_jump(d):
    d.polygon(r((4, 9, 10, 4, 10, 7, 14, 7, 14, 11, 10, 11, 10, 14)), fill=INK)


def g_prefs(d):
    import math
    cx, cy = 9 * S, 9 * S
    pts = []
    for i in range(16):
        a = i * math.pi / 8
        rad = (6 if i % 2 == 0 else 4.4) * S
        pts.append((cx + rad * math.cos(a), cy + rad * math.sin(a)))
    d.polygon(pts, fill=INK)
    d.ellipse((cx - 2 * S, cy - 2 * S, cx + 2 * S, cy + 2 * S), fill=(230, 232, 238, 255))


def g_resize(d):
    for k in (0, 4, 8):
        d.line(r((14 - k, 15, 15, 14 - k)), fill=INK, width=int(1.3 * S))


GADGETS = {
    "close": g_close, "iconify": g_iconify, "zoom": g_zoom, "depth": g_depth,
    "jump": g_jump, "prefs": g_prefs, "resize": g_resize,
}


# ---------------------------------------------------------------- gradients (stretched)

def vgradient(name, h, stops, w=4):
    img = Image.new("RGBA", (w, h))
    d = ImageDraw.Draw(img)
    for y in range(h):
        t = y / max(1, h - 1)
        for i in range(len(stops) - 1):
            t0, c0 = stops[i]
            t1, c1 = stops[i + 1]
            if t0 <= t <= t1:
                k = (t - t0) / max(1e-9, t1 - t0)
                c = tuple(int(c0[j] + (c1[j] - c0[j]) * k) for j in range(len(c0)))
                if len(c) == 3:
                    c += (255,)
                d.line([(0, y), (w, y)], fill=c)
                break
    img.save(os.path.join(OUT, name + ".png"))


GRADIENTS = {
    "bar": (24, [(0, (252, 252, 254)), (0.5, (232, 234, 240)), (1, (204, 208, 218))]),
    "bar_sel": (24, [(0, (110, 150, 220)), (1, (60, 96, 170))]),
    "title_active": (22, [(0, (150, 182, 236)), (0.5, (104, 144, 214)), (1, (78, 114, 186))]),
    "title_inactive": (22, [(0, (238, 238, 242)), (1, (206, 208, 216))]),
    "menu_sel": (20, [(0, (112, 152, 222)), (1, (70, 108, 182))]),
    "backdrop": (512, [(0, (88, 112, 150)), (0.6, (58, 80, 118)), (1, (38, 52, 80))]),
}


def main():
    os.makedirs(OUT, exist_ok=True)
    for name, fn in ICONS.items():
        save(fn(False), "icon_%s" % name, 64, 64)
        save(fn(True), "icon_%s_sel" % name, 64, 64)
    for name, fn in GADGETS.items():
        for state, (pressed, active) in {"": (False, True), "_down": (True, True), "_inactive": (False, False)}.items():
            img, d = gadget_base(pressed, active)
            fn(d)
            save(img, "gadget_%s%s" % (name, state), 18, 18)
    for name, (h, stops) in GRADIENTS.items():
        vgradient("grad_" + name, h, stops)


if __name__ == "__main__":
    main()
