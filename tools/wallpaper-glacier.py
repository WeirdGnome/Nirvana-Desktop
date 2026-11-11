#!/usr/bin/env python3
# Nirvana Desktop - generator of the "Glacier" wallpaper: deep blue with soft light
# streaks and a glow, to go with the Glacier color scheme.
#   tools/wallpaper-glacier.py [file] [width height]
import math
import sys

import numpy as np
from PIL import Image, ImageFilter

out = sys.argv[1] if len(sys.argv) > 1 else "wallpapers/Glacier.jpg"
W = int(sys.argv[2]) if len(sys.argv) > 3 else 1920
H = int(sys.argv[3]) if len(sys.argv) > 3 else 1080

y, x = np.mgrid[0:H, 0:W].astype(np.float32)
u, v = x / W, y / H
top = np.array([0x0E, 0x18, 0x30], np.float32)
bottom = np.array([0x23, 0x4B, 0x8C], np.float32)
img = top[None, None, :] * (1 - v[..., None]) + bottom[None, None, :] * v[..., None]

# a glow low on the left
d = np.sqrt(((u - 0.28) * 1.6) ** 2 + (v - 0.78) ** 2)
img += (np.exp(-d * d * 7.0)[..., None] * np.array([70, 110, 170], np.float32))

# soft diagonal light streaks (bands of light through ice)
for k, (pos, width, strength) in enumerate([(0.18, 0.05, 55), (0.36, 0.025, 40), (0.52, 0.08, 30), (0.71, 0.03, 45)]):
    t = u * 0.8 + v * 0.45 - pos - 0.25
    band = np.exp(-(t / width) ** 2) * (0.35 + 0.65 * (1 - v))
    img += band[..., None] * np.array([strength * 0.75, strength * 0.9, strength], np.float32)

# fine sparkles
rng = np.random.default_rng(7)
spark = np.zeros((H, W), np.float32)
for _ in range(int(W * H / 9000)):
    sx, sy = rng.integers(0, W), rng.integers(0, int(H * 0.8))
    spark[sy, sx] = rng.uniform(60, 200)
spark_img = Image.fromarray(np.clip(spark, 0, 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(1.2))
img += np.asarray(spark_img, np.float32)[..., None] * np.array([0.8, 0.9, 1.0], np.float32) * 1.8

# a light dither so the gradients show no bands
img += rng.uniform(-1.5, 1.5, img.shape).astype(np.float32)
Image.fromarray(np.clip(img, 0, 255).astype(np.uint8)).save(out, quality=92)
print(out)
