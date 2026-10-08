/*
 * When the display redraws and lights, and that the alert path never waits
 * for it: ui_render's policy on scripted models, then radio_ui_tick on a
 * booted radio with a display flush held open in another thread.
 */

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include "app/radio.h"
#include "app/ui_render.h"
#include "fakes/alert_out_fake.h"
#include "fakes/audio_out_fake.h"
#include "fakes/battery_fake.h"
#include "fakes/clock_fake.h"
#include "fakes/display_fake.h"
#include "fakes/input_fake.h"
#include "fakes/storage_fake.h"
#include "fakes/tuner_fake.h"
#include "fakes/watchdog_fake.h"
#include "services/ble/ble_service.h"
#include "services/same/same_header.h"

#define SECOND 1000LL
#define MINUTE (60 * SECOND)

static struct ui_render render;
static struct ui_model ui;

static void reset(void *f)
{
	ARG_UNUSED(f);
	display_fake_init();
	ui_render_init(&render);
	ui_model_init(&ui);
	ui.freq_khz = 162550;
	ui.signal_bars = 3;
	ui.hours_left = 149;
}

ZTEST_SUITE(render, NULL, NULL, reset, NULL, NULL);

ZTEST(render, test_first_frame_draws_then_nothing_until_a_change)
{
	ui_model_refresh(&ui);
	zassert_true(ui_render_frame(&render, &ui, 0), "the first frame always draws");
	zassert_mem_equal(render.back, hal_display_framebuffer(), HAL_DISPLAY_FB_SIZE);
	zassert_false(ui_render_frame(&render, &ui, SECOND), "nothing changed");
	zassert_false(ui_render_frame(&render, &ui, 10 * MINUTE), "still nothing, however long");
}

ZTEST(render, test_standby_redraws_at_most_once_a_minute)
{
	ui_model_refresh(&ui);
	zassert_true(ui_render_frame(&render, &ui, 0));
	ui.hours_left = 148;
	zassert_false(ui_render_frame(&render, &ui, SECOND), "a standby change waits");
	ui.hours_left = 140;
	zassert_false(ui_render_frame(&render, &ui, 59 * SECOND));
	zassert_true(ui_render_frame(&render, &ui, MINUTE), "a minute after the last redraw");
	ui.battery_percent = 50;
	zassert_false(ui_render_frame(&render, &ui, MINUTE + SECOND));
	zassert_true(ui_render_frame(&render, &ui, 2 * MINUTE));
}

ZTEST(render, test_a_new_screen_draws_at_once)
{
	ui_model_refresh(&ui);
	zassert_true(ui_render_frame(&render, &ui, 0));
	ui.alert = true;
	ui.active_alerts = 1;
	memcpy(ui.event, "TOR", 4);
	strcpy(ui.event_name, "Tornado Warning");
	ui_model_refresh(&ui);
	zassert_true(ui_render_frame(&render, &ui, SECOND), "the alert screen, at once");
	ui.alert = false;
	ui.active_alerts = 0;
	ui_model_refresh(&ui);
	zassert_true(ui_render_frame(&render, &ui, 2 * SECOND), "back to standby, at once");
}

ZTEST(render, test_other_screens_redraw_on_any_change)
{
	ui.ble_screen = BLE_SCREEN_CONNECT;
	ui.ble_seconds = 120;
	ui_model_refresh(&ui);
	zassert_true(ui_render_frame(&render, &ui, 0));
	ui.ble_seconds = 119;
	zassert_true(ui_render_frame(&render, &ui, SECOND), "the countdown ticks");
	zassert_false(ui_render_frame(&render, &ui, 2 * SECOND), "but only on a change");
}

ZTEST(render, test_backlight_five_seconds_per_press_and_alert)
{
	ui_model_refresh(&ui);
	(void)ui_render_frame(&render, &ui, 0);
	zassert_false(ui_render_backlight(&render, 0), "not at boot");

	ui.key_presses++;
	(void)ui_render_frame(&render, &ui, 10 * SECOND);
	zassert_true(ui_render_backlight(&render, 10 * SECOND));
	zassert_true(ui_render_backlight(&render, 15 * SECOND - 1));
	zassert_false(ui_render_backlight(&render, 15 * SECOND));

	ui.key_presses++;
	(void)ui_render_frame(&render, &ui, 14 * SECOND);
	zassert_true(ui_render_backlight(&render, 18 * SECOND), "each press restarts the 5 s");

	ui.alert = true;
	ui.active_alerts = 1;
	ui_model_refresh(&ui);
	(void)ui_render_frame(&render, &ui, MINUTE);
	zassert_true(ui_render_backlight(&render, MINUTE), "a new alert lights it");
	(void)ui_render_frame(&render, &ui, MINUTE + 6 * SECOND);
	zassert_false(ui_render_backlight(&render, MINUTE + 6 * SECOND), "the same alert doesn't again");
	ui.active_alerts = 2;
	(void)ui_render_frame(&render, &ui, MINUTE + 7 * SECOND);
	zassert_true(ui_render_backlight(&render, MINUTE + 7 * SECOND), "a second alert does");
}

/* ---- The alert path doesn't wait for the display ---- */

static struct radio radio;
static K_MUTEX_DEFINE(app_lock);

static void lock_cb(void *user)
{
	ARG_UNUSED(user);
	(void)k_mutex_lock(&app_lock, K_FOREVER);
}

static void unlock_cb(void *user)
{
	ARG_UNUSED(user);
	(void)k_mutex_unlock(&app_lock);
}

static const struct radio_lock lk = {.lock = lock_cb, .unlock = unlock_cb};

#define UI_STACK 4096
K_THREAD_STACK_DEFINE(ui_stack, UI_STACK);
static struct k_thread ui_thread;

static void ui_entry(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);
	radio_ui_tick(&radio, &lk);
}

static void on_watchdog_reset(void *user)
{
	ARG_UNUSED(user);
}

ZTEST(render, test_an_alert_sounds_while_a_flush_is_stuck)
{
	static const struct health_config cfg = HEALTH_CONFIG_DEFAULT;
	static const char raw[] = "ZCZC-WXR-TOR-048453+0030-2811500-KEWX/NWS-";
	struct same_header h;

	clock_fake_reset();
	storage_fake_init();
	alert_out_fake_init();
	audio_out_fake_init();
	input_fake_init();
	tuner_fake_init();
	battery_fake_init();
	watchdog_fake_init(on_watchdog_reset, NULL);
	radio_boot(&radio, &cfg, 0);
	zassert_ok(same_parse_header(raw, sizeof(raw) - 1U, &h));

	/* The UI thread starts a flush, and the panel takes forever. */
	display_fake_hold(true);
	k_thread_create(&ui_thread, ui_stack, UI_STACK, ui_entry, NULL, NULL, NULL,
			K_PRIO_PREEMPT(5), 0, K_NO_WAIT);
	for (int i = 0; i < 100 && !display_fake_flushing(); i++) {
		k_msleep(1);
	}
	zassert_true(display_fake_flushing(), "the flush is under way");

	/* Meanwhile the decoder thread takes the lock and a header arrives. */
	zassert_ok(k_mutex_lock(&app_lock, K_NO_WAIT), "the UI thread isn't holding the lock");
	alert_mgr_on_header(&radio.alerts, &h);
	zassert_equal(alert_out_fake_buzzer(), HAL_ALERT_PATTERN_ALERT, "the alert sounds");
	zassert_true(display_fake_flushing(), "while the display is still busy");
	k_mutex_unlock(&app_lock);

	display_fake_release();
	zassert_ok(k_thread_join(&ui_thread, K_SECONDS(1)));
	zassert_equal(display_fake_flushes(), 1);
	display_fake_hold(false);

	/* The next step draws the alert. */
	radio_ui_tick(&radio, &lk);
	zassert_equal(radio.ui.screen, UI_SCREEN_ALERT);
	zassert_equal(display_fake_flushes(), 2);
	zassert_true(display_fake_backlight(), "and lights it");
}
