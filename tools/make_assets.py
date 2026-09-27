#!/usr/bin/env python3
"""Makes the game's font files and the Arabic wordmark from their open-source originals.

Run it again only to change a weight or the wordmark; its outputs are committed in resources/:
    python3 -m pip install fonttools pillow      (Pillow's wheels include HarfBuzz, needed to join Arabic letters)
    python3 tools/make_assets.py

Why a script: Google Fonts serves these fonts as variable fonts (every weight in one file), and the game's text
renderer only draws a variable font's default weight. So each weight the game uses is cut out as an ordinary font.
And neither raylib nor ImGui joins Arabic letters, so the Arabic name is rendered here, joined, as an image.
"""

import io
import pathlib
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


def download(family, file):
    url = f"{FONTS_REPO}/{family}/{urllib.request.quote(file)}"
    with urllib.request.urlopen(url) as response:
        return response.read()


def main():
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
    family, file, weight = ARABIC_FONT
    arabic = TTFont(io.BytesIO(download(family, file)))
    arabic = instancer.instantiateVariableFont(arabic, {"wght": weight})
    buffer = io.BytesIO()
    arabic.save(buffer)
    font = ImageFont.truetype(io.BytesIO(buffer.getvalue()), WORDMARK_HEIGHT, layout_engine=ImageFont.Layout.RAQM)
    image = Image.new("L", (WORDMARK_HEIGHT * 4, WORDMARK_HEIGHT * 2), 0)
    ImageDraw.Draw(image).text((WORDMARK_HEIGHT // 2, WORDMARK_HEIGHT // 3), WORDMARK, font=font, fill=255, direction="rtl", language="ar")
    image = image.crop(image.getbbox())
    white = Image.new("RGBA", image.size, (255, 255, 255, 0))
    white.putalpha(image) # the letters' coverage becomes the alpha: smooth edges, any tint
    white.save(IMAGES_OUT / "lahn-arabic.png")
    print("image lahn-arabic.png", image.size)


if __name__ == "__main__":
    main()
