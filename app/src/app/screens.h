/*
 * Screens: draws the screen model into the display framebuffer.
 *
 * Pure drawing: the same model always gives the same pixels, so tests
 * compare whole screens with golden images (tests/display/screens). Every
 * string is cut to the width it has, never wrapped off the screen.
 */

#ifndef APP_SCREENS_H_
#define APP_SCREENS_H_

#include "app/ui_model.h"
#include "services/gfx/gfx.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Draw ui->screen into c (HAL_DISPLAY_WIDTH x HAL_DISPLAY_HEIGHT). */
void screens_draw(const struct ui_model *ui, struct gfx_canvas *c);

#ifdef __cplusplus
}
#endif

#endif /* APP_SCREENS_H_ */
