/*
 * Health supervisor: watchdog policy, tuner faults, no signal, no weekly
 * test and battery levels, on a simulated device that reboots when the
 * watchdog fires.
 */

#include <string.h>
#include <zephyr/ztest.h>

#include "app/alert_log.h"
#include "app/alert_manager.h"
#include "app/clock_sync.h"
#include "app/health.h"
#include "app/settings.h"
#include "app/ui_model.h"
#include "fakes/alert_out_fake.h"
#include "fakes/audio_out_fake.h"
#include "fakes/battery_fake.h"
#include "fakes/clock_fake.h"
#include "fakes/input_fake.h"
#include "fakes/storage_fake.h"
#include "fakes/tuner_fake.h"
#include "fakes/watchdog_fake.h"

#define SECOND 1000LL
#define MINUTE (60 * SECOND)
#define HOUR   (60 * MINUTE)
#define DAY    (24 * HOUR)

static struct {
	struct settings settings;
	struct ui_model ui;
	struct alert_mgr alerts;
	struct health health;
	uint32_t boots;
	bool audio_stalled;
	bool decoder_stalled;
	uint32_t samples;
	int64_t reset_at;
	uint32_t tuner_resets; /* across reboots */
} dev;

static const struct health_config cfg = HEALTH_CONFIG_DEFAULT;

static void on_input(const struct hal_input_event *e, void *user)
{
	alert_mgr_on_input(user, e);
}

static void boot(void)
{
	(void)settings_restore(&dev.settings);
	ui_model_init(&dev.ui);
	alert_log_init();
	clock_sync_init(0);
	alert_mgr_init(&dev.alerts, &dev.settings, &dev.ui);
	(void)hal_input_init(on_input, &dev.alerts);
	health_init(&dev.health, &cfg, &dev.alerts, &dev.ui);
	alert_mgr_set_rwt_hook(&dev.alerts, health_note_rwt, &dev.health);
	(void)hal_tuner_power(true);
	(void)hal_tuner_set_band(HAL_TUNER_BAND_WB);
	(void)hal_tuner_tune(162475);
	dev.boots++;
}

static void on_watchdog_reset(void *user)
{
	ARG_UNUSED(user);
	dev.reset_at = hal_clock_uptime_ms();
	dev.tuner_resets += dev.health.stats.tuner_resets;
	clock_fake_simulate_reset(); /* RAM and RTC lost */
	if (!battery_fake_shipped()) {
		boot();
	}
}

/* Advance time in 100 ms steps with the audio pipeline running (unless stalled). */
static void run(int64_t ms)
{
	for (int64_t done = 0; done < ms; done += 100) {
		if (!dev.audio_stalled) {
			health_note_audio(&dev.health);
		}
		if (!dev.decoder_stalled) {
			dev.samples += 1042;
			health_note_decoder(&dev.health, dev.samples);
		}
		clock_fake_advance_ms(100);
	}
}

static void before(void *f)
{
	ARG_UNUSED(f);
	memset(&dev, 0, sizeof(dev));
	clock_fake_reset();
	storage_fake_init();
	alert_out_fake_init();
	audio_out_fake_init();
	input_fake_init();
	tuner_fake_init();
	battery_fake_init();
	watchdog_fake_init(on_watchdog_reset, NULL);
	boot();
}

ZTEST_SUITE(health, NULL, NULL, before, NULL, NULL);

/* ---- Watchdog policy ---- */

ZTEST(health, test_healthy_radio_feeds_watchdog)
{
	run(HOUR);
	zassert_equal(watchdog_fake_resets(), 0);
	zassert_true(watchdog_fake_running());
	zassert_true(dev.health.stats.feeds >= 3599);
	zassert_equal(dev.health.stats.missed_feeds, 0);
	zassert_equal(dev.ui.screen, UI_SCREEN_STANDBY);
}

ZTEST(health, test_decoder_stall_resets_and_shows_restarted)
{
	run(MINUTE);
	dev.decoder_stalled = true;
	run(12 * SECOND); /* 2 s heartbeat + 8 s watchdog, and a margin */
	zassert_equal(watchdog_fake_resets(), 1, "watchdog starved");
	zassert_equal(dev.boots, 2, "application restarted");
	zassert_equal(dev.health.boot_reason, HAL_RESET_WATCHDOG);
	zassert_equal(health_logged_watchdog_resets(), 1, "reset reason logged");
	zassert_equal(dev.ui.screen, UI_SCREEN_RESTARTED);
	zassert_equal(alert_mgr_state(&dev.alerts), ALERT_STATE_STANDBY, "standby resumes");

	dev.decoder_stalled = false;
	run(HEALTH_RESTARTED_MS + SECOND);
	zassert_equal(dev.ui.screen, UI_SCREEN_STANDBY, "RESTARTED only briefly");
	run(10 * MINUTE);
	zassert_equal(watchdog_fake_resets(), 1, "fed again after the restart");
}

ZTEST(health, test_stall_resets_within_heartbeat_plus_timeout)
{
	int64_t stalled_at;

	run(MINUTE);
	stalled_at = hal_clock_uptime_ms();
	dev.audio_stalled = true;
	run(HEALTH_HEARTBEAT_MS + HAL_WATCHDOG_TIMEOUT_MS + 2 * SECOND);
	zassert_equal(watchdog_fake_resets(), 1);
	/* Last feed no later than one tick after the heartbeat went stale. */
	zassert_true(dev.reset_at <= stalled_at + HEALTH_HEARTBEAT_MS + HEALTH_TICK_MS +
				     HAL_WATCHDOG_TIMEOUT_MS, "reset at %lld", dev.reset_at);
	zassert_true(dev.reset_at >= stalled_at + HAL_WATCHDOG_TIMEOUT_MS);
}

ZTEST(health, test_listening_needs_only_the_tuner)
{
	input_fake_emit(HAL_INPUT_PRESS, HAL_INPUT_KEY_BAND);
	zassert_equal(alert_mgr_state(&dev.alerts), ALERT_STATE_LISTENING);
	dev.audio_stalled = true;
	dev.decoder_stalled = true;
	run(50 * MINUTE);
	zassert_equal(watchdog_fake_resets(), 0, "no weather decoding while listening");
}

/* ---- Tuner ---- */

ZTEST(health, test_three_tuner_faults_reset_the_tuner)
{
	uint32_t ups = tuner_fake_power_ups();

	run(5 * SECOND);
	tuner_fake_fail_status(2, TUNER_FAKE_IO);
	run(5 * SECOND);
	zassert_equal(tuner_fake_power_ups(), ups, "2 faults: no reset");
	tuner_fake_fail_status(3, TUNER_FAKE_INVALID);
	run(5 * SECOND);
	zassert_equal(tuner_fake_power_ups(), ups + 1, "3 consecutive faults: reset");
	zassert_equal(tuner_fake_last_tune_khz(), 162475, "re-tuned to the weather channel");
	zassert_equal(dev.health.stats.tuner_resets, 1);
	run(MINUTE);
	zassert_equal(watchdog_fake_resets(), 0, "the reset fixed it");
	zassert_equal(dev.ui.warnings & UI_WARN_TUNER_FAULT, 0, "fault warning cleared");
}

ZTEST(health, test_persistent_tuner_faults_end_in_watchdog_reset)
{
	run(5 * SECOND);
	tuner_fake_fail_status(UINT32_MAX, TUNER_FAKE_IO);
	tuner_fake_fail_power_up(true);
	run(HEALTH_TUNER_STALE_MS + HAL_WATCHDOG_TIMEOUT_MS + 2 * SECOND);
	zassert_true(dev.tuner_resets >= 1, "a tuner reset was tried first");
	zassert_true(watchdog_fake_resets() >= 1, "then the watchdog reset the radio");
}

/* ---- Signal ---- */

ZTEST(health, test_no_signal_after_10_minutes_and_hourly_chirps)
{
	run(MINUTE);
	tuner_fake_set_signal(10, 3);
	run(10 * MINUTE - 2 * SECOND);
	zassert_equal(dev.ui.warnings & UI_WARN_NO_SIGNAL, 0, "not before 10 minutes");
	run(3 * SECOND);
	zassert_not_equal(dev.ui.warnings & UI_WARN_NO_SIGNAL, 0);
	zassert_equal(dev.ui.screen, UI_SCREEN_WARNING);
	zassert_equal(alert_out_fake_count_of(ALERT_OUT_BUZZER, HAL_ALERT_PATTERN_CHIRP), 1);
	run(3 * HOUR);
	zassert_equal(alert_out_fake_count_of(ALERT_OUT_BUZZER, HAL_ALERT_PATTERN_CHIRP), 4,
		      "chirp every hour");

	tuner_fake_set_signal(40, 25);
	run(5 * SECOND);
	zassert_equal(dev.ui.warnings & UI_WARN_NO_SIGNAL, 0, "recovery clears it");
	zassert_equal(dev.ui.screen, UI_SCREEN_STANDBY);
}

ZTEST(health, test_brief_fade_is_not_no_signal)
{
	tuner_fake_set_signal(10, 3);
	run(9 * MINUTE);
	tuner_fake_set_signal(40, 25);
	run(5 * SECOND);
	tuner_fake_set_signal(10, 3);
	run(9 * MINUTE);
	zassert_equal(dev.ui.warnings & UI_WARN_NO_SIGNAL, 0, "the timer restarts on recovery");
}

ZTEST(health, test_no_signal_not_judged_while_listening)
{
	input_fake_emit(HAL_INPUT_PRESS, HAL_INPUT_KEY_BAND);
	tuner_fake_set_signal(10, 3);
	run(30 * MINUTE);
	zassert_equal(dev.ui.warnings & UI_WARN_NO_SIGNAL, 0);
}

/* ---- Weekly test ---- */

ZTEST(health, test_no_weekly_test_after_8_days_cleared_by_rwt)
{
	struct same_header h;
	static const char rwt[] = "ZCZC-WXR-RWT-048000+0015-2791700-KEWX/NWS-";

	run(8 * DAY - MINUTE);
	zassert_equal(dev.ui.warnings & UI_WARN_NO_WEEKLY_TEST, 0);
	run(2 * MINUTE);
	zassert_not_equal(dev.ui.warnings & UI_WARN_NO_WEEKLY_TEST, 0);
	zassert_equal(dev.ui.screen, UI_SCREEN_WARNING);
	zassert_equal(dev.ui.warnings & UI_WARN_NO_SIGNAL, 0, "its own reason line");

	zassert_ok(same_parse_header(rwt, strlen(rwt), &h));
	alert_mgr_on_header(&dev.alerts, &h);
	zassert_equal(dev.ui.warnings & UI_WARN_NO_WEEKLY_TEST, 0, "the next RWT clears it");
	zassert_equal(dev.ui.screen, UI_SCREEN_STANDBY);
}

/* ---- Battery ---- */

ZTEST(health, test_battery_week_warnings_and_ship_mode)
{
	/* A week of standby: 100% to empty, the cell reaching 3.3 V at 0%. */
	static const struct battery_point week[] = {
		{0, 100, 4150},
		{6 * DAY, 20, 3650},
		{6 * DAY + 20 * HOUR, 5, 3500},
		{7 * DAY, 0, 3300},
	};

	battery_fake_script(week, ARRAY_SIZE(week));
	run(6 * DAY - HOUR);
	zassert_equal(dev.ui.warnings & (UI_WARN_BATTERY_LOW | UI_WARN_BATTERY_CRITICAL), 0);
	run(2 * HOUR);
	zassert_not_equal(dev.ui.warnings & UI_WARN_BATTERY_LOW, 0, "warning at 20%");
	zassert_equal(alert_out_fake_count_of(ALERT_OUT_BUZZER, HAL_ALERT_PATTERN_CHIRP), 1,
		      "one chirp at 20%");

	run(20 * HOUR); /* readings come once a minute: an hour past the 5% point */
	zassert_not_equal(dev.ui.warnings & UI_WARN_BATTERY_CRITICAL, 0, "critical at 5%");
	run(2 * HOUR);
	zassert_true(alert_out_fake_count_of(ALERT_OUT_BUZZER, HAL_ALERT_PATTERN_CHIRP) >= 5,
		     "every 30 minutes when critical");

	run(4 * HOUR);
	zassert_true(battery_fake_shipped(), "ship mode at 3.3 V");
	zassert_equal(alert_out_fake_count_of(ALERT_OUT_BUZZER, HAL_ALERT_PATTERN_FINAL_BEEP), 1);
	zassert_equal(dev.ui.screen, UI_SCREEN_ALERTS_OFF);
}

ZTEST(health, test_charging_clears_battery_warnings)
{
	battery_fake_set(15, 3620, HAL_BATTERY_DISCHARGING);
	run(2 * MINUTE);
	zassert_not_equal(dev.ui.warnings & UI_WARN_BATTERY_LOW, 0);
	battery_fake_set(16, 3900, HAL_BATTERY_CHARGING);
	run(2 * MINUTE);
	zassert_equal(dev.ui.warnings & UI_WARN_BATTERY_LOW, 0);
}

ZTEST(health, test_chirps_never_over_an_alert)
{
	struct same_header h;
	static const char tor[] = "ZCZC-WXR-TOR-048453+0100-2781915-KEWX/NWS-";
	struct event_table t;

	event_table_clear(&t, 1);
	zassert_ok(event_table_add(&t, "TOR", "Tornado Warning", EVENT_CLASS_WARNING));
	zassert_ok(settings_set_event_table(&dev.settings, &t));
	zassert_ok(same_parse_header(tor, strlen(tor), &h));
	alert_mgr_on_header(&dev.alerts, &h);
	zassert_equal(alert_mgr_state(&dev.alerts), ALERT_STATE_ALERTING);

	battery_fake_set(15, 3620, HAL_BATTERY_DISCHARGING);
	run(90 * SECOND);
	zassert_not_equal(dev.ui.warnings & UI_WARN_BATTERY_LOW, 0, "warning still recorded");
	zassert_equal(alert_out_fake_count_of(ALERT_OUT_BUZZER, HAL_ALERT_PATTERN_CHIRP), 0);
	zassert_equal(dev.ui.screen, UI_SCREEN_ALERT, "the alert outranks the warning");
}
