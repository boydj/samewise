/*
 * display fake: the framebuffer in RAM, a copy of what the last flush sent,
 * and counts. A flush can be held (display_fake_hold) to stand in for a
 * slow SPI transfer, so tests can check that nothing else waits for it.
 */

#ifndef FAKES_DISPLAY_FAKE_H_
#define FAKES_DISPLAY_FAKE_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Blank framebuffer, nothing flushed, backlight off, flushes not held. */
void display_fake_init(void);

/** What the panel shows: the framebuffer as of the last flush. */
const uint8_t *display_fake_panel(void);

uint32_t display_fake_flushes(void);
bool display_fake_backlight(void);
/** Backlight turn-ons since init. */
uint32_t display_fake_backlight_ons(void);

/**
 * Hold every flush until display_fake_release(); a held flush blocks its
 * caller (a thread on native_sim). display_fake_flushing() is true meanwhile.
 */
void display_fake_hold(bool hold);
void display_fake_release(void);
bool display_fake_flushing(void);

#ifdef __cplusplus
}
#endif

#endif /* FAKES_DISPLAY_FAKE_H_ */
