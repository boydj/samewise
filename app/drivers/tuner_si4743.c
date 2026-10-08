/*
 * hal/tuner.h on the Skyworks (Silicon Labs) Si4743 over I2C.
 *
 * Every command, argument, response byte and property here is from AN332
 * "Si47xx Programming Guide" Rev. 1.2 (Silicon Labs, May 2020):
 *   - section 4 and 6.1: a command is one write of up to 8 bytes (command,
 *     then arguments); the reply is a read of the status byte and up to 15
 *     response bytes; poll the status byte until CTS (bit 7); ERR (bit 6)
 *     means an invalid command or argument;
 *   - section 5.2 (FM, FUNC 0), 5.3 (AM, FUNC 1) and 5.4 (WB, FUNC 3, the
 *     chapter for Si4707/36/37/38/39/42/43): POWER_UP, GET_REV,
 *     POWER_DOWN, SET_PROPERTY, GET_INT_STATUS and each band's TUNE_FREQ,
 *     SEEK_START, TUNE_STATUS and RSQ_STATUS; FM_RDS_STATUS;
 *   - Tables 50-52: command timing (CTS 300 us, POWER_UP 110 ms; tune
 *     complete 60 ms FM, 80 ms AM, 250 ms WB; seek 60-200 ms per channel).
 * RDS radio text (groups 2A and 2B) is laid out by the RDS standard, IEC
 * 62106, not AN332: block B holds the group type (bits 15:12), version B
 * (bit 11), the text A/B flag (bit 4) and the segment address (bits 3:0).
 *
 * A tune doesn't wait for the chip to finish (up to 250 ms on WB): it
 * returns once the command is accepted, and the next status read collects
 * the seek/tune-complete flag. The health supervisor tunes from a timer
 * that holds the app lock, which must not wait that long. A seek does
 * wait, for up to the whole band; it is for the listening UI only.
 */

#include <errno.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/kernel.h>

#include "hal/tuner.h"

#define NODE DT_INST(0, skyworks_si4743)

static const struct i2c_dt_spec bus = I2C_DT_SPEC_GET(NODE);
#if DT_NODE_HAS_PROP(NODE, reset_gpios)
static const struct gpio_dt_spec rst = GPIO_DT_SPEC_GET(NODE, reset_gpios);
#endif

/* Commands (AN332 Tables 8, 12 and 17). */
#define CMD_POWER_UP       0x01U
#define CMD_GET_REV        0x10U
#define CMD_POWER_DOWN     0x11U
#define CMD_SET_PROPERTY   0x12U
#define CMD_GET_INT_STATUS 0x14U
#define CMD_FM_TUNE_FREQ   0x20U
#define CMD_FM_SEEK_START  0x21U
#define CMD_FM_TUNE_STATUS 0x22U
#define CMD_FM_RSQ_STATUS  0x23U
#define CMD_FM_RDS_STATUS  0x24U
#define CMD_AM_TUNE_FREQ   0x40U
#define CMD_AM_SEEK_START  0x41U
#define CMD_AM_TUNE_STATUS 0x42U
#define CMD_AM_RSQ_STATUS  0x43U
#define CMD_WB_TUNE_FREQ   0x50U
#define CMD_WB_TUNE_STATUS 0x52U
#define CMD_WB_RSQ_STATUS  0x53U

/* Status byte (Tables 10, 14 and 19). */
#define STATUS_CTS    0x80U
#define STATUS_ERR    0x40U
#define STATUS_STCINT 0x01U

/* POWER_UP: FUNC in ARG1 bits 3:0, XOSCEN (bit 4) 0 on Si474x; OPMODE analog out. */
#define FUNC_FM          0x00U
#define FUNC_AM          0x01U
#define FUNC_WB          0x03U
#define OPMODE_ANALOG    0x05U
#define PN_SI4743        0x2BU /* GET_REV RESP1: last two digits of the part number, hex */

/* Properties. */
#define PROP_RX_HARD_MUTE  0x4001U /* LMUTE bit 1, RMUTE bit 0 */
#define PROP_FM_RDS_CONFIG 0x1502U
/* RDSEN, and keep groups whose blocks needed at most 3-5 corrected bits (2 per block). */
#define RDS_CONFIG         0xAA01U

/* SEEK_START ARG1 and TUNE_STATUS RESP1. */
#define SEEK_UP   0x08U
#define SEEK_WRAP 0x04U
#define INTACK    0x01U
#define RESP_BLTF 0x80U

/* Timing (Tables 50-52), with margin. */
#define CTS_MS        5
#define POWER_UP_MS   200
#define SEEK_STEP_MS  200 /* worst-case per channel */
#define FM_CHANNELS   205 /* (10790 - 8750) / 10 + 1 at the default seek band and spacing */
#define AM_CHANNELS   120 /* (1710 - 520) / 10 + 1 */

/* Band limits in kHz (sections 5.2.1, 5.3.1 and 5.4.1). */
#define FM_MIN_KHZ 64000U
#define FM_MAX_KHZ 108000U
#define AM_MIN_KHZ 520U
#define AM_MAX_KHZ 1710U
#define WB_MIN_KHZ 162400U
#define WB_MAX_KHZ 162550U

#define RT_MAX 64U

static struct {
	bool powered;
	enum hal_tuner_band band;
	uint32_t khz;
	bool tune_pending;
	bool band_mute;
	bool user_mute;
	/* RDS radio text. */
	char rt[RT_MAX];
	uint16_t rt_seen; /* segments received, one bit each */
	int8_t rt_ab;     /* text A/B flag, -1 before the first group */
	bool rt_2b;       /* version B: 2 characters a segment */
} s = {.band = HAL_TUNER_BAND_WB, .rt_ab = -1};

/* ---- Transport ---- */

/*
 * Poll the status byte until CTS. Most commands take 300 us, so the first
 * 2 ms poll every 100 us without giving up the CPU; after that (POWER_UP,
 * or a chip that isn't answering) sleep between polls until the deadline.
 */
#define FAST_POLLS   20
#define FAST_POLL_US 100

static int wait_cts(uint8_t *reply, size_t len, int timeout_ms)
{
	int64_t end = k_uptime_get() + timeout_ms;

	for (int i = 0;; i++) {
		if (i2c_read_dt(&bus, reply, len) != 0) {
			return -EIO;
		}
		if ((reply[0] & STATUS_CTS) != 0U) {
			return (reply[0] & STATUS_ERR) != 0U ? -EIO : 0;
		}
		if (i < FAST_POLLS) {
			k_busy_wait(FAST_POLL_US);
		} else if (k_uptime_get() >= end) {
			return -EIO;
		} else {
			k_msleep(1);
		}
	}
}

/* Send a command and wait for CTS; resp gets RESP1.. (resp_len bytes). */
static int command(const uint8_t *cmd, size_t len, uint8_t *resp, size_t resp_len, int timeout_ms)
{
	uint8_t reply[16];
	int err;

	if (len > 8U || resp_len > 15U) {
		return -EINVAL;
	}
	if (i2c_write_dt(&bus, cmd, len) != 0) {
		return -EIO;
	}
	err = wait_cts(reply, 1U + resp_len, timeout_ms);
	if (err == 0 && resp_len > 0U) {
		memcpy(resp, &reply[1], resp_len);
	}
	return err;
}

static int set_property(uint16_t prop, uint16_t value)
{
	const uint8_t cmd[] = {CMD_SET_PROPERTY, 0,
			       (uint8_t)(prop >> 8), (uint8_t)prop,
			       (uint8_t)(value >> 8), (uint8_t)value};

	return command(cmd, sizeof(cmd), NULL, 0, CTS_MS);
}

static int apply_mute(void)
{
	return set_property(PROP_RX_HARD_MUTE, (s.band_mute || s.user_mute) ? 0x0003U : 0x0000U);
}

/* ---- RDS radio text (IEC 62106 groups 2A and 2B) ---- */

static void rt_clear(void)
{
	memset(s.rt, ' ', sizeof(s.rt));
	s.rt_seen = 0;
	s.rt_ab = -1;
}

static void rt_group(uint16_t b, uint16_t c, uint16_t d)
{
	uint8_t addr = (uint8_t)(b & 0x0FU);
	int8_t ab = (int8_t)((b >> 4) & 1U);
	bool version_b = ((b >> 11) & 1U) != 0U;

	if ((b >> 12) != 2U) {
		return; /* not radio text */
	}
	if (ab != s.rt_ab || version_b != s.rt_2b) {
		rt_clear(); /* the station started a new text */
		s.rt_ab = ab;
		s.rt_2b = version_b;
	}
	if (version_b) {
		s.rt[addr * 2U] = (char)(d >> 8);
		s.rt[addr * 2U + 1U] = (char)d;
	} else {
		s.rt[addr * 4U] = (char)(c >> 8);
		s.rt[addr * 4U + 1U] = (char)c;
		s.rt[addr * 4U + 2U] = (char)(d >> 8);
		s.rt[addr * 4U + 3U] = (char)d;
	}
	s.rt_seen |= (uint16_t)(1U << addr);
}

/* ---- Power and band ---- */

static uint8_t func_of(enum hal_tuner_band band)
{
	return band == HAL_TUNER_BAND_FM ? FUNC_FM : band == HAL_TUNER_BAND_AM ? FUNC_AM : FUNC_WB;
}

static int power_up(void)
{
	const uint8_t cmd[] = {CMD_POWER_UP, func_of(s.band), OPMODE_ANALOG};
	const uint8_t rev[] = {CMD_GET_REV};
	uint8_t resp[8];
	int err;

	err = command(cmd, sizeof(cmd), NULL, 0, POWER_UP_MS);
	if (err == 0) {
		err = command(rev, sizeof(rev), resp, sizeof(resp), CTS_MS);
	}
	if (err != 0) {
		return err;
	}
	if (resp[0] != PN_SI4743) {
		return -ENODEV;
	}
	s.powered = true;
	s.tune_pending = false;
	s.band_mute = true; /* until the first tune in this band */
	rt_clear();
	if (s.band == HAL_TUNER_BAND_FM) {
		err = set_property(PROP_FM_RDS_CONFIG, RDS_CONFIG);
	}
	return err == 0 ? apply_mute() : err;
}

static int power_down(void)
{
	const uint8_t cmd[] = {CMD_POWER_DOWN};

	s.powered = false;
	return command(cmd, sizeof(cmd), NULL, 0, CTS_MS);
}

int hal_tuner_power(bool on)
{
	if (!device_is_ready(bus.bus)) {
		return -ENODEV;
	}
	if (!on) {
		return s.powered ? power_down() : 0;
	}
#if DT_NODE_HAS_PROP(NODE, reset_gpios)
	/* Reset into 2-wire mode (section 6: GPO1 high, GPO2 low by default). */
	if (gpio_is_ready_dt(&rst)) {
		(void)gpio_pin_configure_dt(&rst, GPIO_OUTPUT_ACTIVE);
		k_busy_wait(100);
		(void)gpio_pin_set_dt(&rst, 0);
		k_busy_wait(100);
	}
#endif
	s.powered = false;
	return power_up();
}

int hal_tuner_set_band(enum hal_tuner_band band)
{
	int err;

	if (band != HAL_TUNER_BAND_FM && band != HAL_TUNER_BAND_AM && band != HAL_TUNER_BAND_WB) {
		return -EINVAL;
	}
	if (!s.powered) {
		s.band = band; /* takes effect at power-up */
		return 0;
	}
	if (band == s.band) {
		return 0;
	}
	/* A new function needs POWER_DOWN, then POWER_UP (section 5.2.1, note). */
	err = power_down();
	s.band = band;
	if (err == 0) {
		err = power_up();
	}
	return err;
}

/* ---- Tuning ---- */

static bool in_band(uint32_t khz)
{
	switch (s.band) {
	case HAL_TUNER_BAND_FM:
		return khz >= FM_MIN_KHZ && khz <= FM_MAX_KHZ && khz % 10U == 0U;
	case HAL_TUNER_BAND_AM:
		return khz >= AM_MIN_KHZ && khz <= AM_MAX_KHZ;
	case HAL_TUNER_BAND_WB:
		/* 2.5 kHz units: 162.4 MHz = 64960, 162.55 MHz = 65020 (section 5.4.1). */
		return khz >= WB_MIN_KHZ && khz <= WB_MAX_KHZ && (khz * 2U) % 5U == 0U;
	}
	return false;
}

int hal_tuner_tune(uint32_t freq_khz)
{
	uint8_t cmd[6];
	size_t len;
	uint16_t units;
	int err;

	if (!s.powered) {
		return -EIO;
	}
	if (!in_band(freq_khz)) {
		return -EINVAL;
	}
	switch (s.band) {
	case HAL_TUNER_BAND_FM: /* ARG1 0, FREQ in 10 kHz, ANTCAP 0 (automatic) */
		units = (uint16_t)(freq_khz / 10U);
		cmd[0] = CMD_FM_TUNE_FREQ;
		cmd[4] = 0;
		len = 5;
		break;
	case HAL_TUNER_BAND_AM: /* ARG1 0, FREQ in kHz, ANTCAP 0 (automatic) */
		units = (uint16_t)freq_khz;
		cmd[0] = CMD_AM_TUNE_FREQ;
		cmd[4] = 0;
		cmd[5] = 0;
		len = 6;
		break;
	case HAL_TUNER_BAND_WB: /* ARG1 0, FREQ in 2.5 kHz */
	default:
		units = (uint16_t)(freq_khz * 2U / 5U);
		cmd[0] = CMD_WB_TUNE_FREQ;
		len = 4;
		break;
	}
	cmd[1] = 0;
	cmd[2] = (uint8_t)(units >> 8);
	cmd[3] = (uint8_t)units;
	err = command(cmd, len, NULL, 0, CTS_MS);
	if (err != 0) {
		return err;
	}
	s.khz = freq_khz;
	s.tune_pending = true;
	rt_clear();
	if (s.band_mute) {
		s.band_mute = false;
		return apply_mute();
	}
	return 0;
}

/* TUNE_STATUS with INTACK: clears STCINT; returns the frequency in kHz and BLTF. */
static int tune_status(uint32_t *khz, bool *bltf)
{
	uint8_t cmd[2] = {0, INTACK};
	uint8_t resp[5];
	uint16_t units;
	int err;

	cmd[0] = s.band == HAL_TUNER_BAND_FM   ? CMD_FM_TUNE_STATUS
		 : s.band == HAL_TUNER_BAND_AM ? CMD_AM_TUNE_STATUS
					       : CMD_WB_TUNE_STATUS;
	err = command(cmd, sizeof(cmd), resp, sizeof(resp), CTS_MS);
	if (err != 0) {
		return err;
	}
	units = (uint16_t)(resp[1] << 8 | resp[2]);
	*khz = s.band == HAL_TUNER_BAND_FM   ? units * 10U
	       : s.band == HAL_TUNER_BAND_AM ? units
					     : (uint32_t)units * 5U / 2U;
	*bltf = (resp[0] & RESP_BLTF) != 0U;
	return 0;
}

static int stc_done(bool *done)
{
	const uint8_t cmd[] = {CMD_GET_INT_STATUS};
	uint8_t status;
	int err;

	if (i2c_write_dt(&bus, cmd, sizeof(cmd)) != 0) {
		return -EIO;
	}
	err = wait_cts(&status, 1, CTS_MS);
	*done = err == 0 && (status & STATUS_STCINT) != 0U;
	return err;
}

/* If a tune is outstanding and has finished, acknowledge it and read the frequency. */
static int collect_tune(void)
{
	bool done;
	bool bltf;
	uint32_t khz;
	int err;

	if (!s.tune_pending) {
		return 0;
	}
	err = stc_done(&done);
	if (err != 0 || !done) {
		return err;
	}
	err = tune_status(&khz, &bltf);
	if (err == 0) {
		s.khz = khz;
		s.tune_pending = false;
	}
	return err;
}

int hal_tuner_seek(bool up, uint32_t *found_khz)
{
	uint8_t cmd[6] = {0};
	size_t len;
	int64_t end;
	uint32_t khz;
	bool bltf;
	bool done = false;
	int err;

	if (!s.powered) {
		return -EIO;
	}
	if (s.band == HAL_TUNER_BAND_WB) {
		return -ENOTSUP; /* the WB receiver has no seek (Table 17) */
	}
	cmd[0] = s.band == HAL_TUNER_BAND_FM ? CMD_FM_SEEK_START : CMD_AM_SEEK_START;
	cmd[1] = (uint8_t)((up ? SEEK_UP : 0U) | SEEK_WRAP);
	len = s.band == HAL_TUNER_BAND_FM ? 2U : 6U; /* AM: ARG2-3 0, ANTCAP 0 */
	err = command(cmd, len, NULL, 0, CTS_MS);
	if (err != 0) {
		return err;
	}
	rt_clear();
	end = k_uptime_get() +
	      (int64_t)SEEK_STEP_MS * (s.band == HAL_TUNER_BAND_FM ? FM_CHANNELS : AM_CHANNELS);
	while (!done) {
		err = stc_done(&done);
		if (err != 0) {
			return err;
		}
		if (!done) {
			if (k_uptime_get() >= end) {
				return -EIO;
			}
			k_msleep(10);
		}
	}
	err = tune_status(&khz, &bltf);
	if (err != 0) {
		return err;
	}
	s.khz = khz;
	s.tune_pending = false;
	if (bltf) {
		return -ENOENT; /* wrapped back to where it started */
	}
	*found_khz = khz;
	if (s.band_mute) {
		s.band_mute = false;
		return apply_mute();
	}
	return 0;
}

/* ---- Status, mute, RDS ---- */

int hal_tuner_get_status(struct hal_tuner_status *st)
{
	uint8_t cmd[2] = {0, 0};
	uint8_t resp[7];
	size_t resp_len;
	int err;

	memset(st, 0, sizeof(*st));
	if (!s.powered) {
		return -EIO;
	}
	err = collect_tune();
	if (err != 0) {
		return err;
	}
	cmd[0] = s.band == HAL_TUNER_BAND_FM   ? CMD_FM_RSQ_STATUS
		 : s.band == HAL_TUNER_BAND_AM ? CMD_AM_RSQ_STATUS
					       : CMD_WB_RSQ_STATUS;
	resp_len = s.band == HAL_TUNER_BAND_AM ? 5U : 7U;
	err = command(cmd, sizeof(cmd), resp, resp_len, CTS_MS);
	if (err != 0) {
		return err;
	}
	/* RESP4 RSSI (dBuV), RESP5 SNR (dB), FM RESP3 bit 7 PILOT. */
	st->valid = true;
	st->freq_khz = s.khz;
	st->rssi_dbuv = resp[3];
	st->snr_db = resp[4];
	st->stereo = s.band == HAL_TUNER_BAND_FM && (resp[2] & 0x80U) != 0U;
	return 0;
}

int hal_tuner_mute(bool mute)
{
	s.user_mute = mute;
	return s.powered ? apply_mute() : 0;
}

int hal_tuner_rds_text(char *buf, size_t len)
{
	const uint8_t cmd[] = {CMD_FM_RDS_STATUS, INTACK};
	uint8_t resp[12];
	size_t n = 0;
	size_t max = s.rt_2b ? 32U : RT_MAX;

	if (len == 0U) {
		return -EINVAL;
	}
	buf[0] = '\0';
	if (!s.powered) {
		return -EIO;
	}
	if (s.band != HAL_TUNER_BAND_FM) {
		return 0;
	}
	/* Drain the FIFO: 25 groups at most (section 5.2.1, FM_RDS_STATUS). */
	for (int i = 0; i < 26; i++) {
		int err = command(cmd, sizeof(cmd), resp, sizeof(resp), CTS_MS);

		if (err != 0) {
			return err;
		}
		if (resp[2] == 0U) {
			break; /* RDSFIFOUSED: empty, no group in the blocks */
		}
		rt_group((uint16_t)(resp[5] << 8 | resp[6]), (uint16_t)(resp[7] << 8 | resp[8]),
			 (uint16_t)(resp[9] << 8 | resp[10]));
	}
	/* The text runs to a carriage return, or up to the first missing segment. */
	for (size_t i = 0; i < max && n + 1U < len; i++) {
		size_t seg = s.rt_2b ? i / 2U : i / 4U;

		if ((s.rt_seen & (1U << seg)) == 0U || s.rt[i] == '\r') {
			break;
		}
		buf[n++] = s.rt[i];
	}
	while (n > 0U && buf[n - 1U] == ' ') {
		n--;
	}
	buf[n] = '\0';
	return (int)n;
}
