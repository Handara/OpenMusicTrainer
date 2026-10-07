#!/usr/bin/env python3
"""Makes the game's font files from their open-source originals.

Run it again only to change a weight; its outputs are committed in resources/:
    python3 -m pip install fonttools
    python3 tools/make_assets.py

Why a script: Google Fonts serves these fonts as variable fonts (every weight in one file), and the game's text
renderer only draws a variable font's default weight. So each weight the game uses is cut out as an ordinary font.
"""

import io
import pathlib
import urllib.request

from fontTools.ttLib import TTFont
from fontTools.varLib import instancer

# Google's font repository, pinned to one commit so the files never change under us
FONTS_REPO = "https://raw.githubusercontent.com/google/fonts/23e54b51ddffbc7713c583748e3bd86f62b1fa4a/ofl"
ROOT = pathlib.Path(__file__).resolve().parent.parent
FONTS_OUT = ROOT / "resources" / "fonts"

# (family folder, variable font file, [(weight, output file)])
FONTS = [
    ("figtree", "Figtree[wght].ttf", [(500, "Figtree-Medium.ttf"), (700, "Figtree-Bold.ttf"), (800, "Figtree-ExtraBold.ttf")]),
    ("chivomono", "ChivoMono[wght].ttf", [(400, "ChivoMono-Regular.ttf")]),
]


def download(family, file):
    url = f"{FONTS_REPO}/{family}/{urllib.request.quote(file)}"
    with urllib.request.urlopen(url) as response:
        return response.read()


def main():
    FONTS_OUT.mkdir(parents=True, exist_ok=True)

    for family, file, weights in FONTS:
        # Each font's license travels with it
        (FONTS_OUT / f"{family}-OFL.txt").write_bytes(download(family, "OFL.txt"))
        variable = download(family, file)
        for weight, out in weights:
            font = TTFont(io.BytesIO(variable))
            static = instancer.instantiateVariableFont(font, {"wght": weight})
            static.save(FONTS_OUT / out)
            print("font", out)


if __name__ == "__main__":
    main()
