#!/usr/bin/env python3
"""make_stx_splash.py - the boot / loading picture for the SuperTux CD32X disc, made from
SuperTux's own title pictures (so the repository carries none of them).

    python3 tools/make_stx_splash.py <SuperTux 0.1.3 folder>/data build/Splash.png

320x200: SuperTux 0.1.3's title background (images/title/background.jpg) with its logo
(images/title/logo.png), as its title screen. The same picture shows at power-on and while a
level pack loads (the loader and the platform layer both show IMAGE.RAW).

The progress bar is drawn by the boot path (cd32x/sh2/cdloader_sh2.c splash_bar,
port/plat_mars.c - not changed): x 56-236 of the 320, lines 186-198 of the 224 (174-186 of
this 200-line picture); the logo sits above it. tools/make_boot_image.py turns the picture into
IMAGE.RAW; tools/splash_bar_colours.py then gives the bar SuperTux's colours.
(Hurrican CD32X's tools/make_splash.py, with SuperTux's pictures.)"""
import os
import sys
from PIL import Image

data, out = sys.argv[1], sys.argv[2]
title = os.path.join(data, "images", "title")
CX, BAR_Y0 = 146, 174

img = Image.open(os.path.join(title, "background.jpg")).convert("RGBA").resize((320, 240), Image.LANCZOS)
img = img.crop((0, 20, 320, 220))
logo = Image.open(os.path.join(title, "logo.png")).convert("RGBA")
logo = logo.resize((logo.width * 8 // 10, logo.height * 8 // 10), Image.LANCZOS)
bx0, by0, bx1, by1 = logo.getchannel("A").point(lambda v: 255 if v >= 128 else 0).getbbox()
img.alpha_composite(logo, (CX - (bx0 + bx1) // 2, max(4, (BAR_Y0 - 10 - (by1 - by0)) // 2 - by0)))

# make_boot_image.py cuts away black borders (values <= 8): a frame just above that keeps the
# whole 320x200 in place without a visible line
out_img = img.convert("RGB")
for i in range(320):
    out_img.putpixel((i, 0), (9, 9, 9)); out_img.putpixel((i, 199), (9, 9, 9))
for j in range(200):
    out_img.putpixel((0, j), (9, 9, 9)); out_img.putpixel((319, j), (9, 9, 9))
os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
out_img.save(out)
print("  %s: the splash (SuperTux's title background and logo)" % out)
