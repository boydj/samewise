/*
 * drivers/display_zephyr.c against a display that records what it is sent:
 * the conversion to the panel's format (the Sharp driver's LSB-first,
 * 1 = white rows, and an MSB-first, 1 = black panel), whole-width bands,
 * and changed rows only.
 */

#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/ztest.h>

#include "hal/display.h"
#include "services/gfx/font_vt323.h"
#include "services/gfx/gfx.h"

/* ---- The recording display ---- */

#define DT_DRV_COMPAT zephyr_wx_recording_display

#define MAX_WRITES 64

struct write_rec {
	uint16_t y;
	uint16_t height;
};

static struct {
	enum display_pixel_format format;
	bool msb_first;
	uint8_t panel[HAL_DISPLAY_FB_SIZE]; /* in the panel's own format */
	struct write_rec writes[MAX_WRITES];
	int n_writes;
	bool rejected;
	bool unblanked;
} rec;

static int rec_write(const struct device *dev, const uint16_t x, const uint16_t y,
		     const struct display_buffer_descriptor *desc, const void *buf)
{
	ARG_UNUSED(dev);
	/* What sharp,ls0xx accepts: x 0, full width, pitch = width. */
	if (x != 0 || desc->width != HAL_DISPLAY_WIDTH || desc->pitch != desc->width ||
	    y + desc->height > HAL_DISPLAY_HEIGHT ||
	    desc->buf_size != desc->height * HAL_DISPLAY_STRIDE) {
		rec.rejected = true;
		return -EINVAL;
	}
	memcpy(&rec.panel[y * HAL_DISPLAY_STRIDE], buf, desc->buf_size);
	if (rec.n_writes < MAX_WRITES) {
		rec.writes[rec.n_writes++] = (struct write_rec){y, desc->height};
	}
	return 0;
}

static void rec_caps(const struct device *dev, struct display_capabilities *caps)
{
	ARG_UNUSED(dev);
	memset(caps, 0, sizeof(*caps));
	caps->x_resolution = HAL_DISPLAY_WIDTH;
	caps->y_resolution = HAL_DISPLAY_HEIGHT;
	caps->supported_pixel_formats = rec.format;
	caps->current_pixel_format = rec.format;
	caps->screen_info = SCREEN_INFO_X_ALIGNMENT_WIDTH |
			    (rec.msb_first ? SCREEN_INFO_MONO_MSB_FIRST : 0U);
}

static int rec_blanking_off(const struct device *dev)
{
	ARG_UNUSED(dev);
	rec.unblanked = true;
	return 0;
}

static DEVICE_API(display, rec_api) = {
	.write = rec_write,
	.get_capabilities = rec_caps,
	.blanking_off = rec_blanking_off,
};

DEVICE_DT_INST_DEFINE(0, NULL, NULL, NULL, NULL, POST_KERNEL, CONFIG_DISPLAY_INIT_PRIORITY,
		      &rec_api);

/* Pixel (x, y) as the panel holds it: black or not. */
static bool panel_black(int x, int y)
{
	uint8_t b = rec.panel[y * HAL_DISPLAY_STRIDE + x / 8];
	uint8_t bit = rec.msb_first ? (uint8_t)(0x80U >> (x % 8)) : (uint8_t)(1U << (x % 8));
	bool set = (b & bit) != 0U;

	return rec.format == PIXEL_FORMAT_MONO01 ? !set : set;
}

/* ---- Tests ---- */

static struct gfx_canvas canvas;

static void before(void *f)
{
	ARG_UNUSED(f);
	rec.n_writes = 0;
	rec.rejected = false;
	canvas = (struct gfx_canvas){hal_display_framebuffer(), HAL_DISPLAY_WIDTH, HAL_DISPLAY_HEIGHT,
				     HAL_DISPLAY_STRIDE};
}

ZTEST_SUITE(display_zephyr, NULL, NULL, before, NULL, NULL);

static void assert_panel_matches(void)
{
	for (int y = 0; y < (int)HAL_DISPLAY_HEIGHT; y++) {
		for (int x = 0; x < (int)HAL_DISPLAY_WIDTH; x++) {
			zassert_equal(panel_black(x, y), gfx_get(&canvas, x, y), "pixel (%d, %d)", x,
				      y);
		}
	}
}

static void draw_something(void)
{
	struct gfx_text_style st = {.font = &gfx_vt323_25, .scale = 2, .color = GFX_BLACK};

	gfx_clear(&canvas, GFX_WHITE);
	gfx_text(&canvas, &st, 3, 20, 0, "1.2");
	gfx_pixel(&canvas, 0, 0, GFX_BLACK);
	gfx_pixel(&canvas, 143, 167, GFX_BLACK);
	gfx_fill(&canvas, 7, 100, 9, 3, GFX_BLACK);
}

/* Runs first (alphabetically): the first flush sends every row. */
ZTEST(display_zephyr, test_1_first_flush_sends_everything_in_the_sharp_format)
{
	rec.format = PIXEL_FORMAT_MONO01; /* sharp,ls0xx: 1 = white, LSB first */
	rec.msb_first = false;
	draw_something();
	zassert_ok(hal_display_flush());
	zassert_false(rec.rejected, "the Sharp driver's constraints are met");
	zassert_true(rec.unblanked, "the panel is turned on after the first frame");
	zassert_equal(rec.n_writes, 1, "one band for the whole screen");
	zassert_equal(rec.writes[0].y, 0);
	zassert_equal(rec.writes[0].height, HAL_DISPLAY_HEIGHT);
	assert_panel_matches();
	zassert_equal(rec.panel[0] & 0x01U, 0U, "pixel 0 black: bit 0 low on an LSB-first, 1 = white panel");
}

ZTEST(display_zephyr, test_2_only_changed_rows_are_sent)
{
	rec.format = PIXEL_FORMAT_MONO01;
	rec.msb_first = false;
	zassert_ok(hal_display_flush());
	zassert_equal(rec.n_writes, 0, "nothing changed, nothing sent");

	gfx_pixel(&canvas, 50, 10, GFX_INVERT);
	gfx_fill(&canvas, 0, 120, 144, 4, GFX_INVERT);
	gfx_pixel(&canvas, 143, 167, GFX_WHITE);
	zassert_ok(hal_display_flush());
	zassert_equal(rec.n_writes, 3, "three bands of changed rows");
	zassert_equal(rec.writes[0].y, 10);
	zassert_equal(rec.writes[0].height, 1);
	zassert_equal(rec.writes[1].y, 120);
	zassert_equal(rec.writes[1].height, 4);
	zassert_equal(rec.writes[2].y, 167);
	zassert_equal(rec.writes[2].height, 1);
	assert_panel_matches();
}

ZTEST(display_zephyr, test_3_msb_first_one_is_black_panel)
{
	/* A panel that takes our own layout: the bytes pass straight through. */
	rec.format = PIXEL_FORMAT_MONO10;
	rec.msb_first = true;
	memset(rec.panel, 0, sizeof(rec.panel));
	gfx_clear(&canvas, GFX_INVERT); /* every row changes */
	zassert_ok(hal_display_flush());
	assert_panel_matches();
	zassert_mem_equal(rec.panel, hal_display_framebuffer(), HAL_DISPLAY_FB_SIZE);
}

ZTEST(display_zephyr, test_4_backlight_without_a_gpio)
{
	zassert_equal(hal_display_backlight(true), -ENOTSUP, "no backlight alias on this target");
}
