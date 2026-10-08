/*
 * services/gfx: pixel layout, clipping, rectangles and text.
 */

#include <stdlib.h>
#include <string.h>

#include <zephyr/ztest.h>

#include "services/gfx/font_vt323.h"
#include "services/gfx/gfx.h"

/* A 13 x 10 canvas in a 2-byte-wider stride, with guard bytes around it:
 * nothing may touch the padding bits, the spare stride bytes or the guards. */
#define CW     13
#define CH     10
#define STRIDE 4
#define GUARD  8

static uint8_t mem[GUARD + STRIDE * CH + GUARD];
static struct gfx_canvas c;

static void reset(void *f)
{
	ARG_UNUSED(f);
	memset(mem, 0xA5, sizeof(mem));
	c.buf = &mem[GUARD];
	c.width = CW;
	c.height = CH;
	c.stride = STRIDE;
	for (int y = 0; y < CH; y++) {
		c.buf[y * STRIDE + 1] = 0x05; /* padding bits 13-15 */
		c.buf[y * STRIDE + 2] = 0x5A;
		c.buf[y * STRIDE + 3] = 0xC3;
	}
	gfx_clear(&c, GFX_WHITE);
}

static void assert_untouched_outside(void)
{
	for (int i = 0; i < GUARD; i++) {
		zassert_equal(mem[i], 0xA5, "guard before, byte %d", i);
		zassert_equal(mem[GUARD + STRIDE * CH + i], 0xA5, "guard after, byte %d", i);
	}
	for (int y = 0; y < CH; y++) {
		zassert_equal(c.buf[y * STRIDE + 1] & 0x07, 0x05, "padding bits, row %d", y);
		zassert_equal(c.buf[y * STRIDE + 2], 0x5A, "spare stride, row %d", y);
		zassert_equal(c.buf[y * STRIDE + 3], 0xC3, "spare stride, row %d", y);
	}
}

static int count(void)
{
	int n = 0;

	for (int y = 0; y < CH; y++) {
		for (int x = 0; x < CW; x++) {
			n += gfx_get(&c, x, y) ? 1 : 0;
		}
	}
	return n;
}

ZTEST_SUITE(gfx, NULL, NULL, reset, NULL, NULL);

ZTEST(gfx, test_pixel_layout_matches_the_display_hal)
{
	gfx_pixel(&c, 0, 0, GFX_BLACK);
	gfx_pixel(&c, 9, 2, GFX_BLACK);
	zassert_equal(c.buf[0], 0x80, "pixel x is bit 7 - x % 8, 1 is black");
	zassert_equal(c.buf[2 * STRIDE + 1] & 0xF8, 0x40);
	zassert_true(gfx_get(&c, 9, 2));
	gfx_pixel(&c, 9, 2, GFX_WHITE);
	zassert_false(gfx_get(&c, 9, 2));
	gfx_pixel(&c, 9, 2, GFX_INVERT);
	zassert_true(gfx_get(&c, 9, 2));
	assert_untouched_outside();
}

ZTEST(gfx, test_out_of_range_pixels_are_ignored)
{
	gfx_pixel(&c, -1, 0, GFX_BLACK);
	gfx_pixel(&c, 0, -1, GFX_BLACK);
	gfx_pixel(&c, CW, 0, GFX_BLACK);
	gfx_pixel(&c, 0, CH, GFX_BLACK);
	zassert_equal(count(), 0);
	zassert_false(gfx_get(&c, -1, -1));
	assert_untouched_outside();
}

ZTEST(gfx, test_clear)
{
	gfx_clear(&c, GFX_BLACK);
	zassert_equal(count(), CW * CH);
	gfx_clear(&c, GFX_INVERT);
	zassert_equal(count(), 0);
}

ZTEST(gfx, test_fill_clips_at_every_edge)
{
	gfx_fill(&c, -5, 2, 7, 1, GFX_BLACK); /* left: x 0..1 */
	zassert_equal(count(), 2);
	gfx_fill(&c, CW - 2, 4, 10, 1, GFX_BLACK); /* right: 2 pixels */
	zassert_equal(count(), 4);
	gfx_fill(&c, 5, -3, 1, 4, GFX_BLACK); /* top: y 0 */
	zassert_equal(count(), 5);
	gfx_fill(&c, 7, CH - 1, 1, 9, GFX_BLACK); /* bottom: y CH-1 */
	zassert_equal(count(), 6);
	gfx_fill(&c, -100, -100, 1000, 1000, GFX_BLACK);
	zassert_equal(count(), CW * CH, "everything, nothing beyond");
	gfx_fill(&c, 3, 3, 0, 5, GFX_WHITE);
	gfx_fill(&c, 3, 3, -2, 5, GFX_WHITE);
	zassert_equal(count(), CW * CH, "empty rectangles draw nothing");
	assert_untouched_outside();
}

ZTEST(gfx, test_invert_twice_restores)
{
	gfx_fill(&c, 2, 2, 4, 4, GFX_BLACK);
	gfx_fill(&c, 0, 0, 5, 5, GFX_INVERT);
	zassert_false(gfx_get(&c, 3, 3));
	zassert_true(gfx_get(&c, 0, 0));
	zassert_true(gfx_get(&c, 5, 5));
	gfx_fill(&c, 0, 0, 5, 5, GFX_INVERT);
	zassert_equal(count(), 16);
	assert_untouched_outside();
}

ZTEST(gfx, test_lines_and_frame)
{
	gfx_hline(&c, 1, 1, 5, GFX_BLACK);
	gfx_vline(&c, 1, 1, 5, GFX_BLACK);
	zassert_equal(count(), 9);
	gfx_clear(&c, GFX_WHITE);
	gfx_frame(&c, 0, 0, 8, 6, 2, GFX_BLACK);
	zassert_equal(count(), 8 * 6 - 4 * 2, "a 2-pixel border around a 4 x 2 hole");
	zassert_false(gfx_get(&c, 2, 2));
	zassert_true(gfx_get(&c, 1, 4));
	gfx_clear(&c, GFX_WHITE);
	gfx_frame(&c, 0, 0, 4, 4, 2, GFX_BLACK);
	zassert_equal(count(), 16, "too thick for a hole: filled");
	gfx_frame(&c, -2, -2, CW + 4, CH + 4, 3, GFX_BLACK);
	zassert_true(gfx_get(&c, 0, 0));
	zassert_false(gfx_get(&c, 6, 6));
	assert_untouched_outside();
}

/* ---- Text, on the full display size ---- */

static uint8_t fb[18 * 168];
static struct gfx_canvas big = {fb, 144, 168, 18};

static int columns_with_ink(int *first, int *last)
{
	int n = 0;

	*first = -1;
	*last = -1;
	for (int x = 0; x < big.width; x++) {
		for (int y = 0; y < big.height; y++) {
			if (gfx_get(&big, x, y)) {
				if (*first < 0) {
					*first = x;
				}
				*last = x;
				n++;
				break;
			}
		}
	}
	return n;
}

ZTEST(gfx, test_text_width_and_scale)
{
	struct gfx_text_style st = {.font = &gfx_vt323_25, .scale = 1, .color = GFX_BLACK};
	int one = gfx_text_width(&st, "8");

	zassert_equal(one, 10, "VT323 at 25 px advances 10 pixels");
	zassert_equal(gfx_text_width(&st, "88"), 20);
	st.spacing = 3;
	zassert_equal(gfx_text_width(&st, "88"), 23, "spacing between characters only");
	st.scale = 2;
	zassert_equal(gfx_text_width(&st, "88"), 46);
	zassert_equal(gfx_line_height(&st), 2 * gfx_vt323_25.line_height);
	zassert_equal(gfx_text_width(&st, ""), 0);
	st.scale = 0;
	zassert_equal(gfx_text_width(&st, "8"), 10, "scale 0 means 1");
}

static uint8_t ref[18 * 168];
static struct gfx_canvas refc = {ref, 144, 168, 18};

ZTEST(gfx, test_alignment)
{
	struct gfx_text_style st = {.font = &gfx_vt323_25, .scale = 1, .color = GFX_BLACK};
	struct gfx_text_style left = st;
	int w = gfx_text_width(&st, "HH");

	gfx_clear(&big, GFX_WHITE);
	zassert_equal(gfx_text(&big, &st, 10, 3, 0, "HH"), w);
	gfx_clear(&refc, GFX_WHITE);
	gfx_text(&refc, &left, 0, 0, 0, "HH");
	for (int y = 0; y < 30; y++) {
		for (int x = 0; x < 30; x++) {
			zassert_equal(gfx_get(&big, x + 10, y + 3), gfx_get(&refc, x, y), "left: at x, y");
		}
	}

	gfx_clear(&big, GFX_WHITE);
	st.align = GFX_RIGHT;
	gfx_text(&big, &st, 100, 0, 0, "HH");
	gfx_clear(&refc, GFX_WHITE);
	gfx_text(&refc, &left, 100 - w, 0, 0, "HH");
	zassert_mem_equal(fb, ref, sizeof(fb), "right: ends at x");

	gfx_clear(&big, GFX_WHITE);
	st.align = GFX_CENTER;
	gfx_text(&big, &st, 72, 0, 0, "HH");
	gfx_clear(&refc, GFX_WHITE);
	gfx_text(&refc, &left, 72 - w / 2, 0, 0, "HH");
	zassert_mem_equal(fb, ref, sizeof(fb), "centre: half the width each side of x");
}

ZTEST(gfx, test_max_width_cuts_never_wraps)
{
	struct gfx_text_style st = {.font = &gfx_vt323_25, .scale = 1, .color = GFX_BLACK};
	int first;
	int last;

	zassert_equal(gfx_text_fit(&st, "TORNADO WARNING", 45), 4, "four 10-pixel letters fit in 45");
	zassert_equal(gfx_text_fit(&st, "AB", 1000), 2);
	zassert_equal(gfx_text_fit(&st, "AB", 5), 0);
	gfx_clear(&big, GFX_WHITE);
	zassert_equal(gfx_text(&big, &st, 0, 0, 45, "TORNADO WARNING"), 40);
	columns_with_ink(&first, &last);
	zassert_true(last < 45);
	for (int x = 0; x < big.width; x++) {
		for (int y = gfx_vt323_25.line_height; y < big.height; y++) {
			zassert_false(gfx_get(&big, x, y), "nothing below the first line");
		}
	}
}

ZTEST(gfx, test_unknown_characters_draw_as_question_marks)
{
	struct gfx_text_style st = {.font = &gfx_vt323_17, .scale = 1, .color = GFX_BLACK};
	static uint8_t a[18 * 168];
	static uint8_t b[18 * 168];
	struct gfx_canvas ca = {a, 144, 168, 18};
	struct gfx_canvas cb = {b, 144, 168, 18};

	gfx_clear(&ca, GFX_WHITE);
	gfx_clear(&cb, GFX_WHITE);
	gfx_text(&ca, &st, 0, 0, 0, "A\x01\x7F\xE9Z");
	gfx_text(&cb, &st, 0, 0, 0, "A???Z");
	zassert_mem_equal(a, b, sizeof(a));
}

ZTEST(gfx, test_text_in_inverse_and_off_canvas)
{
	struct gfx_text_style st = {.font = &gfx_vt323_25, .scale = 1, .color = GFX_BLACK};
	int ink = 0;
	int white_in_bar = 0;

	gfx_clear(&big, GFX_WHITE);
	gfx_text(&big, &st, 4, 0, 0, "H");
	for (int y = 0; y < 30; y++) {
		for (int x = 0; x < 30; x++) {
			ink += gfx_get(&big, x, y) ? 1 : 0;
		}
	}
	zassert_true(ink > 0);

	gfx_clear(&big, GFX_WHITE);
	gfx_fill(&big, 0, 0, 144, 30, GFX_BLACK);
	st.color = GFX_INVERT;
	gfx_text(&big, &st, 4, 0, 0, "H");
	for (int y = 0; y < 30; y++) {
		for (int x = 0; x < 30; x++) {
			white_in_bar += gfx_get(&big, x, y) ? 0 : 1;
		}
	}
	zassert_equal(white_in_bar, ink, "inverse text: white letters on the black bar");

	/* Partly and wholly off the canvas: clipped, nothing written outside fb. */
	st.color = GFX_BLACK;
	st.scale = 2;
	gfx_text(&big, &st, -15, -20, 0, "162.550");
	gfx_text(&big, &st, 130, 150, 0, "162.550");
	gfx_text(&big, &st, 500, 500, 0, "162.550");
	gfx_text(&big, &st, -500, -500, 0, "162.550");
}
