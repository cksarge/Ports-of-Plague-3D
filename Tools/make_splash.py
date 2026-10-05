#!/usr/bin/env python3
"""Makes the splash screen's pictures from Epic's official splash-screen logo.

The source is SourceArt/Splash/UE-logo-2023-SplashScreen-primary-vertical-White.png, downloaded unchanged from
Epic's brand library (brand.epicgames.com, "Splash screen logo"). Its shape is never altered here: every
picture made is that same logo, scaled as a whole.

  splash_logo_white  the official logo as it is, only smaller          (the "official" splash)
  splash_logo_gold   the same shape in the game's gold leaf            (the "custom" splash: see TRADEMARKS.txt,
  splash_logo_hot    the same shape in pale gold, for the gleam         which needs Epic's written approval
  splash_logo_edge   the outline of the same shape, glowing             before it is shown to anyone)

Needs Pillow (pip3 install pillow).
"""
from pathlib import Path
from PIL import Image, ImageChops, ImageFilter

ROOT = Path(__file__).resolve().parent.parent
SOURCE = ROOT / 'SourceArt/Splash/UE-logo-2023-SplashScreen-primary-vertical-White.png'
OUT = ROOT / 'Content/UI/Art'
SIZE = (1186, 1440)  # half the source, the same proportions

logo = Image.open(SOURCE).convert('RGBA').resize(SIZE, Image.LANCZOS)
alpha = logo.getchannel('A')
logo.save(OUT / 'splash_logo_white.png')

def filled(top, bottom):
    """The logo's shape in a colour running from top to bottom."""
    ramp = Image.new('RGB', SIZE)
    px = ramp.load()
    for y in range(SIZE[1]):
        t = y / (SIZE[1] - 1)
        colour = tuple(round(a + (b - a) * t) for a, b in zip(top, bottom))
        for x in range(SIZE[0]):
            px[x, y] = colour
    ramp.putalpha(alpha)
    return ramp

# The game's gold: gold-light at the top to gold, as on its buttons and frames (game.css: --gold-light, --gold).
filled((0xf3, 0xd2, 0x7a), (0xc9, 0x96, 0x24)).save(OUT / 'splash_logo_gold.png')
filled((0xff, 0xf6, 0xd8), (0xff, 0xea, 0xb0)).save(OUT / 'splash_logo_hot.png')

# The outline: where the shape's edge is, as a thin bright line with a soft glow round it.
grown = alpha.filter(ImageFilter.MaxFilter(5))
shrunk = alpha.filter(ImageFilter.MinFilter(5))
line = ImageChops.subtract(grown, shrunk)
glow = line.filter(ImageFilter.GaussianBlur(9)).point(lambda v: min(255, int(v * 0.9)))
edge_alpha = ImageChops.lighter(line, glow)
edge = Image.new('RGB', SIZE, (0xff, 0xdf, 0x8a))
edge.putalpha(edge_alpha)
edge.save(OUT / 'splash_logo_edge.png')
print('splash pictures written to', OUT)
