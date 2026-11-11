#!/usr/bin/env python3
# Nirvana Desktop - generator of the "Nirvana Desktop" wallpaper in a glossy late-1990s/2000s style:
# a navy depth, a nebula, ribbons of light, a mirror floor and a glass logo.
# Everything drawn from scratch (no third-party graphics). Font: Inter (SIL OFL).
#
# python3 tools/wallpaper-nirvana-desktop.py wallpapers/"Nirvana Desktop.jpg" [width height]
import math
import random
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

OUT = sys.argv[1] if len(sys.argv) > 1 else "Nirvana Desktop.jpg"
W = int(sys.argv[2]) if len(sys.argv) > 2 else 2560
H = int(sys.argv[3]) if len(sys.argv) > 3 else 1440
S = W / 2560.0                      # scale relative to the 2560x1440 design
HORIZON = int(H * 0.64)             # the "floor" line
rng = random.Random(1984)
nrng = np.random.default_rng(1984)
FONTS = "/usr/share/fonts/opentype/inter/"


def font(name, size):
    try:
        return ImageFont.truetype(FONTS + name, int(size * S))
    except OSError:
        return ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", int(size * S))


def blur(a, r):
    """Gaussian blur of a float HxWx3 array (through PIL, channel by channel)."""
    out = np.empty_like(a)
    for c in range(a.shape[2]):
        m = a[..., c].max() or 1.0
        im = Image.fromarray(np.clip(a[..., c] / m * 255, 0, 255).astype(np.uint8))
        out[..., c] = np.asarray(im.filter(ImageFilter.GaussianBlur(r)), dtype=np.float32) / 255 * m
    return out


yy, xx = np.mgrid[0:H, 0:W].astype(np.float32)

# 1. Background: deep navy, a brighter glow behind the logo
img = np.zeros((H, W, 3), np.float32)
top, mid, deep = np.array([4, 8, 30.]), np.array([16, 44, 110.]), np.array([2, 4, 16.])
t = yy / H
img[:] = (top * (1 - t)[..., None] + deep * t[..., None])
cx, cy = W * 0.5, H * 0.5
d = np.sqrt(((xx - cx) / (W * 0.55)) ** 2 + ((yy - cy) / (H * 0.5)) ** 2)
glow = np.clip(1 - d, 0, 1) ** 2
img += glow[..., None] * (mid - top)

# 2. Nebula: noise at several scales, violet and turquoise
def nebula():
    n = np.zeros((H, W), np.float32)
    for scale, amp in ((6, 1.0), (14, 0.5), (32, 0.25)):
        small = nrng.random((scale * H // W + 2, scale + 2)).astype(np.float32)
        big = np.asarray(Image.fromarray((small * 255).astype(np.uint8)).resize((W, H), Image.BICUBIC),
                         np.float32) / 255
        n += big * amp
    n = np.asarray(Image.fromarray((n / n.max() * 255).astype(np.uint8)).filter(
        ImageFilter.GaussianBlur(20 * S)), np.float32) / 255   # without interpolation artifacts
    return n ** 3


mask = np.clip(1 - np.abs(yy - H * 0.38) / (H * 0.45), 0, 1)
img += (nebula() * mask)[..., None] * np.array([70, 40, 120.]) * 0.9
img += (nebula() * mask)[..., None] * np.array([10, 70, 110.]) * 0.6

# 3. Stars
stars = np.zeros((H, W, 3), np.float32)
for _ in range(int(900 * S * S)):
    x, y = rng.randrange(W), rng.randrange(int(HORIZON * 0.95))
    b = rng.random() ** 3 * 255
    stars[y, x] = (b * 0.8, b * 0.9, b)
img += stars + blur(stars, 2.5 * S) * 3

# 4. Ribbons of light: gentle waves from left to right with a glow
ribbons = np.zeros((H, W, 3), np.float32)
layer = Image.new("RGB", (W, H))
dr = ImageDraw.Draw(layer)
specs = [  # (height, amplitude, frequency, phase, color, thickness)
    (0.50, 0.10, 1.3, 0.2, (90, 200, 255), 3.0),
    (0.53, 0.13, 1.1, 1.6, (70, 120, 255), 2.2),
    (0.47, 0.08, 1.7, 2.9, (170, 110, 255), 2.0),
    (0.56, 0.15, 0.9, 4.1, (60, 220, 230), 1.6),
    (0.44, 0.11, 1.5, 5.3, (255, 150, 60), 1.8),   # orange accent
    (0.58, 0.09, 1.2, 0.9, (120, 160, 255), 1.2),
]
for base, amp, freq, ph, col, wid in specs:
    for k in range(5):  # a bundle of thin threads makes a ribbon
        off = (k - 2) * 7 * S
        pts = []
        for x in range(-20, W + 20, 6):
            u = x / W
            y = H * (base + amp * math.sin(u * math.pi * freq * 2 + ph) * (0.4 + 0.6 * u)
                     + 0.03 * math.sin(u * 9 + ph * 2)) + off * (0.3 + u)
            pts.append((x, y))
        fade = 1 - abs(k - 2) * 0.28
        dr.line(pts, fill=tuple(int(c * fade) for c in col), width=max(1, int(wid * S)))
rib = np.asarray(layer, np.float32)
# the ribbons fade at the left edge and near the logo, so they do not fight with the title
fade_x = np.clip(xx / (W * 0.25), 0, 1)
rib *= fade_x[..., None]
# behind the title the ribbons dim (a soft ellipse around the logo and the title)
bx, by = W * 0.5, HORIZON - 175 * S
behind = np.clip(1 - np.sqrt(((xx - bx) / (W * 0.30)) ** 2 + ((yy - by) / (170 * S)) ** 2), 0, 1)
rib *= (1 - 0.8 * np.clip(behind * 1.8, 0, 1))[..., None]
ribbons = rib * 0.9 + blur(rib, 6 * S) * 2.2 + blur(rib, 26 * S) * 2.5 + blur(rib, 80 * S) * 1.6
img += ribbons * (yy < HORIZON)[..., None]

# 5. The logo and the "Nirvana Desktop" title (a separate RGBA layer - needed for the reflection)
brand = Image.new("RGBA", (W, H), (0, 0, 0, 0))
bd = ImageDraw.Draw(brand)
L = int(330 * S)                                   # size of the chip (logo)
f_nirvana, f_desk = font("InterDisplay-Bold.otf", 190), font("InterDisplay-ExtraLight.otf", 190)
gap = int(30 * S)
w_nirvana = bd.textlength("Nirvana", font=f_nirvana)
w_desk = bd.textlength(" Desktop", font=f_desk)
total = L + gap + w_nirvana + w_desk
x0 = int((W - total) / 2)
y0 = HORIZON - int(L * 56 / 64) - int(40 * S)      # the pins stand just above the "floor"

# 5a. The logo: a chip seen from the side - a golden rim, a green board with golden traces,
#     pins under the lower edges and a silver N with a violet shadow (the same as draw_logo)
chip = Image.new("RGBA", (L, L), (0, 0, 0, 0))
cd = ImageDraw.Draw(chip)
u = L / 64.0
P = lambda pts: [(px * u, py * u) for px, py in pts]
for side in (0, 1):                                     # the pins
    for i in range(9):
        t = (i + 0.7) / 9.4
        px, py = (32 + 26 * t, 48 - 16 * t) if side else (6 + 26 * t, 32 + 16 * t)
        cd.rectangle([px * u, py * u, (px + 1.7) * u, (py + 6) * u], fill=(95, 89, 76, 255))
        cd.rectangle([px * u, py * u, (px + 1.0) * u, (py + 5.2) * u], fill=(222, 214, 192, 255))
cd.polygon(P([(4, 32), (32, 15), (60, 32), (32, 50)]), fill=(122, 90, 18, 255))
cd.polygon(P([(5, 31), (32, 14), (59, 31), (32, 48)]), fill=(232, 185, 58, 255))
cd.line(P([(6, 31), (32, 15), (58, 31)]), fill=(255, 227, 138, 255), width=max(2, int(u * 0.8)))
cd.polygon(P([(10, 31), (32, 17.5), (54, 31), (32, 44.5)]), fill=(46, 138, 78, 255))
for a, b, c, d in ((32, 19, 32, 24), (24, 24, 28, 27), (40, 24, 36, 27), (16, 31, 22, 31),
                   (42, 31, 48, 31), (24, 38, 28, 35), (40, 38, 36, 35), (32, 43, 32, 39)):
    cd.line(P([(a, b), (c, d)]), fill=(214, 178, 74, 255), width=max(2, int(u * 0.6)))
    cd.ellipse([a * u - u * 0.7, b * u - u * 0.7, a * u + u * 0.7, b * u + u * 0.7], fill=(214, 178, 74, 255))
f_n = font("InterDisplay-Bold.otf", 21 * u / S)
cx, cy = 32 * u, 31 * u
cd.text((cx + 1.4 * u, cy + 1.4 * u), "N", font=f_n, fill=(90, 42, 136, 255), anchor="mm")
cd.text((cx, cy), "N", font=f_n, fill=(242, 238, 226, 255), anchor="mm")
def spark(px, py, r, col):
    cd.polygon(P([(px - r, py), (px, py - r * 0.22), (px + r, py), (px, py + r * 0.22)]), fill=col)
    cd.polygon(P([(px, py - r), (px + r * 0.22, py), (px, py + r), (px - r * 0.22, py)]), fill=col)
spark(52, 10, 6, (255, 212, 90, 255))
spark(12, 14, 3.5, (127, 240, 224, 255))
spark(58, 22, 3, (255, 212, 90, 255))
brand.alpha_composite(chip, (x0, y0))

# 5b. The title: "Nirvana" bold and white, "Desktop" thin and light blue
tx = x0 + L + gap
ty = y0 + int(L * 31 / 64)
bd.text((tx, ty), "Nirvana", font=f_nirvana, fill=(255, 255, 255, 255), anchor="lm")
bd.text((tx + w_nirvana, ty), " Desktop", font=f_desk, fill=(150, 200, 255, 255), anchor="lm")
# a letter-spaced subtitle below the title
f_sub = font("Inter-Medium.otf", 30)
sub = "  ".join("NIRVANA 0.1")
bd.text((tx + 6 * S, ty + int(120 * S)), sub, font=f_sub, fill=(150, 175, 230, 210), anchor="lm")

# a glow below the logo and the title
ba = np.asarray(brand, np.float32)
halo = blur(ba[..., :3] * (ba[..., 3:] / 255) * np.array([0.5, 0.7, 1.0]), 30 * S)
img += halo * 0.8

# 6. Floor: darker, with a reflection of the lights and a slight gradient
floor = yy >= HORIZON
refl = np.flipud(img[2 * HORIZON - H:HORIZON]) if 2 * HORIZON >= H else None
if refl is not None:
    depth = (yy[HORIZON:] - HORIZON) / (H - HORIZON)
    k = (0.38 * (1 - depth) ** 1.6)[..., None]
    base_floor = np.array([3, 6, 20.]) * (1 - depth[..., None]) + np.array([1, 2, 8.]) * depth[..., None]
    img[HORIZON:] = base_floor + refl * k
# horizon line
hl = np.exp(-((yy - HORIZON) / (2.5 * S)) ** 2) * np.clip(1 - np.abs(xx - W / 2) / (W * 0.48), 0, 1) ** 1.5
img += hl[..., None] * np.array([90, 150, 255.]) * 1.2

# 7. Composition: background + logo/title + their reflection on the floor
out = Image.fromarray(np.clip(img, 0, 255).astype(np.uint8), "RGB").convert("RGBA")
bottom = y0 + int(L * 57 / 64)
mirror = brand.crop((0, y0, W, bottom)).transpose(Image.FLIP_TOP_BOTTOM)
ma = np.asarray(mirror, np.float32)
fadev = np.linspace(0.32, 0, ma.shape[0])[:, None] ** 1.3
ma[..., 3] *= fadev / 0.32 * 0.32
mirror = Image.fromarray(ma.astype(np.uint8), "RGBA").filter(ImageFilter.GaussianBlur(1.2 * S))
out.alpha_composite(mirror, (0, HORIZON + (HORIZON - bottom)))
out.alpha_composite(brand)

# 8. A subtle vignette and saving
o = np.asarray(out.convert("RGB"), np.float32)
vig = 1 - 0.35 * np.clip(np.sqrt(((xx - W / 2) / (W / 2)) ** 2 + ((yy - H / 2) / (H / 2)) ** 2) - 0.6, 0, 1)
o *= vig[..., None]
o += nrng.normal(0, 1.2, o.shape)               # slight noise - no banding in the gradients
Image.fromarray(np.clip(o, 0, 255).astype(np.uint8)).save(OUT, quality=92, optimize=True, subsampling=0)
print(OUT)
