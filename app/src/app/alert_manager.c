/*
 * Alert manager: the alert state machine.
 */

#include <string.h>

#include "app/alert_log.h"
#include "app/alert_manager.h"
#include "app/clock_sync.h"
#include "hal/alert_out.h"
#include "hal/audio_out.h"
#include "services/match/filter.h"
#include "services/match/match.h"
#include "services/match/same_time.h"

#define TUNE_KEYS (HAL_INPUT_KEY_TUNE_UP | HAL_INPUT_KEY_TUNE_DOWN | HAL_INPUT_KEY_BAND)

static void schedule_purge(struct alert_mgr *m);

static const struct active_alert *newest(const struct alert_mgr *m)
{
	const struct active_alert *best = NULL;

	for (int i = 0; i < ALERT_MGR_MAX_ACTIVE; i++) {
		if (m->active[i].used && (best == NULL || m->active[i].seq > best->seq)) {
			best = &m->active[i];
		}
	}
	return best;
}

static void update_ui(struct alert_mgr *m)
{
	struct ui_model *ui = m->ui;
	const struct active_alert *a = newest(m);
	const struct event_entry *e;

	ui->listening = m->state == ALERT_STATE_LISTENING;
	ui->locked = m->locked;
	ui->alert = a != NULL;
	ui->alert_silenced = m->state == ALERT_STATE_SILENCED;
	ui->alert_audio = m->state == ALERT_STATE_ALERT_AUDIO;
	ui->active_alerts = 0;
	for (int i = 0; i < ALERT_MGR_MAX_ACTIVE; i++) {
		ui->active_alerts += m->active[i].used ? 1U : 0U;
	}
	if (a != NULL) {
		memcpy(ui->event, a->header.event, sizeof(ui->event));
		e = event_table_find(&m->settings->events, a->header.event);
		strcpy(ui->event_name, e != NULL ? e->name : a->header.event);
		ui->event_class = a->cls;
		ui->expires_ms = a->expires_ms;
		ui->matched_count = 0;
		for (uint8_t i = 0; i < a->header.location_count; i++) {
			if (a->matched & (1UL << i)) {
				ui->matched[ui->matched_count++] = a->header.locations[i];
			}
		}
	}
	ui_model_refresh(ui);
}

static void outputs_quiet(void)
{
	(void)hal_alert_out_buzzer(HAL_ALERT_PATTERN_OFF);
	(void)hal_alert_out_vibrate(HAL_ALERT_PATTERN_OFF);
}

static void audio_off(void)
{
	(void)hal_audio_out_route(HAL_AUDIO_OUT_ROUTE_NONE);
	(void)hal_audio_out_amp(false);
}

static void stop_sounding(struct alert_mgr *m)
{
	hal_clock_timer_stop(&m->continuous);
	hal_clock_timer_stop(&m->reminder);
}

static void enter(struct alert_mgr *m, enum alert_state next)
{
	enum alert_state prev = (enum alert_state)m->state;
	const struct active_alert *a = newest(m);

	m->state = (uint8_t)next;
	switch (next) {
	case ALERT_STATE_ALERTING:
		if (prev == ALERT_STATE_ALERT_AUDIO) {
			audio_off();
		}
		(void)hal_alert_out_buzzer(HAL_ALERT_PATTERN_ALERT);
		(void)hal_alert_out_vibrate(a != NULL && a->cls == EVENT_CLASS_WARNING
						    ? HAL_ALERT_PATTERN_WARNING
						    : HAL_ALERT_PATTERN_WATCH);
		(void)hal_alert_out_led(true);
		hal_clock_timer_stop(&m->reminder);
		(void)hal_clock_timer_start(&m->continuous, ALERT_CONTINUOUS_MS, 0);
		break;
	case ALERT_STATE_ALERT_AUDIO:
		stop_sounding(m);
		outputs_quiet();
		(void)hal_alert_out_led(true);
		(void)hal_audio_out_amp(true);
		(void)hal_audio_out_route(HAL_AUDIO_OUT_ROUTE_ALERT);
		break;
	case ALERT_STATE_SILENCED:
		stop_sounding(m);
		outputs_quiet();
		if (prev == ALERT_STATE_ALERT_AUDIO) {
			audio_off();
		}
		break;
	case ALERT_STATE_STANDBY:
	case ALERT_STATE_LISTENING:
		stop_sounding(m);
		if (prev == ALERT_STATE_ALERTING || prev == ALERT_STATE_ALERT_AUDIO ||
		    prev == ALERT_STATE_SILENCED) {
			outputs_quiet();
			(void)hal_alert_out_led(false);
		}
		if (prev == ALERT_STATE_ALERT_AUDIO) {
			audio_off();
		}
		if (next == ALERT_STATE_LISTENING) {
			(void)hal_clock_timer_start(&m->idle, LISTENING_IDLE_MS, 0);
		} else {
			hal_clock_timer_stop(&m->idle);
		}
		break;
	}
	update_ui(m);
}

static void continuous_done(struct hal_clock_timer *t, void *user)
{
	struct alert_mgr *m = user;

	(void)t;
	outputs_quiet();
	(void)hal_clock_timer_start(&m->reminder, ALERT_REMINDER_PERIOD_MS, ALERT_REMINDER_PERIOD_MS);
}

static void reminder(struct hal_clock_timer *t, void *user)
{
	(void)t;
	(void)user;
	/* Both patterns stop on their own after 3 seconds. */
	(void)hal_alert_out_buzzer(HAL_ALERT_PATTERN_REMINDER);
	(void)hal_alert_out_vibrate(HAL_ALERT_PATTERN_REMINDER);
}

static void purge_due(struct hal_clock_timer *t, void *user)
{
	struct alert_mgr *m = user;
	int64_t now = hal_clock_uptime_ms();
	bool any = false;

	(void)t;
	for (int i = 0; i < ALERT_MGR_MAX_ACTIVE; i++) {
		if (m->active[i].used && m->active[i].expires_ms <= now) {
			m->active[i].used = false;
		}
		any = any || m->active[i].used;
	}
	if (any) {
		schedule_purge(m);
		update_ui(m);
	} else if (m->state != ALERT_STATE_LISTENING && m->state != ALERT_STATE_STANDBY) {
		enter(m, ALERT_STATE_STANDBY);
	}
}

static void idle_timeout(struct hal_clock_timer *t, void *user)
{
	struct alert_mgr *m = user;

	(void)t;
	if (m->state == ALERT_STATE_LISTENING) {
		enter(m, ALERT_STATE_STANDBY);
	}
}

static void schedule_purge(struct alert_mgr *m)
{
	int64_t soonest = -1;
	int64_t now = hal_clock_uptime_ms();

	for (int i = 0; i < ALERT_MGR_MAX_ACTIVE; i++) {
		if (m->active[i].used && (soonest < 0 || m->active[i].expires_ms < soonest)) {
			soonest = m->active[i].expires_ms;
		}
	}
	if (soonest < 0) {
		hal_clock_timer_stop(&m->purge);
		return;
	}
	(void)hal_clock_timer_start(&m->purge, soonest > now ? (uint32_t)(soonest - now) : 0U, 0);
}

static void add_active(struct alert_mgr *m, const struct same_header *h, uint8_t cls,
		       uint32_t matched, int64_t expires_ms)
{
	struct active_alert *slot = NULL;

	for (int i = 0; i < ALERT_MGR_MAX_ACTIVE; i++) {
		struct active_alert *a = &m->active[i];

		if (!a->used) {
			slot = a;
			break;
		}
		if (slot == NULL || a->expires_ms < slot->expires_ms) {
			slot = a;
		}
	}
	if (slot->used) {
		m->stats.displaced++;
	}
	slot->used = true;
	slot->header = *h;
	slot->cls = cls;
	slot->matched = matched;
	slot->expires_ms = expires_ms;
	slot->seq = ++m->seq;
	schedule_purge(m);
}

void alert_mgr_init(struct alert_mgr *m, const struct settings *settings, struct ui_model *ui)
{
	memset(m, 0, sizeof(*m));
	m->settings = settings;
	m->ui = ui;
	m->state = ALERT_STATE_STANDBY;
	m->locked = hal_input_locked() != 0;
	m->headphones = hal_input_headphones() != 0;
	dup_init(&m->dups);
	hal_clock_timer_init(&m->continuous, continuous_done, m);
	hal_clock_timer_init(&m->reminder, reminder, m);
	hal_clock_timer_init(&m->purge, purge_due, m);
	hal_clock_timer_init(&m->idle, idle_timeout, m);
	update_ui(m);
}

void alert_mgr_set_rwt_hook(struct alert_mgr *m, alert_mgr_rwt_cb cb, void *user)
{
	m->on_rwt = cb;
	m->rwt_user = user;
}

void alert_mgr_on_header(struct alert_mgr *m, const struct same_header *h)
{
	const struct settings *s = m->settings;
	const struct county_list *counties = settings_active_counties(s);
	const struct event_entry *e = event_table_find(&s->events, h->event);
	bool is_test = e != NULL && e->cls == EVENT_CLASS_TEST;
	bool is_rwt = memcmp(h->event, "RWT", 3) == 0;
	int64_t now_ms = hal_clock_uptime_ms();
	enum filter_verdict verdict;
	uint32_t matched;
	int64_t expires;
	uint8_t flags = 0;

	m->stats.headers++;
	if (m->state == ALERT_STATE_LISTENING) {
		m->stats.ignored_listening++; /* the tuner isn't on weather: alerts paused */
		return;
	}
	if (dup_seen(&m->dups, h, now_ms)) {
		m->stats.duplicates++;
		return;
	}
	if (is_rwt) {
		m->stats.rwt++;
		(void)clock_sync_on_rwt(h);
		if (m->on_rwt != NULL) {
			m->on_rwt(m->rwt_user);
		}
	}

	matched = match_header(h, counties->codes, counties->count);
	if (matched == 0U && !is_test) {
		/* Other counties' traffic never enters the duplicate store: on a busy
		 * day it would evict our own alerts. */
		m->stats.not_matched++;
		return;
	}

	if (same_issue_in_future(h, hal_clock_utc_s())) {
		m->stats.future_issue++;
		flags |= ALERT_LOG_FLAG_FUTURE_ISSUE;
	}
	expires = same_expiry_ms(h, hal_clock_utc_s(), now_ms);
	if (matched != 0U || is_rwt) {
		dup_add(&m->dups, h, expires, now_ms);
	}
	if (expires <= now_ms) {
		m->stats.expired++;
		alert_log_append(h, hal_clock_utc_s(), ALERT_LOG_EXPIRED, flags);
		return;
	}

	verdict = filter_decide(&s->events, h->event, (enum filter_preset)s->filter, s->custom);
	if (verdict == FILTER_VERDICT_UNKNOWN) {
		m->stats.unknown++;
		alert_log_append(h, hal_clock_utc_s(), ALERT_LOG_UNKNOWN, flags);
		return;
	}
	if (verdict == FILTER_VERDICT_LOG) {
		m->stats.filtered++;
		alert_log_append(h, hal_clock_utc_s(), ALERT_LOG_FILTERED, flags);
		return;
	}

	m->stats.alerted++;
	alert_log_append(h, hal_clock_utc_s(), ALERT_LOG_ALERTED, flags);
	add_active(m, h, e->cls, matched, expires);
	enter(m, m->headphones ? ALERT_STATE_ALERT_AUDIO : ALERT_STATE_ALERTING);
}

void alert_mgr_on_eom(struct alert_mgr *m)
{
	if (m->state == ALERT_STATE_ALERT_AUDIO) {
		enter(m, ALERT_STATE_SILENCED);
	}
}

static void on_key(struct alert_mgr *m, const struct hal_input_event *e)
{
	switch ((enum alert_state)m->state) {
	case ALERT_STATE_ALERTING:
	case ALERT_STATE_ALERT_AUDIO:
		/* Any key only silences, locked or not. */
		enter(m, ALERT_STATE_SILENCED);
		break;
	case ALERT_STATE_SILENCED:
		break; /* only the purge time ends it */
	case ALERT_STATE_STANDBY:
		if (!m->locked && e->type == HAL_INPUT_PRESS && (e->keys & TUNE_KEYS) != 0U) {
			enter(m, ALERT_STATE_LISTENING);
		}
		break;
	case ALERT_STATE_LISTENING:
		if (m->locked) {
			break;
		}
		if (e->type == HAL_INPUT_PRESS && (e->keys & HAL_INPUT_KEY_STBY) != 0U) {
			enter(m, ALERT_STATE_STANDBY);
		} else {
			(void)hal_clock_timer_start(&m->idle, LISTENING_IDLE_MS, 0);
		}
		break;
	}
}

void alert_mgr_on_input(struct alert_mgr *m, const struct hal_input_event *e)
{
	switch (e->type) {
	case HAL_INPUT_LOCK_ON:
	case HAL_INPUT_LOCK_OFF:
		m->locked = e->type == HAL_INPUT_LOCK_ON;
		update_ui(m);
		break;
	case HAL_INPUT_HEADPHONES_IN:
		m->headphones = true;
		if (m->state == ALERT_STATE_ALERTING) {
			enter(m, ALERT_STATE_ALERT_AUDIO);
		}
		break;
	case HAL_INPUT_HEADPHONES_OUT:
		m->headphones = false;
		if (m->state == ALERT_STATE_ALERT_AUDIO) {
			enter(m, ALERT_STATE_SILENCED);
		}
		break;
	case HAL_INPUT_PRESS:
	case HAL_INPUT_LONG_PRESS:
	case HAL_INPUT_COMBO:
		on_key(m, e);
		break;
	}
}

enum alert_state alert_mgr_state(const struct alert_mgr *m)
{
	return (enum alert_state)m->state;
}

const char *alert_state_name(enum alert_state state)
{
	static const char *const names[] = {"listening", "standby", "alerting", "alert audio",
					    "silenced"};

	return (unsigned int)state < sizeof(names) / sizeof(names[0]) ? names[state] : "?";
}
