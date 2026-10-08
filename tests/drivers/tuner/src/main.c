/*
 * drivers/tuner_si4743.c against the Si4743 emulator on native_sim's
 * emulated I2C bus: power-up and part check, each band's tune encoding and
 * limits, the seek/tune-complete handshake, seek found and not found,
 * signal status, mute, RDS radio text and every way the chip can fail.
 */

#include <string.h>

#include <zephyr/drivers/i2c.h>
#include <zephyr/ztest.h>

#include "emul_si4743.h"
#include "hal/tuner.h"

static const struct i2c_dt_spec chip = I2C_DT_SPEC_GET(DT_NODELABEL(si4743));

static void before(void *f)
{
	ARG_UNUSED(f);
	emul_si4743_reset();
	(void)hal_tuner_power(false); /* the driver forgets any earlier state */
	zassert_ok(hal_tuner_set_band(HAL_TUNER_BAND_WB));
	zassert_ok(hal_tuner_power(true));
}

ZTEST_SUITE(si4743, NULL, NULL, before, NULL, NULL);

static void assert_last(const uint8_t *want, uint8_t len)
{
	uint8_t n;
	const uint8_t *got = emul_si4743_last(&n);

	zassert_equal(n, len, "command length");
	zassert_mem_equal(got, want, len);
}

/* The status after the tuner has finished the outstanding tune. */
static struct hal_tuner_status settled(void)
{
	struct hal_tuner_status st;

	for (int i = 0; i < 10; i++) {
		zassert_ok(hal_tuner_get_status(&st));
	}
	return st;
}

ZTEST(si4743, test_power_up_in_the_weather_band_and_check_the_part)
{
	zassert_true(emul_si4743_powered());
	zassert_equal(emul_si4743_func(), EMUL_FUNC_WB, "FUNC 3, WB receive");

	emul_si4743_reset();
	emul_si4743_set_part(0x2A); /* an Si4742: no RDS */
	zassert_equal(hal_tuner_power(true), -ENODEV, "not an Si4743");

	emul_si4743_set_part(0x2B);
	zassert_ok(hal_tuner_power(true));
	zassert_ok(hal_tuner_power(false));
	zassert_false(emul_si4743_powered());
}

ZTEST(si4743, test_weather_band_tune_in_2_5_khz_units)
{
	static const uint8_t tune_162550[] = {0x50, 0x00, 0xFD, 0xFC}; /* 65020 */
	struct hal_tuner_status st;

	emul_si4743_add_station(EMUL_FUNC_WB, 65020, 42, 23, false);
	zassert_ok(hal_tuner_tune(162400));
	zassert_equal(emul_si4743_units(), 64960, "162.4 MHz = 64960");
	zassert_ok(hal_tuner_tune(162550));
	assert_last(tune_162550, sizeof(tune_162550));
	zassert_ok(hal_tuner_tune(162550));
	st = settled();
	zassert_true(st.valid);
	zassert_equal(st.freq_khz, 162550);
	zassert_equal(st.rssi_dbuv, 42);
	zassert_equal(st.snr_db, 23);
	zassert_false(st.stereo);
}

ZTEST(si4743, test_tune_returns_before_it_completes)
{
	struct hal_tuner_status st;
	uint8_t n;

	zassert_ok(hal_tuner_tune(162400)); /* the band's first tune also unmutes */
	(void)settled();
	emul_si4743_stc_after(3);
	zassert_ok(hal_tuner_tune(162475));
	zassert_equal(emul_si4743_last(&n)[0], 0x50, "nothing after the tune command");
	zassert_ok(hal_tuner_get_status(&st));
	zassert_equal(st.freq_khz, 162475, "the frequency asked for, meanwhile");
	st = settled();
	zassert_equal(st.freq_khz, 162475, "and the chip's once it completes");
	zassert_ok(hal_tuner_get_status(&st));
	zassert_equal(emul_si4743_last(&n)[0], 0x53, "then only the RSQ status per read");
}

ZTEST(si4743, test_tune_limits_send_nothing)
{
	uint8_t before_len;
	const uint8_t *last = emul_si4743_last(&before_len);
	uint8_t first = last[0];
	uint8_t n;

	zassert_equal(hal_tuner_tune(162375), -EINVAL);
	zassert_equal(hal_tuner_tune(162575), -EINVAL);
	zassert_equal(hal_tuner_tune(162412), -EINVAL, "not on the 2.5 kHz grid");
	zassert_equal(emul_si4743_last(&n)[0], first, "no command sent");

	zassert_ok(hal_tuner_set_band(HAL_TUNER_BAND_FM));
	zassert_equal(hal_tuner_tune(63990), -EINVAL);
	zassert_equal(hal_tuner_tune(108010), -EINVAL);
	zassert_equal(hal_tuner_tune(98705), -EINVAL, "FM is in 10 kHz");
	zassert_ok(hal_tuner_tune(64000));
	zassert_ok(hal_tuner_tune(108000));

	zassert_ok(hal_tuner_set_band(HAL_TUNER_BAND_AM));
	zassert_equal(hal_tuner_tune(519), -EINVAL);
	zassert_equal(hal_tuner_tune(1711), -EINVAL);
	zassert_ok(hal_tuner_tune(520));
	zassert_ok(hal_tuner_tune(1710));
}

ZTEST(si4743, test_band_change_powers_down_then_up)
{
	static const uint8_t tune_98_7[] = {0x20, 0x00, 0x26, 0x8E, 0x00}; /* 9870 */
	static const uint8_t tune_1010[] = {0x40, 0x00, 0x03, 0xF2, 0x00, 0x00};

	zassert_equal(emul_si4743_power_ups(), 1);
	zassert_ok(hal_tuner_set_band(HAL_TUNER_BAND_FM));
	zassert_equal(emul_si4743_power_ups(), 2, "POWER_DOWN, then POWER_UP");
	zassert_equal(emul_si4743_func(), EMUL_FUNC_FM);
	zassert_equal(emul_si4743_property(0x1502), 0xAA01, "RDS on, up to 3-5 bit errors");
	zassert_ok(hal_tuner_tune(64000)); /* the band's first tune also unmutes */
	zassert_ok(hal_tuner_tune(98700));
	assert_last(tune_98_7, sizeof(tune_98_7));

	zassert_ok(hal_tuner_set_band(HAL_TUNER_BAND_AM));
	zassert_equal(emul_si4743_func(), EMUL_FUNC_AM);
	zassert_ok(hal_tuner_tune(520));
	zassert_ok(hal_tuner_tune(1010));
	assert_last(tune_1010, sizeof(tune_1010));
	zassert_ok(hal_tuner_set_band(HAL_TUNER_BAND_AM));
	zassert_equal(emul_si4743_power_ups(), 3, "the same band: no restart");
	zassert_equal(hal_tuner_set_band(7), -EINVAL);
}

ZTEST(si4743, test_mute_until_tuned_and_on_request)
{
	zassert_ok(hal_tuner_set_band(HAL_TUNER_BAND_FM));
	zassert_equal(emul_si4743_property(0x4001), 0x0003, "a new band starts muted");
	zassert_ok(hal_tuner_tune(98700));
	zassert_equal(emul_si4743_property(0x4001), 0x0000, "the first tune unmutes it");
	zassert_ok(hal_tuner_mute(true));
	zassert_equal(emul_si4743_property(0x4001), 0x0003, "LMUTE and RMUTE");
	zassert_ok(hal_tuner_tune(101100));
	zassert_equal(emul_si4743_property(0x4001), 0x0003, "a mute you asked for stays");
	zassert_ok(hal_tuner_mute(false));
	zassert_equal(emul_si4743_property(0x4001), 0x0000);
}

ZTEST(si4743, test_fm_status_and_stereo_pilot)
{
	struct hal_tuner_status st;

	emul_si4743_add_station(EMUL_FUNC_FM, 9870, 55, 31, true);
	emul_si4743_set_noise(8, 1);
	zassert_ok(hal_tuner_set_band(HAL_TUNER_BAND_FM));
	zassert_ok(hal_tuner_tune(98700));
	st = settled();
	zassert_equal(st.rssi_dbuv, 55);
	zassert_equal(st.snr_db, 31);
	zassert_true(st.stereo, "PILOT");
	zassert_ok(hal_tuner_tune(98900));
	st = settled();
	zassert_equal(st.rssi_dbuv, 8);
	zassert_false(st.stereo);
}

ZTEST(si4743, test_seek_finds_wraps_and_gives_up)
{
	uint32_t found = 0;

	emul_si4743_add_station(EMUL_FUNC_FM, 9010, 40, 20, false);
	emul_si4743_add_station(EMUL_FUNC_FM, 10190, 40, 20, false);
	zassert_ok(hal_tuner_set_band(HAL_TUNER_BAND_FM));
	zassert_ok(hal_tuner_tune(98700));
	(void)settled();
	emul_si4743_stc_after(5);
	zassert_ok(hal_tuner_seek(true, &found));
	zassert_equal(found, 101900);
	zassert_ok(hal_tuner_seek(true, &found));
	zassert_equal(found, 90100, "past the top, it wraps");
	zassert_ok(hal_tuner_seek(false, &found));
	zassert_equal(found, 101900, "down, wrapping past the bottom");

	zassert_ok(hal_tuner_set_band(HAL_TUNER_BAND_AM));
	zassert_ok(hal_tuner_tune(1010));
	found = 0;
	zassert_equal(hal_tuner_seek(true, &found), -ENOENT, "no AM station anywhere");
	zassert_equal(found, 0);
	zassert_equal(emul_si4743_units(), 1010, "back where it started");

	zassert_ok(hal_tuner_set_band(HAL_TUNER_BAND_WB));
	zassert_equal(hal_tuner_seek(true, &found), -ENOTSUP, "the WB receiver has no seek");
}

/* RDS group 2A or 2B (IEC 62106): block B = type 2, version, A/B flag, segment. */
static void rt_2a(uint8_t ab, uint8_t seg, const char *four)
{
	emul_si4743_push_rds(0x1234, (uint16_t)(0x2000 | ab << 4 | seg),
			     (uint16_t)(four[0] << 8 | four[1]), (uint16_t)(four[2] << 8 | four[3]),
			     0);
}

ZTEST(si4743, test_rds_radio_text)
{
	char buf[65];

	zassert_ok(hal_tuner_set_band(HAL_TUNER_BAND_FM));
	zassert_ok(hal_tuner_tune(98700));
	zassert_equal(hal_tuner_rds_text(buf, sizeof(buf)), 0, "nothing yet");

	rt_2a(0, 0, "WXYZ");
	rt_2a(0, 1, "-FM ");
	rt_2a(0, 2, "ROCK");
	rt_2a(0, 3, "\r   ");
	zassert_equal(hal_tuner_rds_text(buf, sizeof(buf)), 12);
	zassert_str_equal(buf, "WXYZ-FM ROCK", "up to the carriage return");

	rt_2a(1, 0, "NEWS");
	zassert_ok(hal_tuner_rds_text(buf, sizeof(buf)) < 0);
	zassert_str_equal(buf, "NEWS", "the A/B flag flipped: a new text");

	rt_2a(1, 2, "LATE");
	zassert_equal(hal_tuner_rds_text(buf, sizeof(buf)), 4, "stops at the missing segment 1");

	/* A group the chip judged too damaged (BLE 3 on block C) never arrives. */
	emul_si4743_push_rds(0x1234, 0x2011, 0x4040, 0x4040, 0x0C);
	zassert_equal(hal_tuner_rds_text(buf, sizeof(buf)), 4);

	/* Version B: two characters a segment, in block D. */
	emul_si4743_push_rds(0x1234, 0x2800, 0, 'H' << 8 | 'I', 0);
	emul_si4743_push_rds(0x1234, 0x2801, 0, '!' << 8 | '\r', 0);
	zassert_equal(hal_tuner_rds_text(buf, sizeof(buf)), 3);
	zassert_str_equal(buf, "HI!");

	zassert_equal(hal_tuner_rds_text(buf, 3), 2, "cut to the buffer");
	zassert_str_equal(buf, "HI");

	zassert_ok(hal_tuner_tune(101100));
	zassert_equal(hal_tuner_rds_text(buf, sizeof(buf)), 0, "a new station, no text");

	zassert_ok(hal_tuner_set_band(HAL_TUNER_BAND_WB));
	zassert_equal(hal_tuner_rds_text(buf, sizeof(buf)), 0, "no RDS off FM");
}

ZTEST(si4743, test_a_64_character_text)
{
	char buf[65];
	static const char text[] = "0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDE.";

	zassert_ok(hal_tuner_set_band(HAL_TUNER_BAND_FM));
	zassert_ok(hal_tuner_tune(98700));
	for (uint8_t seg = 0; seg < 16; seg++) {
		rt_2a(0, seg, &text[seg * 4]);
	}
	zassert_equal(hal_tuner_rds_text(buf, sizeof(buf)), 64);
	zassert_str_equal(buf, text);
}

ZTEST(si4743, test_failures_are_eio)
{
	struct hal_tuner_status st;
	int64_t t;

	/* The bus fails. */
	emul_si4743_fail(true);
	zassert_equal(hal_tuner_get_status(&st), -EIO);
	zassert_equal(hal_tuner_tune(162550), -EIO);
	emul_si4743_fail(false);

	/* CTS comes late: polled for, fine. Never: a bounded -EIO. */
	emul_si4743_cts_after(3);
	zassert_ok(hal_tuner_get_status(&st));
	emul_si4743_cts_after(-1);
	t = k_uptime_get();
	zassert_equal(hal_tuner_get_status(&st), -EIO);
	zassert_true(k_uptime_get() - t < 50, "gives up within milliseconds");
	emul_si4743_cts_after(0);

	/* ERR: the chip is in another function than the driver thinks. */
	{
		static const uint8_t down[] = {0x11};
		static const uint8_t up_am[] = {0x01, 0x01, 0x05};

		zassert_ok(i2c_write_dt(&chip, down, sizeof(down)));
		zassert_ok(i2c_write_dt(&chip, up_am, sizeof(up_am)));
		zassert_equal(hal_tuner_get_status(&st), -EIO, "WB_RSQ_STATUS in AM: ERR");
	}

	/* The chip lost power: it answers nothing but POWER_UP. */
	emul_si4743_reset();
	zassert_equal(hal_tuner_get_status(&st), -EIO);
	zassert_ok(hal_tuner_power(true), "and power-up brings it back");
	zassert_ok(hal_tuner_get_status(&st));
}

ZTEST(si4743, test_nothing_works_powered_down)
{
	struct hal_tuner_status st;
	char buf[8];
	uint32_t f;

	zassert_ok(hal_tuner_power(false));
	zassert_equal(hal_tuner_tune(162550), -EIO);
	zassert_equal(hal_tuner_get_status(&st), -EIO);
	zassert_equal(hal_tuner_seek(true, &f), -EIO);
	zassert_equal(hal_tuner_rds_text(buf, sizeof(buf)), -EIO);
	zassert_ok(hal_tuner_mute(true), "remembered for the next power-up");
}
