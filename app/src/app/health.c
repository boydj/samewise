/*
 * Health supervisor.
 */

#include <string.h>

#include "app/health.h"
#include "hal/alert_out.h"
#include "hal/battery.h"
#include "hal/storage.h"
#include "hal/tuner.h"

#define KEY_RESETS "health/wdt"

static bool listening(const struct health *hs)
{
	return alert_mgr_state(hs->alerts) == ALERT_STATE_LISTENING;
}

static bool alert_sounding(const struct health *hs)
{
	enum alert_state st = alert_mgr_state(hs->alerts);

	return st == ALERT_STATE_ALERTING || st == ALERT_STATE_ALERT_AUDIO;
}

static void chirp(struct health *hs, enum hal_alert_pattern pattern)
{
	if (!alert_sounding(hs)) {
		(void)hal_alert_out_buzzer(pattern);
		hs->stats.chirps++;
	}
}

static void set_warning(struct health *hs, uint32_t bit, bool on)
{
	if (on) {
		hs->ui->warnings |= bit;
	} else {
		hs->ui->warnings &= ~bit;
	}
}

static void reset_tuner(struct health *hs)
{
	hs->stats.tuner_resets++;
	(void)hal_tuner_power(false);
	hs->ui->band = HAL_TUNER_BAND_WB;
	if (hal_tuner_power(true) == 0 && hal_tuner_set_band(HAL_TUNER_BAND_WB) == 0) {
		(void)hal_tuner_tune(hs->tuner_khz != 0U ? hs->tuner_khz : hs->cfg.default_khz);
	}
}

uint16_t health_hours_left(const struct hal_battery_status *b)
{
	if (b->charge == HAL_BATTERY_DISCHARGING && b->hours_left != HAL_BATTERY_HOURS_UNKNOWN) {
		return b->hours_left;
	}
	return (uint16_t)((b->soc_percent * UI_STANDBY_HOURS_FULL + 50U) / 100U);
}

uint8_t health_signal_bars(int16_t snr_db, int16_t min_snr_db)
{
	int steps;

	if (snr_db < min_snr_db) {
		return 0;
	}
	steps = 1 + (snr_db - min_snr_db) / HEALTH_BAR_STEP_DB;
	return (uint8_t)(steps > (int)UI_SIGNAL_BARS ? UI_SIGNAL_BARS : steps);
}

static void check_tuner(struct health *hs, int64_t now)
{
	struct hal_tuner_status st;
	int err = hal_tuner_get_status(&st);

	if (err == 0 && st.valid) {
		hs->ui->freq_khz = st.freq_khz;
		hs->ui->stereo = st.stereo;
		hs->ui->signal_bars = health_signal_bars(st.snr_db, hs->cfg.min_snr_db);
		hs->consecutive_faults = 0;
		hs->last_tuner_ok_ms = now;
		set_warning(hs, UI_WARN_TUNER_FAULT, false);
		if (!listening(hs)) {
			hs->tuner_khz = st.freq_khz;
			hs->snr_db = st.snr_db;
			hs->rssi_dbuv = st.rssi_dbuv;
			if (st.snr_db < hs->cfg.min_snr_db) {
				if (hs->weak_since_ms < 0) {
					hs->weak_since_ms = now;
				}
			} else {
				hs->weak_since_ms = -1;
			}
		}
		return;
	}
	hs->stats.tuner_faults++;
	if (++hs->consecutive_faults >= HEALTH_TUNER_FAULTS) {
		hs->consecutive_faults = 0;
		set_warning(hs, UI_WARN_TUNER_FAULT, true);
		reset_tuner(hs);
	}
}

static void check_warnings(struct health *hs, int64_t now)
{
	bool no_signal = !listening(hs) && hs->weak_since_ms >= 0 &&
			 now - hs->weak_since_ms >= HEALTH_NO_SIGNAL_MS;
	bool no_rwt = now - hs->last_rwt_ms >= HEALTH_NO_RWT_MS;
	bool was = (hs->ui->warnings & (UI_WARN_NO_SIGNAL | UI_WARN_NO_WEEKLY_TEST)) != 0U;

	set_warning(hs, UI_WARN_NO_SIGNAL, no_signal);
	set_warning(hs, UI_WARN_NO_WEEKLY_TEST, no_rwt);
	if (!no_signal && !no_rwt) {
		return;
	}
	if (!was || now >= hs->next_warning_chirp_ms) {
		chirp(hs, HAL_ALERT_PATTERN_CHIRP);
		hs->next_warning_chirp_ms = now + HEALTH_WARNING_CHIRP_MS;
	}
}

static void shut_down(struct health *hs)
{
	hs->shut_down = true;
	(void)hal_alert_out_buzzer(HAL_ALERT_PATTERN_FINAL_BEEP);
	hs->ui->alerts_off = true;
	ui_model_refresh(hs->ui);
	hal_clock_timer_stop(&hs->tick);
	(void)hal_battery_ship_mode();
}

static void check_battery(struct health *hs, int64_t now)
{
	struct hal_battery_status b;
	bool low_was = (hs->ui->warnings & UI_WARN_BATTERY_LOW) != 0U;
	bool crit_was = (hs->ui->warnings & UI_WARN_BATTERY_CRITICAL) != 0U;
	bool charging;
	bool low, crit;

	if (hal_battery_get(&b) != 0) {
		return;
	}
	hs->ui->battery_percent = b.soc_percent;
	hs->ui->charging = b.charge != HAL_BATTERY_DISCHARGING;
	hs->ui->hours_left = health_hours_left(&b);
	if (b.voltage_mv <= HEALTH_BATTERY_EMPTY_MV && b.charge == HAL_BATTERY_DISCHARGING) {
		shut_down(hs);
		return;
	}
	charging = b.charge != HAL_BATTERY_DISCHARGING;
	low = !charging && (b.soc_percent <= HEALTH_BATTERY_LOW ||
			    (low_was && b.soc_percent < HEALTH_BATTERY_LOW + HEALTH_BATTERY_HYST));
	crit = !charging &&
	       (b.soc_percent <= HEALTH_BATTERY_CRITICAL ||
		(crit_was && b.soc_percent < HEALTH_BATTERY_CRITICAL + HEALTH_BATTERY_HYST));

	set_warning(hs, UI_WARN_BATTERY_LOW, low);
	set_warning(hs, UI_WARN_BATTERY_CRITICAL, crit);
	if (crit) {
		if (!crit_was || now >= hs->next_critical_chirp_ms) {
			chirp(hs, HAL_ALERT_PATTERN_CHIRP);
			hs->next_critical_chirp_ms = now + HEALTH_CRITICAL_CHIRP_MS;
		}
	} else if (low && !low_was) {
		chirp(hs, HAL_ALERT_PATTERN_CHIRP);
	}
}

static bool heartbeats_ok(const struct health *hs, int64_t now)
{
	if (now - hs->last_tuner_ok_ms > HEALTH_TUNER_STALE_MS) {
		return false;
	}
	if (listening(hs)) {
		return true;
	}
	return now - hs->last_audio_ms <= HEALTH_HEARTBEAT_MS &&
	       now - hs->last_decoder_ms <= HEALTH_HEARTBEAT_MS;
}

static void tick(struct hal_clock_timer *t, void *user)
{
	struct health *hs = user;
	int64_t now = hal_clock_uptime_ms();

	(void)t;
	check_tuner(hs, now);
	if (heartbeats_ok(hs, now)) {
		(void)hal_watchdog_feed();
		hs->stats.feeds++;
	} else {
		hs->stats.missed_feeds++;
	}
	if (now >= hs->next_battery_ms) {
		hs->next_battery_ms = now + HEALTH_BATTERY_POLL_MS;
		check_battery(hs, now);
		if (hs->shut_down) {
			return;
		}
	}
	check_warnings(hs, now);
	if (hs->ui->restarted && now >= hs->restarted_until_ms) {
		hs->ui->restarted = false;
	}
	ui_model_refresh(hs->ui);
}

static void log_reset(enum hal_reset_reason reason)
{
	uint32_t count = health_logged_watchdog_resets();

	if (reason == HAL_RESET_WATCHDOG) {
		count++;
		(void)hal_storage_write(KEY_RESETS, &count, sizeof(count));
	}
}

uint32_t health_logged_watchdog_resets(void)
{
	uint32_t count = 0;

	if (hal_storage_read(KEY_RESETS, &count, sizeof(count)) != (int)sizeof(count)) {
		count = 0;
	}
	return count;
}

void health_init(struct health *hs, const struct health_config *cfg,
		 const struct alert_mgr *alerts, struct ui_model *ui)
{
	int64_t now = hal_clock_uptime_ms();

	memset(hs, 0, sizeof(*hs));
	hs->cfg = *cfg;
	hs->alerts = alerts;
	hs->ui = ui;
	hs->last_audio_ms = now;
	hs->last_decoder_ms = now;
	hs->last_tuner_ok_ms = now;
	hs->last_rwt_ms = now;
	hs->weak_since_ms = -1;
	hs->next_battery_ms = now;

	hs->boot_reason = hal_watchdog_reset_reason();
	log_reset(hs->boot_reason); /* boot path, not the alert path */
	if (hs->boot_reason == HAL_RESET_WATCHDOG) {
		hs->stats.boots_after_watchdog++;
		ui->restarted = true;
		hs->restarted_until_ms = now + HEALTH_RESTARTED_MS;
		ui_model_refresh(ui);
	}

	(void)hal_watchdog_start(HAL_WATCHDOG_TIMEOUT_MS);
	hal_clock_timer_init(&hs->tick, tick, hs);
	(void)hal_clock_timer_start(&hs->tick, HEALTH_TICK_MS, HEALTH_TICK_MS);
}

void health_note_audio(struct health *hs)
{
	hs->last_audio_ms = hal_clock_uptime_ms();
}

void health_note_decoder(struct health *hs, uint32_t samples)
{
	if (samples != hs->decoder_samples) {
		hs->decoder_samples = samples;
		hs->last_decoder_ms = hal_clock_uptime_ms();
	}
}

void health_note_rwt(void *user)
{
	struct health *hs = user;

	hs->last_rwt_ms = hal_clock_uptime_ms();
	hs->ui->warnings &= ~(uint32_t)UI_WARN_NO_WEEKLY_TEST;
	ui_model_refresh(hs->ui);
}
