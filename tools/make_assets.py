#!/usr/bin/env python3
"""Makes the game's font files and the Arabic wordmark from their open-source originals.

Run it again only to change a weight or the wordmark; its outputs are committed in resources/:
    python3 -m pip install fonttools pillow      (Pillow's wheels include HarfBuzz, needed to join Arabic letters)
    python3 tools/make_assets.py
    python3 tools/make_assets.py icon            (the app's icon alone: see make_icon)

Why a script: Google Fonts serves these fonts as variable fonts (every weight in one file), and the game's text
renderer only draws a variable font's default weight. So each weight the game uses is cut out as an ordinary font.
And neither raylib nor ImGui joins Arabic letters, so the Arabic name is rendered here, joined, as an image.
"""

import io
import pathlib
import sys
import urllib.request

from fontTools.ttLib import TTFont
from fontTools.varLib import instancer
from PIL import Image, ImageDraw, ImageFont

# Google's font repository, pinned to one commit so the files never change under us
FONTS_REPO = "https://raw.githubusercontent.com/google/fonts/23e54b51ddffbc7713c583748e3bd86f62b1fa4a/ofl"
ROOT = pathlib.Path(__file__).resolve().parent.parent
FONTS_OUT = ROOT / "resources" / "fonts"
IMAGES_OUT = ROOT / "resources" / "images"

# (family folder, variable font file, [(weight, output file)])
FONTS = [
    ("figtree", "Figtree[wght].ttf", [(500, "Figtree-Medium.ttf"), (700, "Figtree-Bold.ttf"), (800, "Figtree-ExtraBold.ttf")]),
    ("chivomono", "ChivoMono[wght].ttf", [(400, "ChivoMono-Regular.ttf")]),
]
ARABIC_FONT = ("reemkufi", "ReemKufi[wght].ttf", 700)
WORDMARK = "لحن"                 # lahn: a melody
WORDMARK_HEIGHT = 256            # pixels: drawn smaller in the game, so it stays sharp

# The icon: the Arabic name in the accent, on the dark theme's background (ui/theme.cpp), in a rounded square
ICON_BACKGROUND = (7, 9, 15, 255)
ICON_EDGE = (28, 36, 52, 255)   # the theme's lines: the square's edge, so it shows on a dark taskbar too
ICON_INK = (0, 229, 255, 255)   # the accent
ICON_SIZES = [16, 24, 32, 48, 64, 128, 256]
PACKAGING_OUT = ROOT / "packaging"


def download(family, file):
    url = f"{FONTS_REPO}/{family}/{urllib.request.quote(file)}"
    with urllib.request.urlopen(url) as response:
        return response.read()


def arabic_font(size):
    """Reem Kufi at the wordmark's weight, `size` pixels, shaping with HarfBuzz"""
    family, file, weight = ARABIC_FONT
    arabic = TTFont(io.BytesIO(download(family, file)))
    arabic = instancer.instantiateVariableFont(arabic, {"wght": weight})
    buffer = io.BytesIO()
    arabic.save(buffer)
    return ImageFont.truetype(io.BytesIO(buffer.getvalue()), size, layout_engine=ImageFont.Layout.RAQM)


def make_icon():
    """The app's icon, drawn at 1024 pixels and scaled down: resources/images/lahn-icon.png (the window's, 256),
    packaging/windows/lahn.ico (the program file's, every size Windows asks for) and packaging/icon/lahn-icon-1024.png"""
    size = 1024
    # The letters, alone and joined: their ink, to be placed by it (not by the font's line, which has room for marks)
    font = arabic_font(560)
    letters = Image.new("L", (size * 2, size * 2), 0)
    ImageDraw.Draw(letters).text((size // 2, size // 2), WORDMARK, font=font, fill=255, direction="rtl", language="ar")
    letters = letters.crop(letters.getbbox())
    # A rounded square a little inside the canvas (as icons are drawn), its edge a line's colour
    icon = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    draw = ImageDraw.Draw(icon)
    inset, radius, edge = 40, 200, 12
    draw.rounded_rectangle((inset, inset, size - inset, size - inset), radius, fill=ICON_EDGE)
    draw.rounded_rectangle((inset + edge, inset + edge, size - inset - edge, size - inset - edge), radius - edge, fill=ICON_BACKGROUND)
    # The letters in the middle, a little above it: the eye puts the middle higher than it is
    width = int(size * 0.62)
    height = int(letters.height * width / letters.width)
    letters = letters.resize((width, height), Image.LANCZOS)
    ink = Image.new("RGBA", letters.size, ICON_INK)
    ink.putalpha(letters)
    icon.alpha_composite(ink, ((size - width) // 2, (size - height) // 2 - size // 40))

    (PACKAGING_OUT / "icon").mkdir(parents=True, exist_ok=True)
    (PACKAGING_OUT / "windows").mkdir(parents=True, exist_ok=True)
    icon.save(PACKAGING_OUT / "icon" / "lahn-icon-1024.png")
    icon.resize((256, 256), Image.LANCZOS).save(IMAGES_OUT / "lahn-icon.png")
    icon.save(PACKAGING_OUT / "windows" / "lahn.ico", sizes=[(n, n) for n in ICON_SIZES])
    print("icon lahn-icon.png, lahn.ico, lahn-icon-1024.png")


def main():
    if sys.argv[1:] == ["icon"]:
        make_icon()
        return
    FONTS_OUT.mkdir(parents=True, exist_ok=True)
    IMAGES_OUT.mkdir(parents=True, exist_ok=True)

    for family, file, weights in FONTS + [(ARABIC_FONT[0], ARABIC_FONT[1], [])]:
        # Each font's license travels with it
        (FONTS_OUT / f"{family}-OFL.txt").write_bytes(download(family, "OFL.txt"))
        variable = download(family, file)
        for weight, out in weights:
            font = TTFont(io.BytesIO(variable))
            static = instancer.instantiateVariableFont(font, {"wght": weight})
            static.save(FONTS_OUT / out)
            print("font", out)

    # The Arabic wordmark: shaped (letters joined, right to left) by Pillow's HarfBuzz, white on transparent, so the
    # game can tint it to any color. A generous canvas first, then cropped to the ink.
    font = arabic_font(WORDMARK_HEIGHT)
    image = Image.new("L", (WORDMARK_HEIGHT * 4, WORDMARK_HEIGHT * 2), 0)
    ImageDraw.Draw(image).text((WORDMARK_HEIGHT // 2, WORDMARK_HEIGHT // 3), WORDMARK, font=font, fill=255, direction="rtl", language="ar")
    image = image.crop(image.getbbox())
    white = Image.new("RGBA", image.size, (255, 255, 255, 0))
    white.putalpha(image) # the letters' coverage becomes the alpha: smooth edges, any tint
    white.save(IMAGES_OUT / "lahn-arabic.png")
    print("image lahn-arabic.png", image.size)
    make_icon()


if __name__ == "__main__":
    main()
