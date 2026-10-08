/*
 * Drawing into a 1-bit framebuffer: rectangles, lines and bitmap text.
 * Plain C99, no heap, no Zephyr includes.
 *
 * The canvas layout matches hal/display.h: rows of stride bytes, pixel x is
 * bit (7 - x % 8) of byte x / 8, and 1 is black. Everything clips to the
 * canvas, so callers may draw partly or wholly outside it.
 */

#ifndef SERVICES_GFX_GFX_H_
#define SERVICES_GFX_GFX_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct gfx_canvas {
	uint8_t *buf;
	int16_t width;
	int16_t height;
	uint16_t stride; /* bytes per row, at least (width + 7) / 8 */
};

enum gfx_color {
	GFX_WHITE,
	GFX_BLACK,
	GFX_INVERT, /* flip whatever is there */
};

/** One glyph: a width x height bitmap, rows packed MSB first, no row padding. */
struct gfx_glyph {
	uint16_t offset; /* into gfx_font.bitmaps */
	uint8_t width;
	uint8_t height;
	int8_t x;        /* from the pen position to the bitmap's left edge */
	int8_t y;        /* from the line's top to the bitmap's top */
	uint8_t advance; /* pen step to the next glyph */
};

/** A bitmap font covering the characters first..last. */
struct gfx_font {
	uint8_t line_height; /* ascent + descent */
	uint8_t ascent;      /* line top to baseline */
	uint8_t cap_height;  /* baseline to the top of 'H' */
	char first;
	char last;
	const struct gfx_glyph *glyphs; /* last - first + 1 entries */
	const uint8_t *bitmaps;
};

enum gfx_align {
	GFX_LEFT,
	GFX_CENTER, /* x is the centre */
	GFX_RIGHT,  /* x is one past the right edge */
};

struct gfx_text_style {
	const struct gfx_font *font;
	uint8_t scale;   /* 1, or 2 and up to draw each font pixel as scale x scale */
	uint8_t spacing; /* extra pixels between characters, before scaling */
	enum gfx_align align;
	enum gfx_color color;
};

void gfx_clear(struct gfx_canvas *c, enum gfx_color color);
void gfx_pixel(struct gfx_canvas *c, int x, int y, enum gfx_color color);
bool gfx_get(const struct gfx_canvas *c, int x, int y);
void gfx_fill(struct gfx_canvas *c, int x, int y, int w, int h, enum gfx_color color);
/** Outline of a rectangle, thickness pixels wide, inside x, y, w, h. */
void gfx_frame(struct gfx_canvas *c, int x, int y, int w, int h, int thickness, enum gfx_color color);
void gfx_hline(struct gfx_canvas *c, int x, int y, int w, enum gfx_color color);
void gfx_vline(struct gfx_canvas *c, int x, int y, int h, enum gfx_color color);

/** Width in pixels of the first n characters of s (n may exceed strlen). */
int gfx_text_width_n(const struct gfx_text_style *st, const char *s, size_t n);
int gfx_text_width(const struct gfx_text_style *st, const char *s);
/** Height of a line of text in pixels. */
int gfx_line_height(const struct gfx_text_style *st);

/**
 * How many leading characters of s fit in max_width pixels. Characters
 * outside the font are drawn as '?', so the result never splits a line
 * mid-glyph.
 */
size_t gfx_text_fit(const struct gfx_text_style *st, const char *s, int max_width);

/**
 * Draw s with its line top at y, aligned on x. Draws at most the characters
 * that fit in max_width pixels (0 for no limit), so text is cut, never
 * wrapped. Returns the width drawn.
 */
int gfx_text(struct gfx_canvas *c, const struct gfx_text_style *st, int x, int y, int max_width,
	     const char *s);

#ifdef __cplusplus
}
#endif

#endif /* SERVICES_GFX_GFX_H_ */
