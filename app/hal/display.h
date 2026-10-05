/*
 * Display interface: Sharp LS013B7DH05 memory LCD, 144 x 168 pixels, 1 bit
 * per pixel. The fake is a desktop window at real resolution (Zephyr SDL
 * display driver).
 *
 * The alert path must never call these functions; only the UI thread draws.
 */

#ifndef HAL_DISPLAY_H_
#define HAL_DISPLAY_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HAL_DISPLAY_WIDTH  144U
#define HAL_DISPLAY_HEIGHT 168U
/** Bytes per framebuffer row; pixel x is bit (7 - x % 8) of byte x / 8. 1 = black. */
#define HAL_DISPLAY_STRIDE (HAL_DISPLAY_WIDTH / 8U)
#define HAL_DISPLAY_FB_SIZE (HAL_DISPLAY_STRIDE * HAL_DISPLAY_HEIGHT)

/** The framebuffer to draw into; HAL_DISPLAY_FB_SIZE bytes, owned by the driver. */
uint8_t *hal_display_framebuffer(void);

/**
 * Send changed rows to the panel. May block for the SPI transfer (about
 * 10 ms on the board), so call only from the UI thread.
 */
int hal_display_flush(void);

/** Backlight on or off. */
int hal_display_backlight(bool on);

#ifdef __cplusplus
}
#endif

#endif /* HAL_DISPLAY_H_ */
