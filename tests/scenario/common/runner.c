/*
 * Scenario runner.
 */

#include <string.h>

#include "fakes/alert_out_fake.h"
#include "fakes/audio_in_fake.h"
#include "fakes/audio_out_fake.h"
#include "fakes/battery_fake.h"
#include "fakes/clock_fake.h"
#include "fakes/input_fake.h"
#include "fakes/storage_fake.h"
#include "fakes/tuner_fake.h"
#include "fakes/watchdog_fake.h"
#include "hal/audio_in.h"
#include "hal/clock.h"
#include "runner.h"

#define BLOCK        HAL_AUDIO_IN_BLOCK_SAMPLES
#define IDLE_STEP_MS 1000
#define TAIL_MS      6000 /* silence after a WAV: lets copy timeouts run */
/* 2026-10-01T00:00:00Z */
#define FIRMWARE_EPOCH 1790812800LL

static struct {
	struct radio radio;
	struct health_config cfg;
	uint32_t boots;
	bool off;
	bool decoder_stalled;
} s;

static void boot(void)
{
	radio_boot(&s.radio, &s.cfg, FIRMWARE_EPOCH);
	s.boots++;
}

static void on_watchdog_reset(void *user)
{
	(void)user;
	clock_fake_simulate_reset(); /* RAM and RTC lost */
	if (battery_fake_shipped()) {
		s.off = true; /* the charger cut the power */
		return;
	}
	boot();
}

void scn_init(void)
{
	static const struct health_config cfg = HEALTH_CONFIG_DEFAULT;

	memset(&s, 0, sizeof(s));
	s.cfg = cfg;
	clock_fake_reset();
	storage_fake_init();
	alert_out_fake_init();
	audio_out_fake_init();
	audio_in_fake_close();
	input_fake_init();
	tuner_fake_init();
	battery_fake_init();
	watchdog_fake_init(on_watchdog_reset, NULL);
	boot();
}

static struct same_location parse_loc(const char *p)
{
	struct same_location l = {
		.subdivision = (uint8_t)(p[0] - '0'),
		.state = (uint8_t)((p[1] - '0') * 10 + (p[2] - '0')),
		.county = (uint16_t)((p[3] - '0') * 100 + (p[4] - '0') * 10 + (p[5] - '0')),
	};
	return l;
}

void scn_set_home(const char *const *pssccc, size_t n)
{
	struct same_location codes[SETTINGS_MAX_COUNTIES];

	for (size_t i = 0; i < n && i < SETTINGS_MAX_COUNTIES; i++) {
		codes[i] = parse_loc(pssccc[i]);
	}
	(void)settings_set_counties(&s.radio.settings, SETTINGS_MODE_HOME, codes, (uint8_t)n);
}

void scn_use_test_events(void)
{
	struct event_table t;

	event_table_clear(&t, 1);
	(void)event_table_add(&t, "TOR", "Tornado Warning", EVENT_CLASS_WARNING);
	(void)event_table_add(&t, "SVR", "Severe Thunderstorm Warning", EVENT_CLASS_WARNING);
	(void)event_table_add(&t, "TOA", "Tornado Watch", EVENT_CLASS_WATCH);
	(void)event_table_add(&t, "RWT", "Required Weekly Test", EVENT_CLASS_TEST);
	(void)event_table_add(&t, "RMT", "Required Monthly Test", EVENT_CLASS_TEST);
	(void)settings_set_event_table(&s.radio.settings, &t);
}

struct radio *scn_radio(void)
{
	return &s.radio;
}

uint32_t scn_boots(void)
{
	return s.boots;
}

bool scn_device_off(void)
{
	return s.off;
}

/* Advance to t in idle steps with heartbeats (none while the decoder is stalled). */
static void idle_to(int64_t t)
{
	while (hal_clock_uptime_ms() < t) {
		int64_t now = hal_clock_uptime_ms();
		int64_t step = t - now < IDLE_STEP_MS ? t - now : IDLE_STEP_MS;

		if (!s.off && !s.decoder_stalled) {
			radio_idle_audio(&s.radio, (uint32_t)SAME_MS_TO_SAMPLES(step));
		}
		clock_fake_advance_ms(step);
		if (!s.off) {
			radio_low_priority(&s.radio);
		}
	}
}

/* ms from the start of playback after n samples, at 16 MHz / 1536. */
static int64_t samples_to_ms(uint64_t n)
{
	return (int64_t)(n * HAL_AUDIO_IN_RATE_DEN / (HAL_AUDIO_IN_RATE_NUM / 1000U));
}

static void apply(const struct scn_event *e)
{
	switch (e->kind) {
	case SCN_SIGNAL:
		tuner_fake_set_signal((int16_t)e->a, (int16_t)e->b);
		break;
	case SCN_KEY:
		input_fake_emit(HAL_INPUT_PRESS, (uint32_t)e->a);
		break;
	case SCN_HEADPHONES:
		input_fake_emit(e->a ? HAL_INPUT_HEADPHONES_IN : HAL_INPUT_HEADPHONES_OUT, 0);
		break;
	case SCN_LOCK:
		input_fake_emit(e->a ? HAL_INPUT_LOCK_ON : HAL_INPUT_LOCK_OFF, 0);
		break;
	case SCN_DECODER_STALL:
		s.decoder_stalled = e->a != 0;
		break;
	case SCN_TUNER_FAULTS:
		tuner_fake_fail_status((uint32_t)e->a, TUNER_FAKE_IO);
		break;
	case SCN_WAV:
		break; /* handled by the caller */
	}
}

/* Play a WAV from now, applying events due meanwhile; returns the next event index. */
static size_t play(const char *path, const struct scn_event *ev, size_t n, size_t next)
{
	static const int16_t silence[BLOCK];
	int64_t start = hal_clock_uptime_ms();
	uint64_t played = 0;
	bool wav = audio_in_fake_open(path) == 0 && hal_audio_in_start() == 0;
	int64_t tail_end = -1;

	while (wav || hal_clock_uptime_ms() < tail_end) {
		int got;

		while (next < n && ev[next].at_ms <= hal_clock_uptime_ms() && ev[next].kind != SCN_WAV) {
			apply(&ev[next++]);
		}
		if (s.off || s.decoder_stalled) {
			got = BLOCK; /* time passes; nothing is decoded */
		} else if (wav) {
			got = radio_pump_audio(&s.radio, BLOCK);
		} else {
			same_feed(&s.radio.decoder, silence, BLOCK);
			health_note_audio(&s.radio.health);
			health_note_decoder(&s.radio.health, same_get_stats(&s.radio.decoder)->samples +
								     s.radio.idle_samples);
			got = BLOCK;
		}
		if (wav && got <= 0) {
			wav = false;
			tail_end = hal_clock_uptime_ms() + TAIL_MS;
			continue;
		}
		played += (uint64_t)got;
		clock_fake_advance_ms(start + samples_to_ms(played) - hal_clock_uptime_ms());
		if (!s.off) {
			radio_low_priority(&s.radio);
		}
	}
	audio_in_fake_close();
	return next;
}

void scn_run(const struct scn_event *ev, size_t n, int64_t until_ms)
{
	size_t i = 0;

	while (i < n) {
		idle_to(ev[i].at_ms);
		if (ev[i].kind == SCN_WAV) {
			const char *path = ev[i].path;

			i = play(path, ev, n, i + 1);
		} else {
			apply(&ev[i++]);
		}
	}
	idle_to(until_ms);
}

void scn_idle_until(int64_t at_ms)
{
	idle_to(at_ms);
}
