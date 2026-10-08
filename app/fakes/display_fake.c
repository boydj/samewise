/*
 * display fake.
 */

#include <string.h>

#include <zephyr/kernel.h>

#include "fakes/display_fake.h"
#include "hal/display.h"

static uint8_t fb[HAL_DISPLAY_FB_SIZE];
static uint8_t panel[HAL_DISPLAY_FB_SIZE];
static uint32_t flushes;
static bool backlight;
static uint32_t backlight_ons;
static bool hold;
static volatile bool flushing;
static K_SEM_DEFINE(release, 0, 1);

void display_fake_init(void)
{
	memset(fb, 0, sizeof(fb));
	memset(panel, 0, sizeof(panel));
	flushes = 0;
	backlight = false;
	backlight_ons = 0;
	hold = false;
	flushing = false;
	k_sem_reset(&release);
}

const uint8_t *display_fake_panel(void)
{
	return panel;
}

uint32_t display_fake_flushes(void)
{
	return flushes;
}

bool display_fake_backlight(void)
{
	return backlight;
}

uint32_t display_fake_backlight_ons(void)
{
	return backlight_ons;
}

void display_fake_hold(bool on)
{
	hold = on;
}

void display_fake_release(void)
{
	k_sem_give(&release);
}

bool display_fake_flushing(void)
{
	return flushing;
}

uint8_t *hal_display_framebuffer(void)
{
	return fb;
}

int hal_display_flush(void)
{
	flushing = true;
	if (hold) {
		(void)k_sem_take(&release, K_FOREVER);
	}
	memcpy(panel, fb, sizeof(panel));
	flushes++;
	flushing = false;
	return 0;
}

int hal_display_backlight(bool on)
{
	if (on && !backlight) {
		backlight_ons++;
	}
	backlight = on;
	return 0;
}
