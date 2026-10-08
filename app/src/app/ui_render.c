/*
 * When to redraw the display.
 */

#include <string.h>

#include "app/screens.h"
#include "app/ui_render.h"

void ui_render_init(struct ui_render *u)
{
	memset(u, 0, sizeof(*u));
}

bool ui_render_frame(struct ui_render *u, const struct ui_model *ui, int64_t now_ms)
{
	struct gfx_canvas back = {u->back, HAL_DISPLAY_WIDTH, HAL_DISPLAY_HEIGHT, HAL_DISPLAY_STRIDE};
	uint8_t *fb = hal_display_framebuffer();
	bool new_screen;
	bool due;

	if (!u->started || ui->key_presses != u->presses_seen || ui->active_alerts > u->alerts_seen) {
		if (u->started) {
			u->backlight_until_ms = now_ms + UI_RENDER_BACKLIGHT_MS;
		}
		u->presses_seen = ui->key_presses;
	}
	u->alerts_seen = ui->active_alerts;

	screens_draw(ui, &back);
	new_screen = !u->started || ui->screen != u->screen;
	due = new_screen ||
	      (memcmp(u->back, fb, HAL_DISPLAY_FB_SIZE) != 0 &&
	       (ui->screen != UI_SCREEN_STANDBY || now_ms - u->flushed_ms >= UI_RENDER_STANDBY_MS));
	if (!due) {
		return false;
	}
	memcpy(fb, u->back, HAL_DISPLAY_FB_SIZE);
	u->started = true;
	u->screen = (uint8_t)ui->screen;
	u->flushed_ms = now_ms;
	u->flushes++;
	return true;
}

bool ui_render_backlight(const struct ui_render *u, int64_t now_ms)
{
	return now_ms < u->backlight_until_ms;
}
