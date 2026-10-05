/*
 * Health supervisor (spec: Health monitoring). Owns every check that could
 * silently stop alerts, and is the only code that feeds the watchdog.
 *
 * Runs on a 1-second hal/clock.h tick:
 *   - Firmware hang: feeds the watchdog only while the heartbeats are
 *     current: audio blocks and decoder progress within HEALTH_HEARTBEAT_MS
 *     (not required while Listening: the tuner is on AM/FM), and a valid
 *     tuner status within HEALTH_TUNER_STALE_MS. Otherwise the watchdog
 *     resets the radio after 8 s; the next boot logs the reason and shows
 *     RESTARTED for HEALTH_RESTARTED_MS.
 *   - Tuner fault: 3 consecutive failed status reads reset and re-tune the
 *     tuner; if it keeps failing, the tuner heartbeat goes stale and the
 *     watchdog resets the radio.
 *   - No signal: quality below the threshold for 10 minutes (not while
 *     Listening); no weekly test: no RWT for 8 days since boot or the last
 *     RWT. Either shows the warning screen with its reason line and chirps
 *     at once, then hourly.
 *   - Battery: chirp and warning at 20%, a chirp every 30 minutes at 5%,
 *     and at 3.3 V a final long beep, ALERTS OFF and charger ship mode.
 * Chirps never sound over an active alert.
 */

#ifndef APP_HEALTH_H_
#define APP_HEALTH_H_

#include <stdbool.h>
#include <stdint.h>

#include "app/alert_manager.h"
#include "app/ui_model.h"
#include "hal/clock.h"
#include "hal/watchdog.h"

#ifdef __cplusplus
extern "C" {
#endif

#define HEALTH_TICK_MS          1000U
#define HEALTH_HEARTBEAT_MS     2000U
#define HEALTH_TUNER_STALE_MS   10000U
#define HEALTH_TUNER_FAULTS     3U
#define HEALTH_RESTARTED_MS     10000U
#define HEALTH_NO_SIGNAL_MS     (10U * 60U * 1000U)
#define HEALTH_NO_RWT_MS        (8LL * 24 * 3600 * 1000)
#define HEALTH_WARNING_CHIRP_MS (60U * 60U * 1000U)
#define HEALTH_CRITICAL_CHIRP_MS (30U * 60U * 1000U)
#define HEALTH_BATTERY_POLL_MS  60000U
#define HEALTH_BATTERY_LOW      20U
#define HEALTH_BATTERY_CRITICAL 5U
#define HEALTH_BATTERY_EMPTY_MV 3300U
/** Hysteresis before a battery warning clears (percent above its threshold). */
#define HEALTH_BATTERY_HYST     5U

struct health_config {
	/** Signal quality threshold; placeholder until bring-up measurements. */
	int16_t min_snr_db;
	/** Weather frequency to re-tune to if the tuner never reported one. */
	uint32_t default_khz;
};

#define HEALTH_CONFIG_DEFAULT {.min_snr_db = 10, .default_khz = 162550U}

struct health_stats {
	uint32_t feeds;
	uint32_t missed_feeds;
	uint32_t tuner_faults;
	uint32_t tuner_resets;
	uint32_t chirps;
	uint32_t boots_after_watchdog;
};

struct health {
	struct health_config cfg;
	const struct alert_mgr *alerts;
	struct ui_model *ui;
	struct hal_clock_timer tick;
	int64_t last_audio_ms;
	int64_t last_decoder_ms;
	uint32_t decoder_samples;
	int64_t last_tuner_ok_ms;
	uint32_t tuner_khz;
	uint8_t consecutive_faults;
	int64_t weak_since_ms;  /* -1 while the signal is good */
	int64_t last_rwt_ms;
	int64_t next_warning_chirp_ms;
	int64_t next_critical_chirp_ms;
	int64_t restarted_until_ms;
	int64_t next_battery_ms;
	bool shut_down;
	enum hal_reset_reason boot_reason;
	struct health_stats stats;
};

/**
 * Start supervising: read and log the reset reason, start the watchdog
 * and the tick. alerts and ui must outlive the supervisor.
 */
void health_init(struct health *hs, const struct health_config *cfg,
		 const struct alert_mgr *alerts, struct ui_model *ui);

/** An audio block reached the decoder thread. */
void health_note_audio(struct health *hs);

/** Decoder progress: its sample counter (same_stats.samples). */
void health_note_decoder(struct health *hs, uint32_t samples);

/** A new RWT was decoded (alert manager hook). */
void health_note_rwt(void *hs);

/** Resets after a watchdog timeout, read back from storage. */
uint32_t health_logged_watchdog_resets(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_HEALTH_H_ */
