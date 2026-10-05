/*
 * Alert manager: the alert state machine (spec: Alert states, Alert
 * behaviour).
 *
 * Driven by events: decoded headers and EOMs, input events, and its own
 * hal/clock.h timers. Drives hal/alert_out.h and hal/audio_out.h, the alert
 * log and the screen model. Never waits on flash, Bluetooth or the display.
 *
 *   Listening   AM/FM, alerts paused; STBY or 60 min idle -> Standby
 *   Standby     decoding weather; tune or band press -> Listening
 *   Alerting    buzzer and vibration 2 min, then a 3 s reminder every 5 min
 *   Alert audio headphones in: broadcast in the headphones, buzzer off
 *   Silenced    alert on screen and LED until its purge time
 *
 * A new matching alert re-alerts from any state but Listening; the screen
 * shows the newest, and the radio returns to Standby when every active
 * alert has expired.
 */

#ifndef APP_ALERT_MANAGER_H_
#define APP_ALERT_MANAGER_H_

#include <stdbool.h>
#include <stdint.h>

#include "app/settings.h"
#include "app/ui_model.h"
#include "hal/clock.h"
#include "hal/input.h"
#include "services/match/dup.h"
#include "services/same/same_header.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ALERT_MGR_MAX_ACTIVE      8
#define ALERT_CONTINUOUS_MS       (2U * 60U * 1000U)
#define ALERT_REMINDER_PERIOD_MS  (5U * 60U * 1000U)
#define LISTENING_IDLE_MS         (60U * 60U * 1000U)
/** A test alert stays on screen this long unless a key silences it first. */
#define ALERT_TEST_MS             ALERT_CONTINUOUS_MS
/** The event a test alert shows: Practice/Demo Warning. */
#define ALERT_TEST_EVENT          "DMO"

enum alert_state {
	ALERT_STATE_LISTENING,
	ALERT_STATE_STANDBY,
	ALERT_STATE_ALERTING,
	ALERT_STATE_ALERT_AUDIO,
	ALERT_STATE_SILENCED,
};

struct active_alert {
	bool used;
	struct same_header header;
	uint8_t cls;      /* enum event_class */
	uint32_t matched; /* bit i = header.locations[i] matched */
	int64_t expires_ms;
	uint32_t seq;     /* arrival order, for "newest" */
};

struct alert_mgr_stats {
	uint32_t headers;
	uint32_t ignored_listening;
	uint32_t duplicates;
	uint32_t not_matched;
	uint32_t alerted;
	uint32_t filtered;
	uint32_t unknown;
	uint32_t expired;
	uint32_t future_issue; /* issue time distrusted: expiry from receipt */
	uint32_t rwt;
	uint32_t displaced; /* active alerts pushed out when the list was full */
	uint32_t test_alerts;
};

typedef void (*alert_mgr_rwt_cb)(void *user);

struct alert_mgr {
	const struct settings *settings;
	struct ui_model *ui;
	uint8_t state;
	bool locked;
	bool headphones;
	uint32_t seq;
	struct active_alert active[ALERT_MGR_MAX_ACTIVE];
	struct dup_store dups;
	struct hal_clock_timer continuous;
	struct hal_clock_timer reminder;
	struct hal_clock_timer purge;
	struct hal_clock_timer idle;
	int64_t last_rwt_utc; /* -1 until an RWT arrives with the clock set */
	alert_mgr_rwt_cb on_rwt;
	void *rwt_user;
	struct alert_mgr_stats stats;
};

/**
 * Start in Standby with no alerts. Reads the lock and headphone state from
 * hal/input.h. settings and ui must outlive the manager.
 */
void alert_mgr_init(struct alert_mgr *m, const struct settings *settings, struct ui_model *ui);

/** Called for every new RWT, matching or not (the weekly-test health check). */
void alert_mgr_set_rwt_hook(struct alert_mgr *m, alert_mgr_rwt_cb cb, void *user);

/** A voted, parsed header from the SAME decoder. */
void alert_mgr_on_header(struct alert_mgr *m, const struct same_header *h);

/**
 * Test alert from the phone: the warning patterns and the alert screen for
 * ALERT_TEST_MS, logged as ALERT_LOG_TEST, never added to the duplicate
 * store. Only from Standby, so it can't mask a real alert or interrupt
 * listening: -EBUSY otherwise.
 */
int alert_mgr_test_alert(struct alert_mgr *m);

/** End of message from the SAME decoder. */
void alert_mgr_on_eom(struct alert_mgr *m);

/** A hal/input.h event. */
void alert_mgr_on_input(struct alert_mgr *m, const struct hal_input_event *e);

enum alert_state alert_mgr_state(const struct alert_mgr *m);

const char *alert_state_name(enum alert_state state);

#ifdef __cplusplus
}
#endif

#endif /* APP_ALERT_MANAGER_H_ */
