/*
 * hal/display.h on Zephyr's display API: the Sharp LS013B7DH05 through the
 * sharp,ls0xx driver on the board (it also toggles the panel's VCOM), or
 * any other display chosen as zephyr,display (SDL on native_sim).
 *
 * The framebuffer is ours (MSB first, 1 = black); each flush converts the
 * rows that changed since the last one to the panel's format and writes
 * them as whole-width bands, which is all the memory LCD accepts. The
 * backlight is the optional "backlight" GPIO alias.
 */

#include <errno.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/drivers/gpio.h>

#include "hal/display.h"

#define DISPLAY_NODE DT_CHOSEN(zephyr_display)

static const struct device *const dev = DEVICE_DT_GET(DISPLAY_NODE);
#if DT_NODE_EXISTS(DT_ALIAS(backlight))
static const struct gpio_dt_spec backlight = GPIO_DT_SPEC_GET(DT_ALIAS(backlight), gpios);
#endif

static uint8_t fb[HAL_DISPLAY_FB_SIZE];
static uint8_t sent[HAL_DISPLAY_FB_SIZE]; /* what the panel shows */
static uint8_t tx[HAL_DISPLAY_FB_SIZE];
static bool started;

uint8_t *hal_display_framebuffer(void)
{
	return fb;
}

static uint8_t reverse(uint8_t b)
{
	b = (uint8_t)((b & 0xF0U) >> 4 | (b & 0x0FU) << 4);
	b = (uint8_t)((b & 0xCCU) >> 2 | (b & 0x33U) << 2);
	return (uint8_t)((b & 0xAAU) >> 1 | (b & 0x55U) << 1);
}

/* Rows y0..y1-1 of fb into tx, in the panel's bit order and polarity. */
static void convert(const struct display_capabilities *caps, int y0, int y1)
{
	bool invert = caps->current_pixel_format == PIXEL_FORMAT_MONO01; /* 1 = white */
	bool lsb_first = (caps->screen_info & SCREEN_INFO_MONO_MSB_FIRST) == 0U;
	size_t from = (size_t)y0 * HAL_DISPLAY_STRIDE;
	size_t n = (size_t)(y1 - y0) * HAL_DISPLAY_STRIDE;

	for (size_t i = 0; i < n; i++) {
		uint8_t b = fb[from + i];

		b = lsb_first ? reverse(b) : b;
		tx[i] = invert ? (uint8_t)~b : b;
	}
}

static int write_rows(const struct display_capabilities *caps, int y0, int y1)
{
	struct display_buffer_descriptor desc = {
		.buf_size = (uint32_t)((y1 - y0) * (int)HAL_DISPLAY_STRIDE),
		.width = HAL_DISPLAY_WIDTH,
		.height = (uint16_t)(y1 - y0),
		.pitch = HAL_DISPLAY_WIDTH,
	};

	convert(caps, y0, y1);
	return display_write(dev, 0, (uint16_t)y0, &desc, tx);
}

static bool row_changed(int y)
{
	size_t at = (size_t)y * HAL_DISPLAY_STRIDE;

	return memcmp(&fb[at], &sent[at], HAL_DISPLAY_STRIDE) != 0;
}

int hal_display_flush(void)
{
	struct display_capabilities caps;
	int y = 0;
	int err;

	if (!device_is_ready(dev)) {
		return -ENODEV;
	}
	display_get_capabilities(dev, &caps);
	if ((caps.current_pixel_format & (PIXEL_FORMAT_MONO01 | PIXEL_FORMAT_MONO10)) == 0U ||
	    (caps.screen_info & SCREEN_INFO_MONO_VTILED) != 0U ||
	    caps.x_resolution != HAL_DISPLAY_WIDTH || caps.y_resolution != HAL_DISPLAY_HEIGHT) {
		return -ENOTSUP;
	}
	while (y < (int)HAL_DISPLAY_HEIGHT) {
		int end;

		if (started && !row_changed(y)) {
			y++;
			continue;
		}
		end = y + 1;
		while (end < (int)HAL_DISPLAY_HEIGHT && (!started || row_changed(end))) {
			end++;
		}
		err = write_rows(&caps, y, end);
		if (err != 0) {
			return err;
		}
		memcpy(&sent[(size_t)y * HAL_DISPLAY_STRIDE], &fb[(size_t)y * HAL_DISPLAY_STRIDE],
		       (size_t)(end - y) * HAL_DISPLAY_STRIDE);
		y = end;
	}
	if (!started) {
		started = true;
		return display_blanking_off(dev);
	}
	return 0;
}

int hal_display_backlight(bool on)
{
#if DT_NODE_EXISTS(DT_ALIAS(backlight))
	if (!gpio_is_ready_dt(&backlight)) {
		return -ENODEV;
	}
	(void)gpio_pin_configure_dt(&backlight, GPIO_OUTPUT_INACTIVE);
	return gpio_pin_set_dt(&backlight, on ? 1 : 0);
#else
	ARG_UNUSED(on);
	return -ENOTSUP;
#endif
}
