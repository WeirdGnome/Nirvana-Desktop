#!/usr/bin/env python3
# LisaOS - generator tapety "Lisa Desktop" w duchu AmigaOS 4:
# granatowa głębia, mgławica, świetlne wstęgi, lustrzana podłoga i szklane logo.
# Wszystko rysowane od zera (bez cudzych grafik). Czcionka: Inter (SIL OFL).
#
#   python3 tools/tapeta-lisa-desktop.py tapety/"Lisa Desktop.jpg" [szerokość wysokość]
import math
import random
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

OUT = sys.argv[1] if len(sys.argv) > 1 else "Lisa Desktop.jpg"
W = int(sys.argv[2]) if len(sys.argv) > 2 else 2560
H = int(sys.argv[3]) if len(sys.argv) > 3 else 1440
S = W / 2560.0                      # skala względem projektu 2560x1440
HORIZON = int(H * 0.64)             # linia "podłogi"
rng = random.Random(1984)
nrng = np.random.default_rng(1984)
FONTS = "/usr/share/fonts/opentype/inter/"


def font(name, size):
    try:
        return ImageFont.truetype(FONTS + name, int(size * S))
    except OSError:
        return ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", int(size * S))


def blur(a, r):
    """Rozmycie gaussowskie tablicy float HxWx3 (przez PIL, kanał po kanale)."""
    out = np.empty_like(a)
    for c in range(a.shape[2]):
        m = a[..., c].max() or 1.0
        im = Image.fromarray(np.clip(a[..., c] / m * 255, 0, 255).astype(np.uint8))
        out[..., c] = np.asarray(im.filter(ImageFilter.GaussianBlur(r)), dtype=np.float32) / 255 * m
    return out


yy, xx = np.mgrid[0:H, 0:W].astype(np.float32)

# 1. Tło: głęboki granat, jaśniejsza poświata za logo
img = np.zeros((H, W, 3), np.float32)
top, mid, deep = np.array([4, 8, 30.]), np.array([16, 44, 110.]), np.array([2, 4, 16.])
t = yy / H
img[:] = (top * (1 - t)[..., None] + deep * t[..., None])
cx, cy = W * 0.5, H * 0.5
d = np.sqrt(((xx - cx) / (W * 0.55)) ** 2 + ((yy - cy) / (H * 0.5)) ** 2)
glow = np.clip(1 - d, 0, 1) ** 2
img += glow[..., None] * (mid - top)

# 2. Mgławica: szum w kilku skalach, fiolet i turkus
def nebula():
    n = np.zeros((H, W), np.float32)
    for scale, amp in ((6, 1.0), (14, 0.5), (32, 0.25)):
        small = nrng.random((scale * H // W + 2, scale + 2)).astype(np.float32)
        big = np.asarray(Image.fromarray((small * 255).astype(np.uint8)).resize((W, H), Image.BICUBIC),
                         np.float32) / 255
        n += big * amp
    n = np.asarray(Image.fromarray((n / n.max() * 255).astype(np.uint8)).filter(
        ImageFilter.GaussianBlur(20 * S)), np.float32) / 255   # bez śladów interpolacji
    return n ** 3


mask = np.clip(1 - np.abs(yy - H * 0.38) / (H * 0.45), 0, 1)
img += (nebula() * mask)[..., None] * np.array([70, 40, 120.]) * 0.9
img += (nebula() * mask)[..., None] * np.array([10, 70, 110.]) * 0.6

# 3. Gwiazdy
stars = np.zeros((H, W, 3), np.float32)
for _ in range(int(900 * S * S)):
    x, y = rng.randrange(W), rng.randrange(int(HORIZON * 0.95))
    b = rng.random() ** 3 * 255
    stars[y, x] = (b * 0.8, b * 0.9, b)
img += stars + blur(stars, 2.5 * S) * 3

# 4. Świetlne wstęgi: łagodne fale od lewej do prawej z poświatą
ribbons = np.zeros((H, W, 3), np.float32)
layer = Image.new("RGB", (W, H))
dr = ImageDraw.Draw(layer)
specs = [  # (wysokość, amplituda, częstotliwość, faza, kolor, grubość)
    (0.50, 0.10, 1.3, 0.2, (90, 200, 255), 3.0),
    (0.53, 0.13, 1.1, 1.6, (70, 120, 255), 2.2),
    (0.47, 0.08, 1.7, 2.9, (170, 110, 255), 2.0),
    (0.56, 0.15, 0.9, 4.1, (60, 220, 230), 1.6),
    (0.44, 0.11, 1.5, 5.3, (255, 150, 60), 1.8),   # pomarańczowy akcent
    (0.58, 0.09, 1.2, 0.9, (120, 160, 255), 1.2),
]
for base, amp, freq, ph, col, wid in specs:
    for k in range(5):  # wiązka kilku cienkich nitek tworzy wstęgę
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
# wstęgi gasną przy lewej krawędzi i przy logo, żeby nie walczyły z napisem
fade_x = np.clip(xx / (W * 0.25), 0, 1)
rib *= fade_x[..., None]
# za napisem wstęgi przygasają (łagodna elipsa wokół logo i napisu)
bx, by = W * 0.5, HORIZON - 175 * S
behind = np.clip(1 - np.sqrt(((xx - bx) / (W * 0.30)) ** 2 + ((yy - by) / (170 * S)) ** 2), 0, 1)
rib *= (1 - 0.8 * np.clip(behind * 1.8, 0, 1))[..., None]
ribbons = rib * 0.9 + blur(rib, 6 * S) * 2.2 + blur(rib, 26 * S) * 2.5 + blur(rib, 80 * S) * 1.6
img += ribbons * (yy < HORIZON)[..., None]

# 5. Logo i napis "Lisa Desktop" (osobna warstwa RGBA - potrzebna do odbicia)
brand = Image.new("RGBA", (W, H), (0, 0, 0, 0))
bd = ImageDraw.Draw(brand)
L = int(230 * S)                                   # wielkość układu scalonego (logo)
f_lisa, f_desk = font("InterDisplay-Bold.otf", 190), font("InterDisplay-ExtraLight.otf", 190)
gap = int(56 * S)
w_lisa = bd.textlength("Lisa", font=f_lisa)
w_desk = bd.textlength(" Desktop", font=f_desk)
total = L + gap + w_lisa + w_desk
x0 = int((W - total) / 2)
y0 = HORIZON - L - int(40 * S)                     # stoi tuż nad "podłogą"

# 5a. Układ scalony: nóżki, korpus z połyskiem, białe L, złoty znacznik pinu 1
chip = Image.new("RGBA", (L, L), (0, 0, 0, 0))
cd = ImageDraw.Draw(chip)
pin_w, pin_h = int(L * 0.075), int(L * 0.11)
for i in range(5):
    px = int(L * 0.14 + i * (L * 0.72 - pin_w) / 4)
    for py in (0, L - pin_h):
        cd.rounded_rectangle([px, py, px + pin_w, py + pin_h], radius=int(pin_w * 0.25), fill=(196, 202, 214, 255))
        cd.rectangle([px + 2, py + 2, px + pin_w // 2, py + pin_h - 2], fill=(240, 244, 250, 255))
body = [int(L * 0.04), int(L * 0.09), int(L * 0.96), int(L * 0.91)]
r = int(L * 0.09)
grad = Image.new("RGBA", (L, L))
ga = np.zeros((L, L, 4), np.uint8)
gy = np.linspace(0, 1, L)[:, None]
ga[..., 0] = (120 - 60 * gy).astype(np.uint8)
ga[..., 1] = (92 - 52 * gy).astype(np.uint8)
ga[..., 2] = (210 - 70 * gy).astype(np.uint8)
ga[..., 3] = 255
grad = Image.fromarray(ga, "RGBA")
m = Image.new("L", (L, L), 0)
ImageDraw.Draw(m).rounded_rectangle(body, radius=r, fill=255)
chip.paste(grad, (0, 0), m)
cd.rounded_rectangle(body, radius=r, outline=(20, 12, 50, 255), width=max(2, int(L * 0.018)))
# litera L
lw = int(L * 0.11)
lx, ly = int(L * 0.40), int(L * 0.25)
cd.rectangle([lx + 4, ly + 4, lx + lw + 4, int(L * 0.72) + 4], fill=(30, 18, 70, 160))
cd.rectangle([lx + 4, int(L * 0.72) - lw + 4, int(L * 0.66) + 4, int(L * 0.72) + 4], fill=(30, 18, 70, 160))
cd.rectangle([lx, ly, lx + lw, int(L * 0.72)], fill=(255, 255, 255, 255))
cd.rectangle([lx, int(L * 0.72) - lw, int(L * 0.66), int(L * 0.72)], fill=(255, 255, 255, 255))
# złoty znacznik
pr = int(L * 0.055)
cd.ellipse([int(L * 0.12), int(L * 0.16), int(L * 0.12) + 2 * pr, int(L * 0.16) + 2 * pr], fill=(246, 196, 70, 255))
# szklany połysk na górnej połowie korpusu
gloss = Image.new("L", (L, L), 0)
gd = ImageDraw.Draw(gloss)
gd.rounded_rectangle([body[0] + 6, body[1] + 6, body[2] - 6, int(L * 0.48)], radius=r, fill=88)
gloss = gloss.filter(ImageFilter.GaussianBlur(L * 0.01))
gloss_np = np.asarray(gloss, np.float32) * np.linspace(1, 0.25, L)[:, None]
white = Image.new("RGBA", (L, L), (255, 255, 255, 0))
white.putalpha(Image.fromarray(np.clip(gloss_np, 0, 255).astype(np.uint8)))
chip = Image.alpha_composite(chip, white)
brand.alpha_composite(chip, (x0, y0))

# 5b. Napis: "Lisa" grubo i biało, "Desktop" cienko i błękitnie
tx = x0 + L + gap
ty = y0 + L // 2
bd.text((tx, ty), "Lisa", font=f_lisa, fill=(255, 255, 255, 255), anchor="lm")
bd.text((tx + w_lisa, ty), " Desktop", font=f_desk, fill=(150, 200, 255, 255), anchor="lm")
# podtytuł pod napisem, rozstrzelony
f_sub = font("Inter-Medium.otf", 30)
sub = "  ".join("LISAOS 0.1")
bd.text((tx + 6 * S, ty + int(120 * S)), sub, font=f_sub, fill=(150, 175, 230, 210), anchor="lm")

# poświata pod logo i napisem
ba = np.asarray(brand, np.float32)
halo = blur(ba[..., :3] * (ba[..., 3:] / 255) * np.array([0.5, 0.7, 1.0]), 30 * S)
img += halo * 0.8

# 6. Podłoga: ciemniejsza, z odbiciem świateł i lekkim gradientem
floor = yy >= HORIZON
refl = np.flipud(img[2 * HORIZON - H:HORIZON]) if 2 * HORIZON >= H else None
if refl is not None:
    depth = (yy[HORIZON:] - HORIZON) / (H - HORIZON)
    k = (0.38 * (1 - depth) ** 1.6)[..., None]
    base_floor = np.array([3, 6, 20.]) * (1 - depth[..., None]) + np.array([1, 2, 8.]) * depth[..., None]
    img[HORIZON:] = base_floor + refl * k
# linia horyzontu
hl = np.exp(-((yy - HORIZON) / (2.5 * S)) ** 2) * np.clip(1 - np.abs(xx - W / 2) / (W * 0.48), 0, 1) ** 1.5
img += hl[..., None] * np.array([90, 150, 255.]) * 1.2

# 7. Złożenie: tło + logo/napis + ich odbicie na podłodze
out = Image.fromarray(np.clip(img, 0, 255).astype(np.uint8), "RGB").convert("RGBA")
bottom = y0 + L
mirror = brand.crop((0, y0, W, bottom)).transpose(Image.FLIP_TOP_BOTTOM)
ma = np.asarray(mirror, np.float32)
fadev = np.linspace(0.32, 0, ma.shape[0])[:, None] ** 1.3
ma[..., 3] *= fadev / 0.32 * 0.32
mirror = Image.fromarray(ma.astype(np.uint8), "RGBA").filter(ImageFilter.GaussianBlur(1.2 * S))
out.alpha_composite(mirror, (0, HORIZON + (HORIZON - bottom)))
out.alpha_composite(brand)

# 8. Delikatna winieta i zapis
o = np.asarray(out.convert("RGB"), np.float32)
vig = 1 - 0.35 * np.clip(np.sqrt(((xx - W / 2) / (W / 2)) ** 2 + ((yy - H / 2) / (H / 2)) ** 2) - 0.6, 0, 1)
o *= vig[..., None]
o += nrng.normal(0, 1.2, o.shape)               # lekki szum - bez pasów w gradientach
Image.fromarray(np.clip(o, 0, 255).astype(np.uint8)).save(OUT, quality=92, optimize=True, subsampling=0)
print(OUT)
