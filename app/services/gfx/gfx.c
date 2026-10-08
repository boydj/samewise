/*
 * Drawing into a 1-bit framebuffer.
 */

#include "services/gfx/gfx.h"

#include <string.h>

static void put(struct gfx_canvas *c, int x, int y, enum gfx_color color)
{
	uint8_t *b = &c->buf[(size_t)y * c->stride + (size_t)(x / 8)];
	uint8_t bit = (uint8_t)(0x80U >> (x % 8));

	switch (color) {
	case GFX_BLACK:
		*b |= bit;
		break;
	case GFX_WHITE:
		*b &= (uint8_t)~bit;
		break;
	case GFX_INVERT:
		*b ^= bit;
		break;
	}
}

void gfx_clear(struct gfx_canvas *c, enum gfx_color color)
{
	if (color == GFX_INVERT || (size_t)c->stride * 8U != (size_t)c->width) {
		/* Leave any padding bits and spare stride bytes alone. */
		gfx_fill(c, 0, 0, c->width, c->height, color);
		return;
	}
	memset(c->buf, color == GFX_BLACK ? 0xFF : 0x00, (size_t)c->stride * (size_t)c->height);
}

void gfx_pixel(struct gfx_canvas *c, int x, int y, enum gfx_color color)
{
	if (x >= 0 && y >= 0 && x < c->width && y < c->height) {
		put(c, x, y, color);
	}
}

bool gfx_get(const struct gfx_canvas *c, int x, int y)
{
	if (x < 0 || y < 0 || x >= c->width || y >= c->height) {
		return false;
	}
	return (c->buf[(size_t)y * c->stride + (size_t)(x / 8)] & (0x80U >> (x % 8))) != 0U;
}

void gfx_fill(struct gfx_canvas *c, int x, int y, int w, int h, enum gfx_color color)
{
	int x1 = x + w;
	int y1 = y + h;

	if (x < 0) {
		x = 0;
	}
	if (y < 0) {
		y = 0;
	}
	if (x1 > c->width) {
		x1 = c->width;
	}
	if (y1 > c->height) {
		y1 = c->height;
	}
	for (int row = y; row < y1; row++) {
		for (int col = x; col < x1; col++) {
			put(c, col, row, color);
		}
	}
}

void gfx_hline(struct gfx_canvas *c, int x, int y, int w, enum gfx_color color)
{
	gfx_fill(c, x, y, w, 1, color);
}

void gfx_vline(struct gfx_canvas *c, int x, int y, int h, enum gfx_color color)
{
	gfx_fill(c, x, y, 1, h, color);
}

void gfx_frame(struct gfx_canvas *c, int x, int y, int w, int h, int thickness, enum gfx_color color)
{
	int t = thickness;

	if (t <= 0 || w <= 0 || h <= 0) {
		return;
	}
	if (2 * t >= w || 2 * t >= h) {
		gfx_fill(c, x, y, w, h, color);
		return;
	}
	gfx_fill(c, x, y, w, t, color);
	gfx_fill(c, x, y + h - t, w, t, color);
	gfx_fill(c, x, y + t, t, h - 2 * t, color);
	gfx_fill(c, x + w - t, y + t, t, h - 2 * t, color);
}

/* ---- Text ---- */

static const struct gfx_glyph *glyph(const struct gfx_font *f, char ch)
{
	if (ch < f->first || ch > f->last) {
		ch = (f->first <= '?' && '?' <= f->last) ? '?' : f->first;
	}
	return &f->glyphs[ch - f->first];
}

static int scale_of(const struct gfx_text_style *st)
{
	return st->scale == 0U ? 1 : st->scale;
}

int gfx_line_height(const struct gfx_text_style *st)
{
	return st->font->line_height * scale_of(st);
}

int gfx_text_width_n(const struct gfx_text_style *st, const char *s, size_t n)
{
	int w = 0;
	size_t i;

	for (i = 0; i < n && s[i] != '\0'; i++) {
		w += glyph(st->font, s[i])->advance + (i > 0 ? st->spacing : 0);
	}
	return w * scale_of(st);
}

int gfx_text_width(const struct gfx_text_style *st, const char *s)
{
	return gfx_text_width_n(st, s, strlen(s));
}

size_t gfx_text_fit(const struct gfx_text_style *st, const char *s, int max_width)
{
	size_t n = 0;

	while (s[n] != '\0' && gfx_text_width_n(st, s, n + 1) <= max_width) {
		n++;
	}
	return n;
}

static void draw_glyph(struct gfx_canvas *c, const struct gfx_font *f, const struct gfx_glyph *g,
		       int pen_x, int top, int scale, enum gfx_color color)
{
	const uint8_t *bits = &f->bitmaps[g->offset];

	for (int row = 0; row < g->height; row++) {
		for (int col = 0; col < g->width; col++) {
			unsigned int i = (unsigned int)(row * g->width + col);

			if ((bits[i / 8U] & (0x80U >> (i % 8U))) != 0U) {
				gfx_fill(c, pen_x + (g->x + col) * scale, top + (g->y + row) * scale,
					 scale, scale, color);
			}
		}
	}
}

int gfx_text(struct gfx_canvas *c, const struct gfx_text_style *st, int x, int y, int max_width,
	     const char *s)
{
	int scale = scale_of(st);
	size_t n = max_width > 0 ? gfx_text_fit(st, s, max_width) : strlen(s);
	int w = gfx_text_width_n(st, s, n);
	int pen;

	switch (st->align) {
	case GFX_CENTER:
		pen = x - w / 2;
		break;
	case GFX_RIGHT:
		pen = x - w;
		break;
	case GFX_LEFT:
	default:
		pen = x;
		break;
	}
	for (size_t i = 0; i < n; i++) {
		const struct gfx_glyph *g = glyph(st->font, s[i]);

		draw_glyph(c, st->font, g, pen, y, scale, st->color);
		pen += (g->advance + st->spacing) * scale;
	}
	return w;
}
