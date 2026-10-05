/*
 * Scenario runner: plays a timeline against the whole radio on native_sim
 * with the fast clock.
 *
 * Events happen at absolute simulated times. WAV audio is decoded for real,
 * in 160-sample blocks with the clock advanced to match; between events the
 * runner advances in 1-second steps and reports the audio as received and
 * decoded without decoding it (radio_idle_audio), which is what lets a week
 * of standby run in seconds. A watchdog reset reboots the radio, unless the
 * battery has gone into ship mode, which turns the device off.
 */

#ifndef TESTS_SCENARIO_RUNNER_H_
#define TESTS_SCENARIO_RUNNER_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "app/radio.h"

enum scn_kind {
	SCN_WAV,           /* path: play a WAV file, then 6 s of silence */
	SCN_SIGNAL,        /* a = RSSI dBuV, b = SNR dB */
	SCN_KEY,           /* a = hal_input_key mask, short press */
	SCN_HEADPHONES,    /* a = 1 in, 0 out */
	SCN_LOCK,          /* a = 1 locked, 0 unlocked */
	SCN_DECODER_STALL, /* a = 1 stalled, 0 running */
	SCN_TUNER_FAULTS,  /* a = consecutive failed status reads (UINT32_MAX: forever) */
};

struct scn_event {
	int64_t at_ms;
	enum scn_kind kind;
	const char *path;
	int64_t a;
	int64_t b;
};

/** Fresh fakes (blank storage, unset clock, full battery) and a cold boot. */
void scn_init(void);

/** Home counties for the scenarios; call after scn_init(). */
void scn_set_home(const char *const *pssccc, size_t n);

/** Install a test event table: TOR, SVR, TOA, RWT, RMT. */
void scn_use_test_events(void);

/** Play events in time order, then idle until until_ms. */
void scn_run(const struct scn_event *events, size_t n, int64_t until_ms);

/** Idle (heartbeats only) until an absolute time. */
void scn_idle_until(int64_t at_ms);

struct radio *scn_radio(void);
uint32_t scn_boots(void);
bool scn_device_off(void);

#endif /* TESTS_SCENARIO_RUNNER_H_ */
