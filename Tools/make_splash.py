#!/usr/bin/env python3
"""Makes the splash screen's picture from Epic's official splash-screen logo.

The source is SourceArt/Splash/UE-logo-2023-SplashScreen-primary-vertical-White.png, downloaded unchanged from
Epic's brand library (brand.epicgames.com, "Splash screen logo"). It is only made smaller, as a whole: its
shape and colour are never altered (see TRADEMARKS.txt).

Needs Pillow (pip3 install pillow).
"""
from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
SOURCE = ROOT / 'SourceArt/Splash/UE-logo-2023-SplashScreen-primary-vertical-White.png'
OUT = ROOT / 'Content/UI/Art/splash_logo_white.png'
SIZE = (1186, 1440)  # half the source, the same proportions

Image.open(SOURCE).convert('RGBA').resize(SIZE, Image.LANCZOS).save(OUT)
print('splash picture written to', OUT)
