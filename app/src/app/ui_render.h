/*
 * When to redraw the display and light the backlight (spec: Power modes).
 *
 * Each UI step draws the screen model into a back buffer and copies it to
 * the display's framebuffer only when the panel should change:
 *   - at once when the screen changes (an alert, a warning, a window);
 *   - at once when anything on a screen other than standby changes;
 *   - in standby, at most once every UI_RENDER_STANDBY_MS, so a flickering
 *     signal bar or battery figure doesn't keep redrawing the panel.
 * The backlight runs UI_RENDER_BACKLIGHT_MS after each key press and each
 * new alert.
 */

#ifndef APP_UI_RENDER_H_
#define APP_UI_RENDER_H_

#include <stdbool.h>
#include <stdint.h>

#include "app/ui_model.h"
#include "hal/display.h"

#ifdef __cplusplus
extern "C" {
#endif

#define UI_RENDER_STANDBY_MS   60000
#define UI_RENDER_BACKLIGHT_MS 5000

struct ui_render {
	uint8_t back[HAL_DISPLAY_FB_SIZE];
	bool started;
	uint8_t screen;        /* enum ui_screen last flushed */
	int64_t flushed_ms;    /* when */
	uint32_t presses_seen; /* ui_model.key_presses */
	uint8_t alerts_seen;   /* ui_model.active_alerts */
	int64_t backlight_until_ms;
	uint32_t flushes;
};

void ui_render_init(struct ui_render *u);

/**
 * Draw ui into the back buffer; if the panel should change now, copy it to
 * hal_display_framebuffer() and return true (the caller then flushes).
 */
bool ui_render_frame(struct ui_render *u, const struct ui_model *ui, int64_t now_ms);

/** Whether the backlight should be on at now_ms (after ui_render_frame). */
bool ui_render_backlight(const struct ui_render *u, int64_t now_ms);

#ifdef __cplusplus
}
#endif

#endif /* APP_UI_RENDER_H_ */
