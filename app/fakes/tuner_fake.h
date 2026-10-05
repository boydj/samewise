/*
 * tuner fake: scripted signal and faults; records power, band and tune calls.
 */

#ifndef FAKES_TUNER_FAKE_H_
#define FAKES_TUNER_FAKE_H_

#include <stdbool.h>
#include <stdint.h>

#include "hal/tuner.h"

#ifdef __cplusplus
extern "C" {
#endif

enum tuner_fake_fault {
	TUNER_FAKE_IO,      /* the call fails with -EIO (I2C error) */
	TUNER_FAKE_INVALID, /* status reads back with valid = false */
};

/** Powered off, weather band, 162550 kHz, RSSI 40 dBuV, SNR 25 dB, no faults. */
void tuner_fake_init(void);

void tuner_fake_set_signal(int16_t rssi_dbuv, int16_t snr_db);

/** Fail the next n status reads (n = UINT32_MAX: until cleared with n = 0). */
void tuner_fake_fail_status(uint32_t n, enum tuner_fake_fault kind);

/** Make the power-up during a tuner reset fail (the reset can't fix it). */
void tuner_fake_fail_power_up(bool fail);

uint32_t tuner_fake_power_ups(void);
uint32_t tuner_fake_tunes(void);
uint32_t tuner_fake_last_tune_khz(void);
uint32_t tuner_fake_status_reads(void);
bool tuner_fake_powered(void);

#ifdef __cplusplus
}
#endif

#endif /* FAKES_TUNER_FAKE_H_ */
