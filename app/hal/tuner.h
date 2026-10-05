/*
 * Tuner interface: Si4743 on the board, a scripted fake on native_sim.
 *
 * Frequencies are in kHz for every band. Weather band channels 1 to 7 are
 * 162400, 162425, 162450, 162475, 162500, 162525 and 162550 kHz.
 *
 * All functions return 0 or a negative errno unless noted. A function that
 * fails with -EIO counts towards the health supervisor's tuner-fault rule
 * (3 consecutive I2C errors or invalid status).
 */

#ifndef HAL_TUNER_H_
#define HAL_TUNER_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum hal_tuner_band {
	HAL_TUNER_BAND_FM,
	HAL_TUNER_BAND_AM,
	HAL_TUNER_BAND_WB, /* NOAA weather band */
};

struct hal_tuner_status {
	bool valid;        /* false if the tuner reported an invalid status */
	uint32_t freq_khz; /* currently tuned frequency */
	int16_t rssi_dbuv; /* received signal strength */
	int16_t snr_db;    /* signal-to-noise ratio */
	bool stereo;       /* FM only */
};

/** Power the tuner up (on) or down; powering down loses the tuned station. */
int hal_tuner_power(bool on);

/** Select a band. Mutes audio until the next hal_tuner_tune(). */
int hal_tuner_set_band(enum hal_tuner_band band);

/** Tune to freq_khz in the current band; -EINVAL if out of range. */
int hal_tuner_tune(uint32_t freq_khz);

/**
 * Seek up or down from the current frequency to the next valid station.
 *
 * @param up        seek direction
 * @param found_khz set to the station found
 * @return 0, -ENOENT if the band was searched without finding a station,
 *         or another negative errno.
 */
int hal_tuner_seek(bool up, uint32_t *found_khz);

/** Read signal strength and SNR. Never blocks for longer than one I2C transfer. */
int hal_tuner_get_status(struct hal_tuner_status *status);

/** Mute or unmute the tuner's analog audio output. */
int hal_tuner_mute(bool mute);

/**
 * Copy the current RDS radio text (FM only) into buf, NUL-terminated.
 *
 * @return length of the text, 0 if none, or a negative errno.
 */
int hal_tuner_rds_text(char *buf, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* HAL_TUNER_H_ */
