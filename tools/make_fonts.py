#!/usr/bin/env python3
"""Build the TTF fonts stored in the LittleFS image (assets/fonts).

The device renders text with LVGL Tiny TTF (stb_truetype), which needs static
TrueType fonts. This script downloads the sources, pins the variable Noto Sans KR
to Regular (wght 400), and subsets both fonts to what the UI needs:

  NotoSansKR-subset.ttf  Latin-1, punctuation, Hangul Jamo, all 11,172 modern
                         Hangul syllables (song titles can contain any syllable)
  weather-icons.ttf      the Weather Icons glyphs used for WMO weather codes

Both fonts are licensed under the SIL Open Font License 1.1 (copied next to them).

Usage:
    pip install fonttools
    python tools/make_fonts.py
"""

import io
import sys
import urllib.request
from pathlib import Path

from fontTools import subset
from fontTools.ttLib import TTFont
from fontTools.varLib import instancer

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "assets" / "fonts"

NOTO_URL = "https://github.com/google/fonts/raw/main/ofl/notosanskr/NotoSansKR%5Bwght%5D.ttf"
NOTO_OFL = "https://github.com/google/fonts/raw/main/ofl/notosanskr/OFL.txt"
WI_URL = "https://github.com/erikflowers/weather-icons/raw/master/font/weathericons-regular-webfont.ttf"
WI_HEADER = (
    "Weather Icons font by Erik Flowers - https://github.com/erikflowers/weather-icons\n"
    "The font is licensed under the SIL Open Font License, Version 1.1 (stated in the\n"
    "project README; the repository ships no separate license file). Full text below.\n\n"
)

# Must match components/ui/pages/page_weather.c
WEATHER_GLYPHS = [0xF00D, 0xF02E, 0xF002, 0xF086, 0xF013, 0xF014,
                  0xF01C, 0xF019, 0xF01B, 0xF01A, 0xF01E, 0xF0B5]

KOREAN_RANGES = [
    (0x0020, 0x007E),   # ASCII
    (0x00A0, 0x00FF),   # Latin-1 (°, ·, é ...)
    (0x2010, 0x2027),   # dashes, quotes, bullet, ellipsis
    (0x2030, 0x203A),   # per mille, angle quotes
    (0x20A9, 0x20A9),   # won sign
    (0x2190, 0x2193),   # arrows
    (0x3000, 0x303F),   # CJK punctuation
    (0x3131, 0x318E),   # Hangul compatibility Jamo
    (0xAC00, 0xD7A3),   # Hangul syllables
]


def fetch(url: str) -> bytes:
    print(f"download {url}")
    with urllib.request.urlopen(url) as r:
        return r.read()


def subset_font(font: TTFont, unicodes: list[int], out: Path) -> None:
    opts = subset.Options()
    opts.layout_features = []          # stb_truetype uses only 'kern', not GSUB/GPOS
    opts.hinting = False
    opts.name_IDs = [0, 1, 2, 3, 4, 5, 6]
    opts.notdef_outline = True
    opts.glyph_names = False
    s = subset.Subsetter(opts)
    s.populate(unicodes=unicodes)
    s.subset(font)
    font.save(out)
    print(f"wrote {out.relative_to(ROOT)} ({out.stat().st_size / 1024:.0f} KB)")


def main() -> int:
    OUT.mkdir(parents=True, exist_ok=True)

    noto = TTFont(io.BytesIO(fetch(NOTO_URL)))
    if "fvar" in noto:
        noto = instancer.instantiateVariableFont(noto, {"wght": 400})
    chars = [c for lo, hi in KOREAN_RANGES for c in range(lo, hi + 1)]
    subset_font(noto, chars, OUT / "NotoSansKR-subset.ttf")
    ofl = fetch(NOTO_OFL).decode("utf-8")
    (OUT / "OFL-NotoSansKR.txt").write_text(ofl, encoding="utf-8")

    wi = TTFont(io.BytesIO(fetch(WI_URL)))
    subset_font(wi, WEATHER_GLYPHS, OUT / "weather-icons.ttf")
    # The OFL body is identical; reuse Noto's OFL.txt after its copyright line
    body = ofl[ofl.index("This Font Software is licensed"):]
    (OUT / "OFL-WeatherIcons.txt").write_text(WI_HEADER + body, encoding="utf-8")
    return 0


if __name__ == "__main__":
    sys.exit(main())
