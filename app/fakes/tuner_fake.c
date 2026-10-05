/*
 * tuner fake.
 */

#include <errno.h>
#include <string.h>

#include "fakes/tuner_fake.h"

static struct {
	bool powered;
	enum hal_tuner_band band;
	uint32_t freq_khz;
	int16_t rssi;
	int16_t snr;
	bool muted;
	uint32_t fail_left;
	enum tuner_fake_fault fail_kind;
	bool fail_power_up;
	uint32_t power_ups;
	uint32_t tunes;
	uint32_t last_tune;
	uint32_t status_reads;
} s;

void tuner_fake_init(void)
{
	memset(&s, 0, sizeof(s));
	s.band = HAL_TUNER_BAND_WB;
	s.freq_khz = 162550;
	s.rssi = 40;
	s.snr = 25;
}

void tuner_fake_set_signal(int16_t rssi_dbuv, int16_t snr_db)
{
	s.rssi = rssi_dbuv;
	s.snr = snr_db;
}

void tuner_fake_fail_status(uint32_t n, enum tuner_fake_fault kind)
{
	s.fail_left = n;
	s.fail_kind = kind;
}

void tuner_fake_fail_power_up(bool fail)
{
	s.fail_power_up = fail;
}

uint32_t tuner_fake_power_ups(void)
{
	return s.power_ups;
}

uint32_t tuner_fake_tunes(void)
{
	return s.tunes;
}

uint32_t tuner_fake_last_tune_khz(void)
{
	return s.last_tune;
}

uint32_t tuner_fake_status_reads(void)
{
	return s.status_reads;
}

bool tuner_fake_powered(void)
{
	return s.powered;
}

int hal_tuner_power(bool on)
{
	if (on) {
		if (s.fail_power_up) {
			return -EIO;
		}
		s.power_ups++;
	}
	s.powered = on;
	return 0;
}

int hal_tuner_set_band(enum hal_tuner_band band)
{
	if (!s.powered) {
		return -EIO;
	}
	s.band = band;
	return 0;
}

int hal_tuner_tune(uint32_t freq_khz)
{
	if (!s.powered) {
		return -EIO;
	}
	s.freq_khz = freq_khz;
	s.tunes++;
	s.last_tune = freq_khz;
	return 0;
}

int hal_tuner_seek(bool up, uint32_t *found_khz)
{
	(void)up;
	if (!s.powered) {
		return -EIO;
	}
	*found_khz = s.freq_khz;
	return 0;
}

int hal_tuner_get_status(struct hal_tuner_status *status)
{
	s.status_reads++;
	if (s.fail_left > 0U) {
		if (s.fail_left != UINT32_MAX) {
			s.fail_left--;
		}
		if (s.fail_kind == TUNER_FAKE_IO) {
			return -EIO;
		}
		memset(status, 0, sizeof(*status));
		return 0; /* valid = false */
	}
	if (!s.powered) {
		return -EIO;
	}
	status->valid = true;
	status->freq_khz = s.freq_khz;
	status->rssi_dbuv = s.rssi;
	status->snr_db = s.snr;
	status->stereo = false;
	return 0;
}

int hal_tuner_mute(bool mute)
{
	s.muted = mute;
	return 0;
}

int hal_tuner_rds_text(char *buf, size_t len)
{
	if (len > 0U) {
		buf[0] = '\0';
	}
	return 0;
}
