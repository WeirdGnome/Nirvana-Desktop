#!/usr/bin/env python3
"""Nirvana Desktop - the red pointer themes (Xcursor), drawn from scratch.

In the spirit of the red pointers of the 16-bit home computer desktops, but every shape here
is Nirvana's own: described as polygons on a 32 x 32 grid and drawn in three styles:

  Nirvana-Retro16-Classic   pixels of the 16-bit era: a slanted red arrow, a black edge and a
                            light shine, drawn on a 16 x 16 grid and enlarged by whole pixels
  Nirvana-Retro16-Outline   red with a white outline and a soft shadow, smooth at every size
  Nirvana-Retro16-Glass     a red gradient with a glassy shine and a dark red edge

Writes the themes into the cursors/ folder of the source tree (the build installs them into
share/icons). Needs Python 3 and Pillow:  python3 tools/cursors/make_cursors.py
"""
import math
import os
import shutil
import struct
import sys

from PIL import Image, ImageChops, ImageDraw, ImageFilter

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "cursors")
SIZES = [16, 24, 32, 48, 64]   # nominal sizes (Xcursor picks the one nearest to XCURSOR_SIZE)
U = 32.0                       # the shapes are drawn on a 32 x 32 grid

# --------------------------------------------------------------------------------------------
# The shapes: draw(d, s) paints the shape (255) into an "L" mask; s = pixels per grid unit.
# Each shape: (draw, hotspot on the grid)
# --------------------------------------------------------------------------------------------

def P(pts, s, ox=0.0, oy=0.0):
    return [((x + ox) * s, (y + oy) * s) for x, y in pts]


def rot(pts, ang, cx, cy):
    a = math.radians(ang)
    c, n = math.cos(a), math.sin(a)
    return [(cx + (x - cx) * c - (y - cy) * n, cy + (x - cx) * n + (y - cy) * c) for x, y in pts]


# the arrow: a little slanted (the 16-bit pointers leaned), its tail cut straight
ARROW = [(1, 1), (3.2, 23.5), (8.4, 18.6), (12.6, 27.4), (16.6, 25.6), (12.4, 16.9), (19.6, 16.2)]
ARROW_SMALL = [(x * 0.62, y * 0.62) for x, y in ARROW]


def arrow(d, s):
    d.polygon(P(ARROW, s), fill=255)


def arrow_small(d, s):
    d.polygon(P(ARROW_SMALL, s), fill=255)


def clock(d, s, cx, cy, r, frame, frames=8):
    """A round clock face (the busy pointer) with a hand - frame: its position."""
    d.ellipse([(cx - r) * s, (cy - r) * s, (cx + r) * s, (cy + r) * s], fill=255)
    rr = r * 0.66
    a = 2 * math.pi * frame / frames - math.pi / 2
    w = max(1.0, r * 0.22)
    # the hand and the center: cut out (holes show the edge color)
    hx, hy = cx + rr * math.cos(a), cy + rr * math.sin(a)
    nx, ny = -math.sin(a) * w / 2, math.cos(a) * w / 2
    d.polygon(P([(cx + nx, cy + ny), (hx + nx, hy + ny), (hx - nx, hy - ny), (cx - nx, cy - ny)], s), fill=0)
    d.polygon(P([(cx - w / 2, cy - r * 0.62), (cx + w / 2, cy - r * 0.62), (cx + w / 2, cy), (cx - w / 2, cy)], s), fill=0)
    for k in range(4):   # four marks
        b = k * math.pi / 2
        mx, my = cx + r * 0.86 * math.cos(b), cy + r * 0.86 * math.sin(b)
        d.ellipse([(mx - w * 0.5) * s, (my - w * 0.5) * s, (mx + w * 0.5) * s, (my + w * 0.5) * s], fill=0)


def watch(frame):
    def f(d, s):
        clock(d, s, 16, 16, 12.5, frame)
        # the winder on the top
        d.rectangle([14 * s, 1 * s, 18 * s, 4.2 * s], fill=255)
    return f


def progress(frame):
    def f(d, s):
        arrow_small(d, s)
        clock(d, s, 22.5, 22.5, 8.0, frame)
    return f


def ibeam(d, s):
    for r in ([10, 2, 22, 5], [14, 5, 18, 27], [10, 27, 22, 30]):
        d.rectangle([v * s for v in r], fill=255)


def hand(d, s):
    # the pointing finger, three folded fingers, the palm and the thumb
    d.rounded_rectangle([8 * s, 1 * s, 13.2 * s, 18 * s], radius=2.6 * s, fill=255)
    for x0 in (13.4, 17.6, 21.8):
        d.rounded_rectangle([x0 * s, 11 * s, (x0 + 4.4) * s, 20 * s], radius=2.0 * s, fill=255)
    d.rounded_rectangle([8 * s, 15 * s, 26.2 * s, 28 * s], radius=4.0 * s, fill=255)
    d.polygon(P([(8.5, 16), (3.4, 12.4), (1.4, 15), (7, 22.5), (9, 22)], s), fill=255)
    d.rectangle([10 * s, 26 * s, 24 * s, 31 * s], fill=255)
    # the creases between the fingers (holes)
    for x0 in (17.4, 21.6):
        d.rectangle([x0 * s, 12.5 * s, (x0 + 0.4) * s + 0.5, 17 * s], fill=0)


def cross(d, s):
    for r in ([14.5, 1, 17.5, 12], [14.5, 20, 17.5, 31], [1, 14.5, 12, 17.5], [20, 14.5, 31, 17.5]):
        d.rectangle([v * s for v in r], fill=255)
    d.rectangle([15 * s, 15 * s, 17 * s, 17 * s], fill=255)


DOUBLE = [(1, 16), (8, 9), (8, 13.5), (24, 13.5), (24, 9), (31, 16), (24, 23), (24, 18.5), (8, 18.5), (8, 23)]


def double(ang):
    def f(d, s):
        d.polygon(P(rot(DOUBLE, ang, 16, 16), s), fill=255)
    return f


def move(d, s):
    d.polygon(P(DOUBLE, s), fill=255)
    d.polygon(P(rot(DOUBLE, 90, 16, 16), s), fill=255)


def forbidden(d, s):
    d.ellipse([2 * s, 2 * s, 30 * s, 30 * s], fill=255)
    d.ellipse([6.6 * s, 6.6 * s, 25.4 * s, 25.4 * s], fill=0)
    d.polygon(P(rot([(4, 14), (28, 14), (28, 18), (4, 18)], 45, 16, 16), s), fill=255)


def question(d, s):
    arrow_small(d, s)
    # a "?" in a round badge
    d.ellipse([15 * s, 13 * s, 31 * s, 29 * s], fill=255)
    d.arc([19.2 * s, 15.6 * s, 26.8 * s, 23.2 * s], 190, 80, fill=0, width=max(1, int(1.9 * s)))
    d.rectangle([22.1 * s, 22 * s, 23.9 * s, 24 * s], fill=0)
    d.rectangle([22.1 * s, 25 * s, 23.9 * s, 26.8 * s], fill=0)


def badge(sym):
    def f(d, s):
        arrow_small(d, s)
        d.rectangle([17 * s, 17 * s, 31 * s, 31 * s], fill=255)
        w = max(1, int(2 * s))
        if sym == "+":
            d.rectangle([23 * s, 19.5 * s, 25 * s, 28.5 * s], fill=0)
            d.rectangle([19.5 * s, 23 * s, 28.5 * s, 25 * s], fill=0)
        else:   # a little bent arrow (a link)
            d.line(P([(20.5, 28), (20.5, 22.5), (27, 22.5)], s), fill=0, width=w)
            d.polygon(P([(26, 19.5), (29.5, 22.5), (26, 25.5)], s), fill=0)
    return f


# name -> (draw function or a list of animation frames, hotspot)
FRAMES = 8
SHAPES = {
    "left_ptr": (arrow, (1, 1)),
    "watch": ([watch(i) for i in range(FRAMES)], (16, 16)),
    "left_ptr_watch": ([progress(i) for i in range(FRAMES)], (1, 1)),
    "xterm": (ibeam, (16, 16)),
    "hand2": (hand, (10, 1)),
    "crosshair": (cross, (16, 16)),
    "fleur": (move, (16, 16)),
    "sb_h_double_arrow": (double(0), (16, 16)),
    "sb_v_double_arrow": (double(90), (16, 16)),
    "bd_double_arrow": (double(45), (16, 16)),
    "fd_double_arrow": (double(-45), (16, 16)),
    "not-allowed": (forbidden, (16, 16)),
    "question_arrow": (question, (1, 1)),
    "copy": (badge("+"), (1, 1)),
    "link": (badge("link"), (1, 1)),
}

# the other names programs ask for (X11 cursor font, CSS, Qt hashes) -> the files above
ALIASES = {
    "left_ptr": ["default", "arrow", "top_left_arrow", "left-arrow", "right_ptr", "draft_large", "draft_small",
                 "context-menu", "openhand", "closedhand", "grab", "grabbing", "dnd-none", "dnd-move", "move-cursor",
                 "center_ptr", "x-cursor", "X_cursor"],
    "watch": ["wait", "clock", "half-busy-unused"],
    "left_ptr_watch": ["progress", "half-busy", "08e8e1c95fe2fc01f976f1e063a24ccd", "3ecb610c1bf2410f44200f48c40d3599"],
    "xterm": ["text", "ibeam", "vertical-text"],
    "hand2": ["hand1", "hand", "pointer", "pointing_hand", "pointer-hand",
              "9d800788f1b08800ae810202380a0822", "e29285e634086352946a0e7090d73106"],
    "crosshair": ["cross", "tcross", "cross_reverse", "diamond_cross", "cell", "plus", "target", "dotbox", "icon"],
    "fleur": ["move", "all-scroll", "size_all", "fcf21c00b30f7e3f83fe0dfd12e71cff", "4498f0e0c1937ffe01fd06f973665830",
              "9081237383d90e509aa00f00170e968f"],
    "sb_h_double_arrow": ["h_double_arrow", "ew-resize", "col-resize", "size_hor", "left_side", "right_side",
                          "e-resize", "w-resize", "split_h", "028006030e0e7ebffc7f7070c0600140",
                          "14fef782d02440884392942c11205230"],
    "sb_v_double_arrow": ["v_double_arrow", "ns-resize", "row-resize", "size_ver", "top_side", "bottom_side",
                          "n-resize", "s-resize", "split_v", "double_arrow", "00008160000006810000408080010102",
                          "2870a09082c103050810ffdffffe0204"],
    "bd_double_arrow": ["size_fdiag", "nwse-resize", "top_left_corner", "bottom_right_corner", "nw-resize", "se-resize",
                        "c7088f0f3e6c8088236ef8e1e3e70000", "38c5dff7c7b8962045400281044508d2"],
    "fd_double_arrow": ["size_bdiag", "nesw-resize", "top_right_corner", "bottom_left_corner", "ne-resize", "sw-resize",
                        "fcf1c3c7cd4491d801f1e1c78f100000", "50585d75b494802d0151028115016902"],
    "not-allowed": ["crossed_circle", "forbidden", "no-drop", "circle", "03b6e0fcb3499374a867c041f52298f0"],
    "question_arrow": ["help", "whats_this", "left_ptr_help", "d9ce0ab605698f320427677b458ad60b",
                       "5c6cd98b3f3ebcb1f9c7f1c204630408"],
    "copy": ["dnd-copy", "1081e37283d90000800003c07f3ef6bf", "6407b0e94181790501fd1e167b474872"],
    "link": ["alias", "dnd-link", "3085a0e285430894940527032f8b26df", "640fb0e74195791501fd1ed57b41487f"],
}

# The 16-bit style arrow: leaning strongly, the head ending in a barb on the left and the tail
# going off diagonally to the lower right (drawn by hand on the 16 x 16 grid)
CLASSIC_ARROW = [
    "##..............",
    "####............",
    ".#####..........",
    ".#######........",
    "..########......",
    "..##########....",
    "...#######......",
    "...########.....",
    "....###.####....",
    "....##...####...",
    ".....#....####..",
    "...........####.",
    "............###.",
    ".............#..",
    "................",
    "................",
]
# the same arrow, smooth, on the 32 grid (the outline style)
LEAN_ARROW = [(1, 1), (24.6, 11.2), (14.8, 13.6), (29.2, 24.4), (26, 27.8), (12.6, 18.2), (10.4, 21.6)]


# the 16-bit pointer's proportions: a wide head pointing up and to the left, a short thick tail
WIDE_HEAD = [(0.6, 0.6), (25.5, 9.5), (15.5, 13.5), (13.5, 15.5), (9.5, 25.5)]
WIDE_TAIL = [(12.5, 16.5), (16.5, 12.5), (27.5, 23.5), (23.5, 27.5)]


def lean_arrow(d, s):
    d.polygon(P(WIDE_HEAD, s), fill=255)
    d.polygon(P(WIDE_TAIL, s), fill=255)


def lean_arrow_small(d, s):
    d.polygon(P([(x * 0.62, y * 0.62) for x, y in LEAN_ARROW], s), fill=255)


# --------------------------------------------------------------------------------------------
# The classic style: some shapes drawn by hand on the 16 x 16 grid (a polygon at 16 pixels
# loses them). "#" = the shape, "." = empty; the edge and the shine are added around it.
# --------------------------------------------------------------------------------------------

def grid(rows):
    m = Image.new("L", (16, 16), 0)
    px = m.load()
    for y, row in enumerate(rows):
        for x, c in enumerate(row):
            if c == "#":
                px[x, y] = 255
    return m


H_ARROW = ["................"] * 5 + [
    "...#........#...",
    "..##........##..",
    ".##############.",
    ".##############.",
    "..##........##..",
    "...#........#...",
] + ["................"] * 5

MOVE = [
    "................",
    ".......##.......",
    "......####......",
    ".....######.....",
    ".......##.......",
    "...#...##...#...",
    "..##...##...##..",
    ".##############.",
    ".##############.",
    "..##...##...##..",
    "...#...##...#...",
    ".......##.......",
    ".....######.....",
    "......####......",
    ".......##.......",
    "................",
]

QMARK = ["###", "..#", ".##", "...", ".#."]


def disc(m, cx, cy, r):
    px = m.load()
    for y in range(16):
        for x in range(16):
            if (x - cx) ** 2 + (y - cy) ** 2 <= r * r:
                px[x, y] = 255


def hand_pixels(m, cx, cy, length, frame, frames=FRAMES):
    """The clock's hand: holes from the middle towards the frame's direction."""
    px = m.load()
    a = 2 * math.pi * frame / frames - math.pi / 2
    for i in range(0, int(length * 4) + 1):
        t = i / 4
        x, y = int(round(cx + t * math.cos(a))), int(round(cy + t * math.sin(a)))
        if 0 <= x < 16 and 0 <= y < 16:
            px[x, y] = 0


def classic_watch(frame):
    def f():
        m = Image.new("L", (16, 16), 0)
        disc(m, 7.5, 8.5, 6.4)
        ImageDraw.Draw(m).rectangle([6, 0, 9, 1], fill=255)
        hand_pixels(m, 7.5, 8.5, 0.1, frame)         # the middle
        hand_pixels(m, 7.5, 8.5, 4.2, frame)         # the turning hand
        ImageDraw.Draw(m).rectangle([7, 5, 8, 8], fill=0)   # the hand at twelve
        return m
    return f


SMALL_ARROW = [
    "#.......",
    "###.....",
    ".####...",
    ".######.",
    "..####..",
    "..#.###.",
    ".....##.",
]


def small_arrow_grid():
    """A small arrow in the upper left corner, for the composite pointers."""
    m = Image.new("L", (16, 16), 0)
    px = m.load()
    for y, row in enumerate(SMALL_ARROW):
        for x, c in enumerate(row):
            if c == "#":
                px[1 + x, 1 + y] = 255
    return m


def classic_progress(frame):
    def f():
        m = small_arrow_grid()
        disc(m, 11.5, 11.5, 3.9)
        hand_pixels(m, 11.5, 11.5, 2.4, frame)
        return m
    return f


def classic_question():
    m = small_arrow_grid()
    disc(m, 11, 10.5, 4.6)
    px = m.load()
    for y, row in enumerate(QMARK):
        for x, c in enumerate(row):
            if c == "#":
                px[10 + x, 8 + y] = 0
    return m


CLASSIC = {
    "left_ptr": lambda: hard_mask(lean_arrow, 16),
    "watch": [classic_watch(i) for i in range(FRAMES)],
    "left_ptr_watch": [classic_progress(i) for i in range(FRAMES)],
    "sb_h_double_arrow": lambda: grid(H_ARROW),
    "sb_v_double_arrow": lambda: grid(H_ARROW).transpose(Image.TRANSPOSE),
    "fleur": lambda: grid(MOVE),
    "question_arrow": classic_question,
}

# the outline style: the leaning arrow too (the composite ones keep their small arrow)
OUTLINE = {
    "left_ptr": lean_arrow,
}

# --------------------------------------------------------------------------------------------
# The styles
# --------------------------------------------------------------------------------------------
RED = (0xD8, 0x1E, 0x1E)
RED_LIGHT = (0xFF, 0x7A, 0x5A)
RED_DARK = (0x6A, 0x08, 0x08)
CREAM = (0xFF, 0xE8, 0xC8)
AMIGA_RED = (0xE0, 0x20, 0x20)
AMIGA_LIGHT = (0xFF, 0xD8, 0xB8)
BLACK = (0x10, 0x08, 0x08)


def mask_of(draw, size, ss=4):
    """The shape at the size (supersampled - smooth edges)."""
    big = Image.new("L", (size * ss, size * ss), 0)
    draw(ImageDraw.Draw(big), size * ss / U)
    return big.resize((size, size), Image.LANCZOS)


def hard_mask(draw, size):
    """The shape on a pixel grid, without smoothing (the classic style)."""
    m = mask_of(draw, size, 4)
    return m.point(lambda v: 255 if v >= 110 else 0)


def shift(img, dx, dy):
    out = Image.new(img.mode, img.size, 0)
    out.paste(img, (dx, dy))
    return out


def dilate(m, r):
    return m.filter(ImageFilter.MaxFilter(2 * r + 1)) if r > 0 else m


def solid(color, size, alpha):
    img = Image.new("RGBA", (size, size), color + (0,))
    img.putalpha(alpha)
    return img


def style_classic(draw, size):
    # drawn on a 16 x 16 grid (the pixels of the era), enlarged by whole pixels;
    # draw: a shape (drawn at 16 pixels) or a hand-drawn grid (a function without arguments)
    n = 16
    k = classic_scale(size)
    m = draw() if draw.__code__.co_argcount == 0 else hard_mask(draw, n)
    # the three colors of the 16-bit pointers: red inside, a light edge where the light comes
    # from (up, left), black where the shade is (down, right) - and no outline around
    up = ImageChops.subtract(m, shift(m, 1, 0))                      # the left edge
    up = ImageChops.lighter(up, ImageChops.subtract(m, shift(m, 0, 1)))   # and the top
    down = ImageChops.subtract(m, shift(m, -1, 0))
    down = ImageChops.lighter(down, ImageChops.subtract(m, shift(m, 0, -1)))
    down = ImageChops.subtract(down, up)
    img = Image.new("RGBA", (n, n), (0, 0, 0, 0))
    img = Image.alpha_composite(img, solid(AMIGA_RED, n, m))
    img = Image.alpha_composite(img, solid(BLACK, n, down))
    img = Image.alpha_composite(img, solid(AMIGA_LIGHT, n, up))
    img = img.resize((n * k, n * k), Image.NEAREST)
    return img, 1.0


def classic_scale(size):
    """16 -> x1, 24 and 32 -> x2 (the pixels doubled), 48 -> x3, 64 -> x4"""
    return 1 if size < 20 else max(2, round(size / 16))


def soft_shadow(m, size, off, blur, alpha):
    sh = shift(m, off, off).filter(ImageFilter.GaussianBlur(blur))
    return solid((0, 0, 0), size, sh.point(lambda v: int(v * alpha)))


def style_outline(draw, size):
    # the 16-bit pointer made smooth: red, a light edge up and left, a black edge down and
    # right, a thin black line around and a soft shadow
    m = mask_of(draw, size)
    w = max(1, round(size / 18))
    line = dilate(m, max(1, w // 2))
    up = ImageChops.subtract(m, shift(m, w, w))
    down = ImageChops.subtract(m, shift(m, -w, -w))
    img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    img = Image.alpha_composite(img, soft_shadow(line, size, max(1, size // 14), size / 36, 0.42))
    img = Image.alpha_composite(img, solid(BLACK, size, line))
    img = Image.alpha_composite(img, solid(AMIGA_RED, size, m))
    img = Image.alpha_composite(img, solid((0x30, 0x00, 0x00), size, down))
    img = Image.alpha_composite(img, solid(AMIGA_LIGHT, size, up))
    return img, 1.0


def style_glass(draw, size):
    m = mask_of(draw, size)
    w = max(1, round(size / 32))
    edge = dilate(m, w)
    img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    img = Image.alpha_composite(img, soft_shadow(edge, size, max(1, size // 20), size / 32, 0.40))
    img = Image.alpha_composite(img, solid(RED_DARK, size, edge))
    # a vertical gradient: light red on top, deep red below
    grad = Image.new("RGBA", (size, size))
    gp = grad.load()
    for y in range(size):
        t = y / max(1, size - 1)
        c = tuple(int(RED_LIGHT[i] * (1 - t) + (0xA8, 0x10, 0x10)[i] * t) for i in range(3))
        for x in range(size):
            gp[x, y] = c + (255,)
    grad.putalpha(m)
    img = Image.alpha_composite(img, grad)
    # the glassy shine: the upper part of the shape, lighter
    inner = m.filter(ImageFilter.MinFilter(3)) if size >= 24 else m
    band = Image.new("L", (size, size), 0)
    ImageDraw.Draw(band).rectangle([0, 0, size, int(size * 0.42)], fill=255)
    shine = ImageChops.multiply(inner, band).point(lambda v: int(v * 0.42))
    img = Image.alpha_composite(img, solid((0xFF, 0xFF, 0xFF), size, shine))
    return img, 1.0


STYLES = {
    "Nirvana-Retro16-Classic": ("Retro 16 · classic", style_classic),
    "Nirvana-Retro16-Outline": ("Retro 16 · outline", style_outline),
    "Nirvana-Retro16-Glass": ("Retro 16 · glass", style_glass),
}

# --------------------------------------------------------------------------------------------
# The Xcursor file
# --------------------------------------------------------------------------------------------

def xcursor(images):
    """images: [(nominal, img RGBA, xhot, yhot, delay)] -> the bytes of an Xcursor file."""
    ntoc = len(images)
    header = struct.pack("<4sIII", b"Xcur", 16, 0x10000, ntoc)
    pos = 16 + ntoc * 12
    toc, chunks = b"", b""
    for nominal, img, xh, yh, delay in images:
        w, h = img.size
        px = img.load()
        data = bytearray()
        for y in range(h):
            for x in range(w):
                r, g, b, a = px[x, y]
                # premultiplied ARGB
                data += struct.pack("<I", (a << 24) | ((r * a // 255) << 16) | ((g * a // 255) << 8) | (b * a // 255))
        chunk = struct.pack("<IIIIIIIII", 36, 0xFFFD0002, nominal, 1, w, h, xh, yh, delay) + bytes(data)
        toc += struct.pack("<III", 0xFFFD0002, nominal, pos)
        pos += len(chunk)
        chunks += chunk
    return header + toc + chunks


def build(theme, title, style):
    out = os.path.join(ROOT, theme)
    if os.path.isdir(out):
        shutil.rmtree(out)
    os.makedirs(os.path.join(out, "cursors"))
    with open(os.path.join(out, "index.theme"), "w", encoding="utf-8") as f:
        f.write("[Icon Theme]\nName=%s\nComment=Nirvana Desktop: red pointers in the spirit of the 16-bit desktops\n"
                "Inherits=Adwaita\n" % title)
    for name, (draw, (hx, hy)) in SHAPES.items():
        if style is style_classic and name in CLASSIC:
            draw = CLASSIC[name]
        if style is style_outline and name in OUTLINE:
            draw = OUTLINE[name]
        frames = draw if isinstance(draw, list) else [draw]
        images = []
        for size in SIZES:
            for fr in frames:
                img, _ = style(fr, size)
                s = size / U
                if style is style_classic:
                    k = classic_scale(size)
                    xh, yh = int(hx / 2) * k, int(hy / 2) * k
                else:
                    xh, yh = int(round(hx * s)), int(round(hy * s))
                xh, yh = min(xh, img.size[0] - 1), min(yh, img.size[1] - 1)
                images.append((size, img, xh, yh, 110 if len(frames) > 1 else 0))
        with open(os.path.join(out, "cursors", name), "wb") as f:
            f.write(xcursor(images))
        for a in ALIASES.get(name, []):
            p = os.path.join(out, "cursors", a)
            if not os.path.exists(p):
                os.symlink(name, p)
    # a preview for the Appearance panel and the README (the arrow, 32 px)
    img, _ = style(arrow, 32)
    img.save(os.path.join(out, "preview.png"))
    print("written", theme)


if __name__ == "__main__":
    for theme, (title, style) in STYLES.items():
        if len(sys.argv) > 1 and theme not in sys.argv[1:]:
            continue
        build(theme, title, style)
