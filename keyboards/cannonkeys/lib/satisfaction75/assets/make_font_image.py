#!/usr/bin/env python3
# Copyright 2026 Andrew Kannan
# SPDX-License-Identifier: GPL-2.0-or-later
"""Renders QMK's 6x8 glcdfont as a Quantum Painter font image.

The OLED driver's font is what every screen layout was designed around, so the
Quantum Painter port draws with the same glyphs rather than a rendered TTF.
This writes the printable ASCII range (0x20-0x7E) in the layout
`qmk painter-convert-font-image` expects: one row of glyph-start markers, then
the 8px glyph rows.  Regenerate the font with

    python3 make_font_image.py ../../../../../drivers/oled/glcdfont.c s75_glcdfont.png
    qmk painter-convert-font-image -i s75_glcdfont.png -f mono2
"""

import re
import sys

from PIL import Image

GLYPH_W = 6
GLYPH_H = 8
FIRST, LAST = 0x20, 0x7E


def main(src, out):
    text = open(src).read()
    body = text[text.index('{') + 1:text.index('};')]
    data = [int(v, 16) for v in re.findall(r'0x([0-9A-Fa-f]{2})', body)]

    count = LAST - FIRST + 1
    img = Image.new('RGB', (count * GLYPH_W, GLYPH_H + 1), (0, 0, 0))
    px = img.load()
    for n in range(count):
        px[n * GLYPH_W, 0] = (255, 255, 255)  # glyph start marker
        base = (FIRST + n) * GLYPH_W
        for col in range(GLYPH_W):
            byte = data[base + col]
            for row in range(GLYPH_H):
                if byte & (1 << row):
                    px[n * GLYPH_W + col, row + 1] = (255, 255, 255)
    img.save(out)


if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2])
