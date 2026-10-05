/*
 * Alert manager: alert states and behaviour, matching and filtering in
 * context, duplicates, the alert log and RWT clock correction.
 */

#include <string.h>
#include <zephyr/ztest.h>

#include "app/alert_log.h"
#include "app/alert_manager.h"
#include "app/clock_sync.h"
#include "app/settings.h"
#include "app/ui_model.h"
#include "fakes/alert_out_fake.h"
#include "fakes/audio_out_fake.h"
#include "fakes/clock_fake.h"
#include "fakes/input_fake.h"
#include "fakes/storage_fake.h"
#include "services/match/same_time.h"

#define MINUTE (60 * 1000)
/* 2026-10-01T00:00:00Z: firmware epoch for the year floor. */
#define EPOCH 1790812800LL

static struct settings s;
static struct ui_model ui;
static struct alert_mgr m;
static struct same_header h;
static int rwt_hook_calls;

static int64_t utc(int64_t y, unsigned int mo, unsigned int d, int hh, int mm)
{
	return same_days_from_civil(y, mo, d) * 86400 + hh * 3600 + mm * 60;
}

static void header(const char *text)
{
	zassert_ok(same_parse_header(text, strlen(text), &h), "%s", text);
	alert_mgr_on_header(&m, &h);
}

static void key(uint32_t keys)
{
	input_fake_emit(HAL_INPUT_PRESS, keys);
}

static void on_input(const struct hal_input_event *e, void *user)
{
	alert_mgr_on_input(user, e);
}

static void on_rwt(void *user)
{
	ARG_UNUSED(user);
	rwt_hook_calls++;
}

static enum alert_state state(void)
{
	return alert_mgr_state(&m);
}

static void before(void *f)
{
	struct same_location travis = {0, 48, 453};
	struct event_table t;

	ARG_UNUSED(f);
	clock_fake_reset();
	storage_fake_init();
	alert_out_fake_init();
	audio_out_fake_init();
	input_fake_init();
	alert_log_init();
	clock_sync_init(EPOCH);

	zassert_equal(settings_restore(&s), 0);
	event_table_clear(&t, 1);
	zassert_ok(event_table_add(&t, "TOR", "Tornado Warning", EVENT_CLASS_WARNING));
	zassert_ok(event_table_add(&t, "SVR", "Severe Thunderstorm Warning", EVENT_CLASS_WARNING));
	zassert_ok(event_table_add(&t, "TOA", "Tornado Watch", EVENT_CLASS_WATCH));
	zassert_ok(event_table_add(&t, "SPS", "Special Weather Statement", EVENT_CLASS_STATEMENT));
	zassert_ok(event_table_add(&t, "RWT", "Required Weekly Test", EVENT_CLASS_TEST));
	zassert_ok(event_table_add(&t, "RMT", "Required Monthly Test", EVENT_CLASS_TEST));
	zassert_ok(settings_set_event_table(&s, &t));
	zassert_ok(settings_set_counties(&s, SETTINGS_MODE_HOME, &travis, 1));

	ui_model_init(&ui);
	alert_mgr_init(&m, &s, &ui);
	alert_mgr_set_rwt_hook(&m, on_rwt, NULL);
	zassert_ok(hal_input_init(on_input, &m));
	rwt_hook_calls = 0;
}

ZTEST_SUITE(alert_manager, NULL, NULL, before, NULL, NULL);

#define TOR_TRAVIS "ZCZC-WXR-TOR-048453+0030-2781915-KEWX/NWS-"
#define TOA_TRAVIS "ZCZC-WXR-TOA-048453+0600-2781915-KEWX/NWS-"
#define TOR_LONG   "ZCZC-WXR-TOR-048453+0600-2781915-KEWX/NWS-"

/* ---- Alert behaviour ---- */

ZTEST(alert_manager, test_buzzer_two_minutes_then_reminders_until_key)
{
	header(TOR_LONG);
	zassert_equal(state(), ALERT_STATE_ALERTING);
	zassert_equal(alert_out_fake_buzzer(), HAL_ALERT_PATTERN_ALERT);
	zassert_true(alert_out_fake_led());
	zassert_equal(ui.screen, UI_SCREEN_ALERT);
	zassert_str_equal(ui.event_name, "Tornado Warning");

	clock_fake_advance_ms(2 * MINUTE - 1);
	zassert_equal(alert_out_fake_buzzer(), HAL_ALERT_PATTERN_ALERT, "still sounding");
	clock_fake_advance_ms(1);
	zassert_equal(alert_out_fake_buzzer(), HAL_ALERT_PATTERN_OFF, "2 minutes, then quiet");
	zassert_equal(alert_out_fake_vibrate(), HAL_ALERT_PATTERN_OFF);

	/* Reminders every 5 minutes after the continuous period. */
	clock_fake_advance_ms(5 * MINUTE);
	zassert_equal(alert_out_fake_count_of(ALERT_OUT_BUZZER, HAL_ALERT_PATTERN_REMINDER), 1);
	clock_fake_advance_ms(15 * MINUTE);
	zassert_equal(alert_out_fake_count_of(ALERT_OUT_BUZZER, HAL_ALERT_PATTERN_REMINDER), 4);

	key(HAL_INPUT_KEY_VOL_UP);
	zassert_equal(state(), ALERT_STATE_SILENCED);
	zassert_true(alert_out_fake_led(), "LED stays on while silenced");
	zassert_equal(ui.screen, UI_SCREEN_ALERT, "alert stays on screen");
	zassert_true(ui.alert_silenced);
	clock_fake_advance_ms(60 * MINUTE);
	zassert_equal(alert_out_fake_count_of(ALERT_OUT_BUZZER, HAL_ALERT_PATTERN_REMINDER), 4,
		      "no reminders after the key");
}

ZTEST(alert_manager, test_unanswered_reminders_end_at_purge_time)
{
	header(TOR_TRAVIS); /* clock unset: expires 30 minutes after receipt */
	clock_fake_advance_ms(30 * MINUTE - 1);
	zassert_equal(state(), ALERT_STATE_ALERTING);
	clock_fake_advance_ms(1);
	zassert_equal(state(), ALERT_STATE_STANDBY);
	zassert_false(alert_out_fake_led());
	zassert_equal(alert_out_fake_buzzer(), HAL_ALERT_PATTERN_OFF);
	zassert_equal(ui.screen, UI_SCREEN_STANDBY);
	zassert_equal(alert_out_fake_count_of(ALERT_OUT_BUZZER, HAL_ALERT_PATTERN_REMINDER), 5,
		      "at 7, 12, 17, 22 and 27 minutes");
}

ZTEST(alert_manager, test_silenced_until_purge)
{
	header(TOR_TRAVIS);
	key(HAL_INPUT_KEY_STBY);
	zassert_equal(state(), ALERT_STATE_SILENCED);
	key(HAL_INPUT_KEY_BAND);
	zassert_equal(state(), ALERT_STATE_SILENCED, "tune and band don't leave Silenced");
	clock_fake_advance_ms(30 * MINUTE);
	zassert_equal(state(), ALERT_STATE_STANDBY);
	zassert_false(alert_out_fake_led());
}

ZTEST(alert_manager, test_warning_and_watch_vibrate_differently)
{
	header(TOR_TRAVIS);
	zassert_equal(alert_out_fake_vibrate(), HAL_ALERT_PATTERN_WARNING);
	key(HAL_INPUT_KEY_STBY);
	header(TOA_TRAVIS);
	zassert_equal(alert_out_fake_vibrate(), HAL_ALERT_PATTERN_WATCH);
	zassert_not_equal(HAL_ALERT_PATTERN_WARNING, HAL_ALERT_PATTERN_WATCH);
}

ZTEST(alert_manager, test_headphones_in_then_eom)
{
	header(TOR_LONG);
	input_fake_emit(HAL_INPUT_HEADPHONES_IN, 0);
	zassert_equal(state(), ALERT_STATE_ALERT_AUDIO);
	zassert_equal(alert_out_fake_buzzer(), HAL_ALERT_PATTERN_OFF, "buzzer off");
	zassert_true(audio_out_fake_amp(), "amp on");
	zassert_equal(audio_out_fake_route(), HAL_AUDIO_OUT_ROUTE_ALERT, "broadcast plays");
	zassert_true(ui.alert_audio);

	clock_fake_advance_ms(10 * MINUTE);
	zassert_equal(alert_out_fake_count_of(ALERT_OUT_BUZZER, HAL_ALERT_PATTERN_REMINDER), 0,
		      "no reminders while listening in headphones");

	alert_mgr_on_eom(&m);
	zassert_equal(state(), ALERT_STATE_SILENCED, "NNNN ends the audio");
	zassert_false(audio_out_fake_amp());
	zassert_equal(audio_out_fake_route(), HAL_AUDIO_OUT_ROUTE_NONE);
}

ZTEST(alert_manager, test_headphones_out_during_alert_audio)
{
	header(TOR_LONG);
	input_fake_emit(HAL_INPUT_HEADPHONES_IN, 0);
	input_fake_emit(HAL_INPUT_HEADPHONES_OUT, 0);
	zassert_equal(state(), ALERT_STATE_SILENCED);
	zassert_false(audio_out_fake_amp());
	zassert_equal(alert_out_fake_buzzer(), HAL_ALERT_PATTERN_OFF, "no buzzer comes back");
}

ZTEST(alert_manager, test_alert_with_headphones_already_in)
{
	input_fake_emit(HAL_INPUT_HEADPHONES_IN, 0);
	header(TOR_LONG);
	zassert_equal(state(), ALERT_STATE_ALERT_AUDIO);
	zassert_equal(alert_out_fake_count_of(ALERT_OUT_BUZZER, HAL_ALERT_PATTERN_ALERT), 0);
	zassert_true(audio_out_fake_amp());
}

ZTEST(alert_manager, test_eom_outside_alert_audio_changes_nothing)
{
	header(TOR_LONG);
	alert_mgr_on_eom(&m);
	zassert_equal(state(), ALERT_STATE_ALERTING);
}

ZTEST(alert_manager, test_key_lock_during_alert)
{
	input_fake_emit(HAL_INPUT_LOCK_ON, 0);
	key(HAL_INPUT_KEY_BAND);
	zassert_equal(state(), ALERT_STATE_STANDBY, "standby can't be turned off while locked");
	key(HAL_INPUT_KEY_TUNE_UP);
	zassert_equal(state(), ALERT_STATE_STANDBY);
	zassert_true(ui.locked);

	header(TOR_LONG);
	key(HAL_INPUT_KEY_BAND);
	zassert_equal(state(), ALERT_STATE_SILENCED, "the silence press still works");
	key(HAL_INPUT_KEY_BAND);
	zassert_equal(state(), ALERT_STATE_SILENCED, "and nothing else does");
}

ZTEST(alert_manager, test_listening_pauses_alerts_and_times_out)
{
	key(HAL_INPUT_KEY_BAND);
	zassert_equal(state(), ALERT_STATE_LISTENING);
	zassert_equal(ui.screen, UI_SCREEN_LISTENING, "alerts paused");

	header(TOR_LONG);
	zassert_equal(state(), ALERT_STATE_LISTENING, "alerts ignored");
	zassert_equal(alert_out_fake_count(), 0);
	zassert_equal(m.stats.ignored_listening, 1);

	clock_fake_advance_ms(30 * MINUTE);
	key(HAL_INPUT_KEY_TUNE_UP); /* input restarts the 60 minutes */
	clock_fake_advance_ms(60 * MINUTE - 1);
	zassert_equal(state(), ALERT_STATE_LISTENING);
	clock_fake_advance_ms(1);
	zassert_equal(state(), ALERT_STATE_STANDBY, "60 minutes without input");
	zassert_equal(ui.screen, UI_SCREEN_STANDBY);
}

ZTEST(alert_manager, test_stby_returns_from_listening)
{
	key(HAL_INPUT_KEY_TUNE_DOWN);
	zassert_equal(state(), ALERT_STATE_LISTENING);
	key(HAL_INPUT_KEY_STBY);
	zassert_equal(state(), ALERT_STATE_STANDBY);
}

ZTEST(alert_manager, test_new_alert_re_alerts_and_newest_is_shown)
{
	header(TOR_LONG);
	key(HAL_INPUT_KEY_STBY);
	zassert_equal(state(), ALERT_STATE_SILENCED);
	clock_fake_advance_ms(10 * MINUTE);

	header("ZCZC-WXR-SVR-048453+0030-2781925-KEWX/NWS-");
	zassert_equal(state(), ALERT_STATE_ALERTING, "re-alerts");
	zassert_equal(alert_out_fake_count_of(ALERT_OUT_BUZZER, HAL_ALERT_PATTERN_ALERT), 2);
	zassert_str_equal(ui.event, "SVR", "newest on screen");
	zassert_equal(ui.active_alerts, 2);

	/* SVR expires first; the TOR is still active, so no return to standby. */
	clock_fake_advance_ms(30 * MINUTE);
	zassert_equal(state(), ALERT_STATE_ALERTING);
	zassert_str_equal(ui.event, "TOR");
	key(HAL_INPUT_KEY_STBY);
	clock_fake_advance_ms(6 * 60 * MINUTE);
	zassert_equal(state(), ALERT_STATE_STANDBY, "after every alert has expired");
}

/* ---- Matching and filtering in context ---- */

ZTEST(alert_manager, test_other_county_does_not_alert)
{
	header("ZCZC-WXR-TOR-048029+0030-2781915-KEWX/NWS-");
	zassert_equal(state(), ALERT_STATE_STANDBY);
	zassert_equal(m.stats.not_matched, 1);
	zassert_equal(alert_log_count(), 0, "not logged");
}

ZTEST(alert_manager, test_travel_mode_uses_only_travel_list)
{
	struct same_location bexar = {0, 48, 29};

	zassert_ok(settings_set_counties(&s, SETTINGS_MODE_TRAVEL, &bexar, 1));
	zassert_ok(settings_set_mode(&s, SETTINGS_MODE_TRAVEL));
	header(TOR_TRAVIS);
	zassert_equal(state(), ALERT_STATE_STANDBY, "home county ignored in travel mode");
	header("ZCZC-WXR-TOR-048029+0030-2781915-KEWX/NWS-");
	zassert_equal(state(), ALERT_STATE_ALERTING);
}

ZTEST(alert_manager, test_empty_travel_list_accepts_everything)
{
	zassert_ok(settings_set_mode(&s, SETTINGS_MODE_TRAVEL));
	header("ZCZC-WXR-TOR-012086+0030-2781915-KMFL/NWS-");
	zassert_equal(state(), ALERT_STATE_ALERTING);
	zassert_equal(ui.matched_count, 1);
}

ZTEST(alert_manager, test_matched_counties_on_screen)
{
	header("ZCZC-WXR-TOR-048029-148453-048491+0030-2781915-KEWX/NWS-");
	zassert_equal(ui.matched_count, 1);
	zassert_equal(ui.matched[0].subdivision, 1);
	zassert_equal(ui.matched[0].county, 453);
}

ZTEST(alert_manager, test_filter_presets_in_context)
{
	header(TOA_TRAVIS);
	zassert_equal(state(), ALERT_STATE_ALERTING, "default preset alerts on watches");
	key(HAL_INPUT_KEY_STBY);

	zassert_ok(settings_set_filter(&s, FILTER_WARNINGS, NULL));
	header("ZCZC-WXR-TOA-048453+0600-2781930-KEWX/NWS-");
	zassert_equal(m.stats.filtered, 1, "warnings only: a watch is logged, not alerted");
	header("ZCZC-WXR-SPS-048453+0100-2781935-KEWX/NWS-");
	zassert_equal(m.stats.filtered, 2);
}

ZTEST(alert_manager, test_tests_never_alert_but_are_logged)
{
	zassert_ok(settings_set_filter(&s, FILTER_ALL, NULL));
	header("ZCZC-WXR-RWT-048000+0015-2791700-KEWX/NWS-");
	header("ZCZC-WXR-RMT-048000+0015-2791705-KEWX/NWS-");
	zassert_equal(state(), ALERT_STATE_STANDBY);
	zassert_equal(m.stats.filtered, 2);
	zassert_equal(alert_log_count(), 2, "both logged");
	zassert_equal(rwt_hook_calls, 1, "the RWT feeds the health check");
}

ZTEST(alert_manager, test_rwt_for_another_area_still_feeds_health)
{
	header("ZCZC-WXR-RWT-040000+0015-2791700-KOUN/NWS-");
	zassert_equal(rwt_hook_calls, 1);
	zassert_equal(state(), ALERT_STATE_STANDBY);
}

ZTEST(alert_manager, test_unknown_codes_are_logged_never_alerted)
{
	struct alert_log_entry e;

	header("ZCZC-WXR-XYZ-048453+0030-2781915-KEWX/NWS-");
	zassert_equal(state(), ALERT_STATE_STANDBY);
	zassert_equal(m.stats.unknown, 1);
	zassert_equal(alert_log_flush(), 1);
	zassert_ok(alert_log_read(0, &e));
	zassert_equal(e.outcome, ALERT_LOG_UNKNOWN);
	zassert_str_equal(e.raw, "ZCZC-WXR-XYZ-048453+0030-2781915-KEWX/NWS-");
}

/* ---- Duplicates and the log ---- */

ZTEST(alert_manager, test_rebroadcast_inside_window_one_alert_one_log)
{
	header(TOR_TRAVIS);
	key(HAL_INPUT_KEY_STBY);
	clock_fake_advance_ms(10 * MINUTE);
	header(TOR_TRAVIS);
	zassert_equal(state(), ALERT_STATE_SILENCED, "no re-alert");
	zassert_equal(m.stats.alerted, 1);
	zassert_equal(m.stats.duplicates, 1);
	zassert_equal(alert_log_flush(), 1);
	zassert_equal(alert_log_count(), 1, "one log entry");
}

ZTEST(alert_manager, test_same_header_after_expiry_alerts_again_clock_unset)
{
	header(TOR_TRAVIS);
	clock_fake_advance_ms(31 * MINUTE);
	zassert_equal(state(), ALERT_STATE_STANDBY);
	header(TOR_TRAVIS);
	zassert_equal(state(), ALERT_STATE_ALERTING, "receive time + purge has passed: new alert");
	zassert_equal(m.stats.alerted, 2);
}

ZTEST(alert_manager, test_stale_header_with_clock_set_is_logged_expired)
{
	struct alert_log_entry e;

	/* Issued 19:15 with 30 minutes' purge; it is now 20:00. */
	zassert_ok(hal_clock_set_utc(utc(2026, 10, 5, 20, 0)));
	header(TOR_TRAVIS);
	zassert_equal(state(), ALERT_STATE_STANDBY);
	zassert_equal(m.stats.expired, 1);
	zassert_equal(alert_log_flush(), 1);
	zassert_ok(alert_log_read(0, &e));
	zassert_equal(e.outcome, ALERT_LOG_EXPIRED);
}

ZTEST(alert_manager, test_issue_plus_purge_sets_the_end_with_clock_set)
{
	zassert_ok(hal_clock_set_utc(utc(2026, 10, 5, 19, 20)));
	header(TOR_TRAVIS); /* expires 19:45, 25 minutes from now */
	clock_fake_advance_ms(25 * MINUTE - 1);
	zassert_equal(state(), ALERT_STATE_ALERTING);
	clock_fake_advance_ms(1);
	zassert_equal(state(), ALERT_STATE_STANDBY);
}

ZTEST(alert_manager, test_log_survives_restart)
{
	struct alert_log_entry e;

	header(TOR_TRAVIS);
	zassert_equal(alert_log_flush(), 1);
	alert_log_init(); /* as after a reset */
	zassert_equal(alert_log_count(), 1);
	zassert_ok(alert_log_read(0, &e));
	zassert_equal(e.outcome, ALERT_LOG_ALERTED);
	zassert_equal(e.received_utc, -1, "clock was unset");
}

ZTEST(alert_manager, test_log_keeps_newest_16)
{
	char text[64];
	struct alert_log_entry e;

	for (int i = 0; i < 20; i++) {
		snprintf(text, sizeof(text), "ZCZC-WXR-SPS-048453+0030-27819%02d-KEWX/NWS-", i);
		header(text);
		(void)alert_log_flush();
	}
	zassert_equal(alert_log_count(), ALERT_LOG_SIZE);
	zassert_ok(alert_log_read(0, &e));
	zassert_str_equal(e.raw, "ZCZC-WXR-SPS-048453+0030-2781919-KEWX/NWS-", "newest first");
	zassert_ok(alert_log_read(15, &e));
	zassert_str_equal(e.raw, "ZCZC-WXR-SPS-048453+0030-2781904-KEWX/NWS-");
}

ZTEST(alert_manager, test_log_waits_for_flush_and_survives_storage_errors)
{
	header(TOR_TRAVIS);
	zassert_equal(alert_log_count(), 1, "queued in RAM");
	storage_fake_fail_writes(true);
	zassert_equal(alert_log_flush(), 0, "flash failing: kept queued");
	storage_fake_fail_writes(false);
	zassert_equal(alert_log_flush(), 1);
}

/* ---- Clock correction ---- */

#define RWT_1700 "ZCZC-WXR-RWT-048000+0015-2781700-KEWX/NWS-"

ZTEST(alert_manager, test_rwt_sets_an_unset_clock)
{
	header(RWT_1700);
	zassert_equal(hal_clock_utc_s(), utc(2026, 10, 5, 17, 0), "year from the firmware epoch");
}

ZTEST(alert_manager, test_rwt_corrects_a_10_minute_error)
{
	zassert_ok(hal_clock_set_utc(utc(2026, 10, 5, 17, 10)));
	header(RWT_1700);
	zassert_equal(hal_clock_utc_s(), utc(2026, 10, 5, 17, 0));
}

ZTEST(alert_manager, test_rwt_leaves_a_2_minute_error_alone)
{
	zassert_ok(hal_clock_set_utc(utc(2026, 10, 5, 17, 2)));
	header(RWT_1700);
	zassert_equal(hal_clock_utc_s(), utc(2026, 10, 5, 17, 2));
}

ZTEST(alert_manager, test_old_tor_never_changes_the_clock)
{
	zassert_ok(hal_clock_set_utc(utc(2026, 10, 5, 23, 0)));
	header("ZCZC-WXR-TOR-048453+0600-2781915-KEWX/NWS-"); /* issued 3 h 45 min ago */
	zassert_equal(hal_clock_utc_s(), utc(2026, 10, 5, 23, 0));
	zassert_equal(state(), ALERT_STATE_ALERTING, "still valid: 6 h purge");

	/* Unset clock: a TOR doesn't set it either. */
	clock_fake_simulate_reset();
	alert_mgr_init(&m, &s, &ui);
	header("ZCZC-WXR-TOR-048453+0600-2781916-KEWX/NWS-");
	zassert_equal(hal_clock_utc_s(), -1);
}

ZTEST(alert_manager, test_unset_clock_year_floor_moves_forward)
{
	/* An RWT sets the clock to Dec 30, 2026; the flush stores that as the floor. */
	header("ZCZC-WXR-RWT-048000+0015-3641200-KEWX/NWS-");
	zassert_equal(hal_clock_utc_s(), utc(2026, 12, 30, 12, 0));
	zassert_ok(clock_sync_flush());

	/*
	 * After a reset the clock is unset again. Day 300 is October 27: with
	 * only the firmware epoch (Oct 1, 2026) that would be 2026, but the
	 * stored floor says the radio has already seen the end of 2026.
	 */
	clock_fake_simulate_reset();
	clock_sync_init(EPOCH);
	alert_mgr_init(&m, &s, &ui);
	header("ZCZC-WXR-RWT-048000+0015-3001200-KEWX/NWS-");
	zassert_equal(hal_clock_utc_s(), utc(2027, 10, 27, 12, 0));
}

/* ---- Milestone 3 task 0 ---- */

ZTEST(alert_manager, test_day_366_in_2027_expires_at_receive_plus_purge)
{
	struct alert_log_entry e;

	zassert_ok(hal_clock_set_utc(utc(2027, 3, 1, 12, 0)));
	header("ZCZC-WXR-TOR-048453+0030-3661200-KEWX/NWS-");
	zassert_equal(state(), ALERT_STATE_ALERTING, "still alerts");
	zassert_equal(m.stats.future_issue, 1, "distrusted issue time counted");
	clock_fake_advance_ms(30 * MINUTE);
	zassert_equal(state(), ALERT_STATE_STANDBY, "expires 30 minutes after receipt, not in 2028");
	zassert_equal(alert_log_flush(), 1);
	zassert_ok(alert_log_read(0, &e));
	zassert_equal(e.outcome, ALERT_LOG_ALERTED);
	zassert_true(e.flags & ALERT_LOG_FLAG_FUTURE_ISSUE, "logged as a distrusted issue time");
}

ZTEST(alert_manager, test_future_issue_15_minutes_distrusted_5_accepted)
{
	zassert_ok(hal_clock_set_utc(utc(2026, 10, 5, 19, 0)));
	header("ZCZC-WXR-TOR-048453+0030-2781915-KEWX/NWS-");
	zassert_equal(m.stats.future_issue, 1);
	zassert_equal(ui.expires_ms, hal_clock_uptime_ms() + 30 * MINUTE, "receive + purge");
	key(HAL_INPUT_KEY_STBY);

	header("ZCZC-WXR-SVR-048453+0030-2781905-KEWX/NWS-");
	zassert_equal(m.stats.future_issue, 1, "5 minutes ahead is trusted");
	zassert_equal(ui.expires_ms, hal_clock_uptime_ms() + 35 * MINUTE, "issue + purge");
}

ZTEST(alert_manager, test_other_counties_cannot_evict_a_matched_alert)
{
	char text[64];

	header(TOR_LONG);
	zassert_equal(m.stats.alerted, 1);
	key(HAL_INPUT_KEY_STBY);
	for (int i = 1; i <= 40; i++) {
		snprintf(text, sizeof(text), "ZCZC-WXR-SVR-0400%02d+0100-2781930-KOUN/NWS-", i);
		header(text);
	}
	zassert_equal(m.stats.not_matched, 40);
	header(TOR_LONG);
	zassert_equal(m.stats.duplicates, 1, "the re-broadcast is still suppressed");
	zassert_equal(m.stats.alerted, 1);
	zassert_equal(state(), ALERT_STATE_SILENCED, "no second alarm");
	zassert_equal(m.dups.evictions, 0);
}

ZTEST(alert_manager, test_weekly_test_duplicates_still_suppressed)
{
	static const char rwt_elsewhere[] = "ZCZC-WXR-RWT-040000+0015-2791700-KOUN/NWS-";

	header(rwt_elsewhere);
	header(rwt_elsewhere);
	zassert_equal(rwt_hook_calls, 1, "one weekly test, not two");
	zassert_equal(m.stats.duplicates, 1);
	zassert_equal(m.stats.rwt, 1);
}

ZTEST(alert_manager, test_rwt_never_jumps_a_set_clock_by_a_year)
{
	/* 2027 has no day 366; the only year with one nearby is 2028. */
	zassert_ok(hal_clock_set_utc(utc(2027, 3, 1, 12, 0)));
	header("ZCZC-WXR-RWT-048000+0015-3661200-KEWX/NWS-");
	zassert_equal(hal_clock_utc_s(), utc(2027, 3, 1, 12, 0), "clock left alone");
	zassert_equal(clock_sync_rejected(), 1, "the rejected correction is counted");
	zassert_equal(rwt_hook_calls, 1, "it still counts as a weekly test for health");

	/* Afterwards a matching TOR issued now still alerts (the clock is sane). */
	header("ZCZC-WXR-TOR-048453+0030-0601200-KEWX/NWS-");
	zassert_equal(state(), ALERT_STATE_ALERTING);
}

ZTEST(alert_manager, test_rwt_still_corrects_a_clock_hours_slow)
{
	zassert_ok(hal_clock_set_utc(utc(2026, 10, 5, 15, 0)));
	header(RWT_1700);
	zassert_equal(hal_clock_utc_s(), utc(2026, 10, 5, 17, 0), "2 hours is within a day");
	zassert_equal(clock_sync_rejected(), 0);
}

ZTEST(alert_manager, test_test_alert_runs_patterns_and_is_logged_as_test)
{
	struct alert_log_entry e;

	zassert_ok(alert_mgr_test_alert(&m));
	zassert_equal(state(), ALERT_STATE_ALERTING);
	zassert_equal(alert_out_fake_buzzer(), HAL_ALERT_PATTERN_ALERT);
	zassert_equal(alert_out_fake_vibrate(), HAL_ALERT_PATTERN_WARNING);
	zassert_equal(ui.screen, UI_SCREEN_ALERT);
	zassert_str_equal(ui.event, ALERT_TEST_EVENT);
	zassert_equal(alert_log_flush(), 1);
	zassert_ok(alert_log_read(0, &e));
	zassert_equal(e.outcome, ALERT_LOG_TEST);
	zassert_equal(m.stats.alerted, 0, "not counted as a real alert");

	clock_fake_advance_ms(ALERT_TEST_MS);
	zassert_equal(state(), ALERT_STATE_STANDBY, "gone after its time");
	zassert_equal(alert_out_fake_buzzer(), HAL_ALERT_PATTERN_OFF);
	zassert_false(alert_out_fake_led());
}

ZTEST(alert_manager, test_test_alert_never_masks_a_real_alert)
{
	header(TOR_LONG);
	zassert_equal(alert_mgr_test_alert(&m), -EBUSY);
	key(HAL_INPUT_KEY_STBY);
	zassert_equal(state(), ALERT_STATE_SILENCED);
	zassert_equal(alert_mgr_test_alert(&m), -EBUSY, "not while silenced");
	zassert_str_equal(ui.event, "TOR");

	clock_fake_advance_ms(6 * 60 * MINUTE);
	key(HAL_INPUT_KEY_BAND);
	zassert_equal(state(), ALERT_STATE_LISTENING);
	zassert_equal(alert_mgr_test_alert(&m), -EBUSY, "not while listening");
	zassert_equal(m.stats.test_alerts, 0);
}

ZTEST(alert_manager, test_last_rwt_time_recorded)
{
	zassert_equal(m.last_rwt_utc, -1);
	zassert_ok(hal_clock_set_utc(utc(2026, 10, 3, 19, 5)));
	header("ZCZC-WXR-RWT-048000+0015-2761900-KEWX/NWS-");
	zassert_equal(m.last_rwt_utc, utc(2026, 10, 3, 19, 5));
}

ZTEST(alert_manager, test_clear_log)
{
	header(TOR_LONG);
	zassert_equal(alert_log_flush(), 1);
	header(TOA_TRAVIS);
	zassert_equal(alert_log_count(), 2, "one stored, one queued");
	alert_log_clear();
	zassert_equal(alert_log_count(), 0);
	alert_log_init();
	zassert_equal(alert_log_count(), 0, "cleared in storage too");
}
