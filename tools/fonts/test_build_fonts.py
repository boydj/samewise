"""Tests for tools/fonts/build_fonts.py and the fonts as services/gfx draws them."""

import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

sys.path.insert(0, str(Path(__file__).resolve().parent))
import build_fonts  # noqa: E402

ROOT = build_fonts.ROOT
W, H = 64, 64

HARNESS = r"""
#include <stdio.h>
#include <stdlib.h>
#include "services/gfx/gfx.h"
#include "services/gfx/font_vt323.h"

/* argv: size scale text; writes the W x H canvas as one byte per pixel. */
int main(int argc, char **argv)
{
	static uint8_t buf[(W / 8) * H];
	struct gfx_canvas c = {buf, W, H, W / 8};
	struct gfx_text_style st = {0};
	int size = atoi(argv[1]);

	(void)argc;
	st.font = size == 17 ? &gfx_vt323_17 : &gfx_vt323_25;
	st.scale = (uint8_t)atoi(argv[2]);
	st.color = GFX_BLACK;
	gfx_clear(&c, GFX_WHITE);
	gfx_text(&c, &st, 2, 2, 0, argv[3]);
	for (int y = 0; y < H; y++) {
		for (int x = 0; x < W; x++) {
			putchar(gfx_get(&c, x, y) ? 1 : 0);
		}
	}
	return 0;
}
"""


class BuildFontsTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        src = Path(cls.tmp.name) / "harness.c"
        src.write_text(HARNESS)
        cls.exe = Path(cls.tmp.name) / "harness"
        gfx = ROOT / "app" / "services" / "gfx"
        subprocess.run([os.environ.get("CC", "cc"), "-std=c99", "-Wall", "-Werror", f"-DW={W}", f"-DH={H}",
                        "-I", str(ROOT / "app"), str(src), str(gfx / "gfx.c"), str(gfx / "font_vt323.c"),
                        "-o", str(cls.exe)], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def drawn(self, size, scale, text):
        out = subprocess.run([str(self.exe), str(size), str(scale), text], check=True,
                             capture_output=True).stdout
        return [[out[y * W + x] for x in range(W)] for y in range(H)]

    def reference(self, size, text):
        img = Image.new("1", (W, H), 0)
        d = ImageDraw.Draw(img)
        d.fontmode = "1"
        d.text((2, 2), text, font=ImageFont.truetype(str(build_fonts.TTF), size), fill=1, anchor="la")
        return [[1 if img.getpixel((x, y)) else 0 for x in range(W)] for y in range(H)]

    def test_generated_files_are_current(self):
        c, h = build_fonts.generate()
        self.assertEqual(build_fonts.OUT_C.read_text(), c, "run tools/fonts/build_fonts.py")
        self.assertEqual(build_fonts.OUT_H.read_text(), h, "run tools/fonts/build_fonts.py")

    def test_every_glyph_matches_freetype(self):
        for size in build_fonts.SIZES:
            for code in range(build_fonts.FIRST, build_fonts.LAST + 1):
                ch = chr(code)
                with self.subTest(size=size, ch=ch):
                    self.assertEqual(self.drawn(size, 1, ch), self.reference(size, ch))

    def test_scale_2_doubles_every_pixel(self):
        small = self.drawn(25, 1, "8")
        big = self.drawn(25, 2, "8")
        for y in range(2, 2 + (H - 2) // 2):
            for x in range(2, 2 + (W - 2) // 2):
                for dy in (0, 1):
                    for dx in (0, 1):
                        self.assertEqual(big[2 + 2 * (y - 2) + dy][2 + 2 * (x - 2) + dx], small[y][x], (x, y))

    def test_native_size_is_crisp(self):
        # At 25 px (40 font units per pixel) VT323 lands on its own grid: the
        # stems of 'H' are two whole pixels, so every row is the two stems or
        # the crossbar, nothing resampled in between.
        rows = self.drawn(25, 1, "H")
        widths = {sum(r) for r in rows if any(r)}
        self.assertEqual(widths, {4, 8})

    def test_metrics(self):
        for size in build_fonts.SIZES:
            metrics, glyphs = build_fonts.rasterise(size)
            with self.subTest(size=size):
                self.assertGreater(metrics["cap_height"], 0)
                for ch, g, bits in glyphs:
                    self.assertEqual(len(bits), g["width"] * g["height"], ch)
                    self.assertLessEqual(g["y"] + g["height"], metrics["line_height"] + 1, ch)
                    self.assertGreater(g["advance"], 0, ch)


if __name__ == "__main__":
    unittest.main()
