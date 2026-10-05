/*
 * Scenario tests: the whole radio on native_sim with the fast clock, real
 * SAME audio from tools/vectors (group "scenario"), and scripted signal,
 * battery, keys and headphones.
 */

#include <string.h>
#include <zephyr/ztest.h>

#include "app/alert_log.h"
#include "fakes/alert_out_fake.h"
#include "fakes/audio_out_fake.h"
#include "fakes/battery_fake.h"
#include "fakes/host_file/host_file.h"
#include "fakes/tuner_fake.h"
#include "fakes/watchdog_fake.h"
#include "hal/clock.h"
#include "runner.h"
#include "vectors.h"

#define SECOND 1000LL
#define MINUTE (60 * SECOND)
#define HOUR   (60 * MINUTE)
#define DAY    (24 * HOUR)

static const char *const travis[] = {"048453"};

static const char *wav(const char *name)
{
	const struct test_vector *v = test_vector_find(name);

	zassert_not_null(v, "no vector %s", name);
	return v->wav;
}

static struct radio *r;

static enum alert_state state(void)
{
	return alert_mgr_state(&r->alerts);
}

static void before(void *f)
{
	ARG_UNUSED(f);
	scn_init();
	scn_set_home(travis, 1);
	scn_use_test_events();
	r = scn_radio();
}

ZTEST_SUITE(scenario, NULL, NULL, before, NULL, NULL);

ZTEST(scenario, test_decoded_alert_sounds_and_is_logged)
{
	struct alert_log_entry e;
	const struct scn_event ev[] = {{.at_ms = 10 * SECOND, .kind = SCN_WAV, .path = wav("tor")}};

	scn_run(ev, ARRAY_SIZE(ev), 30 * SECOND);
	zassert_equal(state(), ALERT_STATE_ALERTING);
	zassert_equal(alert_out_fake_count_of(ALERT_OUT_BUZZER, HAL_ALERT_PATTERN_ALERT), 1);
	zassert_equal(alert_out_fake_vibrate(), HAL_ALERT_PATTERN_WARNING);
	zassert_equal(r->ui.screen, UI_SCREEN_ALERT);
	zassert_str_equal(r->ui.event_name, "Tornado Warning");
	zassert_equal(alert_log_count(), 1);
	zassert_ok(alert_log_read(0, &e), "flushed at low priority");
	zassert_str_equal(e.raw, "ZCZC-WXR-TOR-048453+0030-2781915-KEWX/NWS-");
}

ZTEST(scenario, test_rebroadcast_one_alert_one_log_entry)
{
	const struct scn_event ev[] = {
		{.at_ms = 10 * SECOND, .kind = SCN_WAV, .path = wav("tor")},
		{.at_ms = 5 * MINUTE, .kind = SCN_WAV, .path = wav("tor")},
	};

	scn_run(ev, ARRAY_SIZE(ev), 6 * MINUTE);
	zassert_equal(r->alerts.stats.alerted, 1);
	zassert_equal(r->alerts.stats.duplicates, 1);
	zassert_equal(alert_log_count(), 1);
	zassert_equal(alert_out_fake_count_of(ALERT_OUT_BUZZER, HAL_ALERT_PATTERN_ALERT), 1);
}

ZTEST(scenario, test_other_county_and_watch)
{
	const struct scn_event ev[] = {
		{.at_ms = 10 * SECOND, .kind = SCN_WAV, .path = wav("svr_elsewhere")},
		{.at_ms = 1 * MINUTE, .kind = SCN_WAV, .path = wav("toa")},
	};

	scn_run(ev, 1, 50 * SECOND);
	zassert_equal(state(), ALERT_STATE_STANDBY, "Oklahoma county: no alert");
	scn_run(&ev[1], 1, 2 * MINUTE);
	zassert_equal(state(), ALERT_STATE_ALERTING, "a watch alerts under the default preset");
	zassert_equal(alert_out_fake_vibrate(), HAL_ALERT_PATTERN_WATCH);
}

ZTEST(scenario, test_alert_then_headphones_audio_ends_on_nnnn)
{
	const struct scn_event ev[] = {
		{.at_ms = 10 * SECOND, .kind = SCN_WAV, .path = wav("tor_eom")},
		/* Header copies end about 5 s in; plug in during the 1050 Hz tone. */
		{.at_ms = 18 * SECOND, .kind = SCN_HEADPHONES, .a = 1},
	};
	int64_t amp_on;

	/* The runner plays the whole file (header, tone, 20 s of message, NNNN). */
	scn_run(ev, ARRAY_SIZE(ev), 2 * MINUTE);

	amp_on = audio_out_fake_amp_on_ms();
	zassert_true(amp_on >= 18 * SECOND && amp_on < 18 * SECOND + 20,
		     "amp on when the headphones went in (%lld)", amp_on);
	zassert_equal(alert_out_fake_count_of(ALERT_OUT_BUZZER, HAL_ALERT_PATTERN_ALERT), 1);
	zassert_equal(alert_out_fake_buzzer(), HAL_ALERT_PATTERN_OFF, "buzzer stopped");
	zassert_equal(alert_out_fake_count_of(ALERT_OUT_BUZZER, HAL_ALERT_PATTERN_REMINDER), 0);
	zassert_equal(r->decoder.stats.eoms, 1, "NNNN decoded");
	zassert_equal(state(), ALERT_STATE_SILENCED, "NNNN ended the audio");
	zassert_false(audio_out_fake_amp());
	zassert_equal(r->ui.screen, UI_SCREEN_ALERT, "still on screen until purge");
}

ZTEST(scenario, test_headphones_out_during_alert_audio)
{
	const struct scn_event ev[] = {
		{.at_ms = 10 * SECOND, .kind = SCN_WAV, .path = wav("tor")},
		{.at_ms = 30 * SECOND, .kind = SCN_HEADPHONES, .a = 1},
		{.at_ms = 40 * SECOND, .kind = SCN_HEADPHONES, .a = 0},
	};

	scn_run(ev, ARRAY_SIZE(ev), MINUTE);
	zassert_equal(state(), ALERT_STATE_SILENCED);
	zassert_false(audio_out_fake_amp());
}

ZTEST(scenario, test_reminders_then_purge_returns_to_standby)
{
	const struct scn_event ev[] = {{.at_ms = 10 * SECOND, .kind = SCN_WAV, .path = wav("tor")}};

	scn_run(ev, ARRAY_SIZE(ev), 25 * MINUTE);
	zassert_equal(state(), ALERT_STATE_ALERTING);
	zassert_true(alert_out_fake_count_of(ALERT_OUT_BUZZER, HAL_ALERT_PATTERN_REMINDER) >= 3);
	scn_idle_until(45 * MINUTE);
	zassert_equal(state(), ALERT_STATE_STANDBY, "purge time (receive + 30 min, clock unset)");
	zassert_false(alert_out_fake_led());
}

ZTEST(scenario, test_key_lock_during_alert)
{
	const struct scn_event ev[] = {
		{.at_ms = 5 * SECOND, .kind = SCN_LOCK, .a = 1},
		{.at_ms = 6 * SECOND, .kind = SCN_KEY, .a = HAL_INPUT_KEY_BAND},
		{.at_ms = 10 * SECOND, .kind = SCN_WAV, .path = wav("tor")},
		{.at_ms = 30 * SECOND, .kind = SCN_KEY, .a = HAL_INPUT_KEY_TUNE_UP},
	};

	scn_run(ev, 2, 8 * SECOND);
	zassert_equal(state(), ALERT_STATE_STANDBY, "standby can't be turned off while locked");
	scn_run(&ev[2], 2, MINUTE);
	zassert_equal(state(), ALERT_STATE_SILENCED, "only the silence press works");
}

ZTEST(scenario, test_listening_ignores_alerts_then_returns)
{
	const struct scn_event ev[] = {
		{.at_ms = 5 * SECOND, .kind = SCN_KEY, .a = HAL_INPUT_KEY_BAND},
		{.at_ms = 10 * SECOND, .kind = SCN_WAV, .path = wav("tor")},
	};

	scn_run(ev, ARRAY_SIZE(ev), MINUTE);
	zassert_equal(state(), ALERT_STATE_LISTENING);
	zassert_equal(r->ui.screen, UI_SCREEN_LISTENING, "alerts paused");
	zassert_equal(alert_out_fake_count(), 0);
	scn_idle_until(5 * SECOND + 60 * MINUTE + SECOND);
	zassert_equal(state(), ALERT_STATE_STANDBY, "60 minutes without input");
}

ZTEST(scenario, test_decoder_stall_resets_and_next_header_decodes)
{
	const struct scn_event ev[] = {
		{.at_ms = MINUTE, .kind = SCN_DECODER_STALL, .a = 1},
		{.at_ms = MINUTE + 15 * SECOND, .kind = SCN_DECODER_STALL, .a = 0},
	};
	const struct scn_event next[] = {{.at_ms = 2 * MINUTE, .kind = SCN_WAV, .path = wav("tor")}};

	scn_run(ev, ARRAY_SIZE(ev), MINUTE + 16 * SECOND);
	zassert_equal(watchdog_fake_resets(), 1, "watchdog starved");
	zassert_equal(scn_boots(), 2, "simulated reset restarted the application");
	zassert_equal(r->health.boot_reason, HAL_RESET_WATCHDOG);
	zassert_equal(r->ui.screen, UI_SCREEN_RESTARTED);
	zassert_equal(state(), ALERT_STATE_STANDBY, "standby resumes");

	scn_run(next, 1, 3 * MINUTE);
	zassert_equal(state(), ALERT_STATE_ALERTING, "and decodes the next header");
	zassert_equal(watchdog_fake_resets(), 1);
}

ZTEST(scenario, test_tuner_faults_then_persistent_faults)
{
	uint32_t ups = tuner_fake_power_ups();
	const struct scn_event ev[] = {{.at_ms = 30 * SECOND, .kind = SCN_TUNER_FAULTS, .a = 3}};
	const struct scn_event forever[] = {
		{.at_ms = 2 * MINUTE, .kind = SCN_TUNER_FAULTS, .a = UINT32_MAX}};

	scn_run(ev, 1, MINUTE);
	zassert_equal(tuner_fake_power_ups(), ups + 1, "3 faults: tuner reset");
	zassert_equal(watchdog_fake_resets(), 0);

	tuner_fake_fail_power_up(true);
	scn_run(forever, 1, 3 * MINUTE);
	zassert_true(watchdog_fake_resets() >= 1, "persistent faults: watchdog reset");
}

ZTEST(scenario, test_signal_drop_no_signal_hourly_chirps_recovery)
{
	const struct scn_event ev[] = {
		{.at_ms = MINUTE, .kind = SCN_SIGNAL, .a = 8, .b = 2},
		{.at_ms = 2 * HOUR, .kind = SCN_SIGNAL, .a = 40, .b = 25},
	};

	scn_run(ev, 1, 11 * MINUTE + 5 * SECOND);
	zassert_not_equal(r->ui.warnings & UI_WARN_NO_SIGNAL, 0, "NO SIGNAL after 10 minutes");
	zassert_equal(r->ui.screen, UI_SCREEN_WARNING);
	zassert_equal(alert_out_fake_count_of(ALERT_OUT_BUZZER, HAL_ALERT_PATTERN_CHIRP), 1);
	scn_idle_until(2 * HOUR - SECOND);
	zassert_equal(alert_out_fake_count_of(ALERT_OUT_BUZZER, HAL_ALERT_PATTERN_CHIRP), 2,
		      "hourly");
	scn_run(&ev[1], 1, 2 * HOUR + 5 * SECOND);
	zassert_equal(r->ui.warnings & UI_WARN_NO_SIGNAL, 0, "recovery clears it");
	zassert_equal(r->ui.screen, UI_SCREEN_STANDBY);
}

ZTEST(scenario, test_8_days_without_rwt_then_rwt_clears_and_sets_clock)
{
	const struct scn_event rwt[] = {{.at_ms = 8 * DAY + HOUR, .kind = SCN_WAV, .path = wav("rwt")}};

	scn_idle_until(8 * DAY + MINUTE);
	zassert_not_equal(r->ui.warnings & UI_WARN_NO_WEEKLY_TEST, 0);
	zassert_equal(r->ui.screen, UI_SCREEN_WARNING);
	zassert_equal(hal_clock_utc_s(), -1);

	scn_run(rwt, 1, 8 * DAY + HOUR + MINUTE);
	zassert_equal(r->ui.warnings & UI_WARN_NO_WEEKLY_TEST, 0, "the next RWT clears it");
	zassert_true(hal_clock_utc_s() > 0, "and sets the unset clock");
	zassert_equal(state(), ALERT_STATE_STANDBY, "tests never alert");
	zassert_equal(watchdog_fake_resets(), 0);
}

ZTEST(scenario, test_week_of_standby_battery_runs_fast)
{
	static const struct battery_point week[] = {
		{0, 100, 4150},
		{6 * DAY, 20, 3650},
		{6 * DAY + 20 * HOUR, 5, 3500},
		{7 * DAY, 0, 3300},
	};
	long long wall_start = wx_host_monotonic_us();
	long long wall_s;

	battery_fake_script(week, ARRAY_SIZE(week));

	scn_idle_until(6 * DAY - HOUR);
	zassert_equal(r->ui.warnings & (UI_WARN_BATTERY_LOW | UI_WARN_BATTERY_CRITICAL), 0);
	scn_idle_until(6 * DAY + HOUR);
	zassert_not_equal(r->ui.warnings & UI_WARN_BATTERY_LOW, 0, "warning at 20%");
	zassert_equal(alert_out_fake_count_of(ALERT_OUT_BUZZER, HAL_ALERT_PATTERN_CHIRP), 1);
	scn_idle_until(6 * DAY + 21 * HOUR);
	zassert_not_equal(r->ui.warnings & UI_WARN_BATTERY_CRITICAL, 0, "critical at 5%");
	scn_idle_until(7 * DAY + HOUR);
	zassert_true(battery_fake_shipped(), "ship mode at 3.3 V");
	zassert_equal(alert_out_fake_count_of(ALERT_OUT_BUZZER, HAL_ALERT_PATTERN_FINAL_BEEP), 1);
	zassert_equal(r->ui.screen, UI_SCREEN_ALERTS_OFF);
	zassert_true(alert_out_fake_count_of(ALERT_OUT_BUZZER, HAL_ALERT_PATTERN_CHIRP) >= 3,
		     "critical chirps every 30 minutes");

	scn_idle_until(7 * DAY + 2 * HOUR);
	zassert_true(scn_device_off(), "the device stays off");
	zassert_equal(scn_boots(), 1, "no reboot after ship mode");

	wall_s = (wx_host_monotonic_us() - wall_start) / 1000000;
	TC_PRINT("simulated week took %lld s of wall time\n", wall_s);
	zassert_true(wall_s < 60, "a simulated week in under a minute");
}

ZTEST(scenario, test_built_in_event_table)
{
	/*
	 * TOR first: the RWT sets the clock to its issue time (Oct 6), after
	 * which this Oct 5 TOR would already have expired and only be logged.
	 */
	const struct scn_event ev[] = {
		{.at_ms = 10 * SECOND, .kind = SCN_WAV, .path = wav("tor")},
		{.at_ms = 1 * MINUTE, .kind = SCN_WAV, .path = wav("rwt")},
	};

	/* A radio the phone never configured: blank storage, default table. */
	scn_init();
	scn_set_home(travis, 1);
	r = scn_radio();
	zassert_equal(r->settings.events.count, 58);

	scn_run(ev, ARRAY_SIZE(ev), 2 * MINUTE);
	zassert_equal(r->alerts.stats.filtered, 1, "RWT logged, not alerted");
	zassert_equal(state(), ALERT_STATE_ALERTING, "TOR alerts");
	zassert_str_equal(r->ui.event_name, "Tornado Warning");
	zassert_equal(alert_out_fake_vibrate(), HAL_ALERT_PATTERN_WARNING);
}
