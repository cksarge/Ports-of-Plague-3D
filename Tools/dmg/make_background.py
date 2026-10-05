#!/usr/bin/env python3
"""Draws the picture behind the icons in the disk image's window: parchment, the game's name, and an arrow
from where the game's icon sits to where the Applications folder sits. Writes it at two sizes (ordinary and
Retina screens). Usage: make_background.py <output folder>"""
import sys
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont, ImageFilter

ROOT = Path(__file__).resolve().parent.parent.parent
FONTS = ROOT / 'Content/UI/Fonts'
out = Path(sys.argv[1])
W, H = 660, 420           # the window's inside, in points
APP, APPS, ROW = 170, 490, 235  # where the two icons' centres are (the same numbers as in settings.py)

for scale, name in ((1, 'background.png'), (2, 'background@2x.png')):
    im = Image.new('RGB', (W * scale, H * scale), (0xf7, 0xec, 0xd0))
    d = ImageDraw.Draw(im)
    # Parchment: paler in the middle, a brown border with a gold line inside it (the game's frames).
    for y in range(H * scale):
        t = y / (H * scale)
        d.line([(0, y), (W * scale, y)], fill=(round(0xfb - 12 * t), round(0xf3 - 16 * t), round(0xdc - 26 * t)))
    d.rectangle([4 * scale, 4 * scale, W * scale - 4 * scale - 1, H * scale - 4 * scale - 1], outline=(0x4d, 0x3a, 0x22), width=3 * scale)
    d.rectangle([9 * scale, 9 * scale, W * scale - 9 * scale - 1, H * scale - 9 * scale - 1], outline=(0xd9, 0xa8, 0x2b), width=1 * scale)
    title = ImageFont.truetype(str(FONTS / 'UnifrakturMaguntia.ttf'), 54 * scale)
    caps = ImageFont.truetype(str(FONTS / 'Cinzel-SemiBold.ttf'), 15 * scale)
    body = ImageFont.truetype(str(FONTS / 'EBGaramond-Italic.ttf'), 17 * scale)
    def centred(text, font, y, fill):
        w = d.textlength(text, font=font)
        d.text(((W * scale - w) / 2, y * scale), text, font=font, fill=fill)
    centred('Ports of Plague', title, 30, (0x8f, 0x1a, 0x12))
    centred('TO INSTALL, DRAG THE GAME INTO APPLICATIONS', caps, 108, (0x4d, 0x3a, 0x22))
    centred('Then open it from your Applications folder.', body, 352, (0x5c, 0x48, 0x2c))
    # The arrow between the two icons.
    y = ROW * scale
    x0, x1 = (APP + 82) * scale, (APPS - 82) * scale
    red = (0xb0, 0x26, 0x1a)
    d.line([(x0, y), (x1 - 14 * scale, y)], fill=red, width=5 * scale)
    d.polygon([(x1, y), (x1 - 22 * scale, y - 13 * scale), (x1 - 22 * scale, y + 13 * scale)], fill=red)
    im.save(out / name)
