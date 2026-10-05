/*
 * Negative audio: 10 minutes each of white noise, silence, the 1050 Hz NOAA
 * Weather Radio alarm tone and the EAS two-tone attention signal (853 and
 * 960 Hz together, 47 CFR 11.31(a)(2)) must produce no headers and no EOMs.
 */

#include <zephyr/ztest.h>

#include "decode_util.h"
#include "vectors.h"

static struct decode_result r;

static void assert_quiet(const char *name)
{
	const struct test_vector *v = test_vector_find(name);
	size_t block = 160;

	zassert_not_null(v);
	zassert_ok(decode_wav(v->wav, block_fixed, &block, true, &r));
	zassert_equal(r.samples, SAME_MS_TO_SAMPLES(10U * 60U * 1000U), "%s: 10 minutes", name);
	zassert_equal(r.n_headers, 0, "%s: false header", name);
	zassert_equal(r.n_eoms, 0, "%s: false EOM", name);
	zassert_equal(r.stats.header_copies + r.stats.eom_copies, 0, "%s: false copy", name);
	TC_PRINT("%s: %u preamble locks, %u aborted frames\n", name, r.stats.preamble_locks,
		 r.stats.frame_aborts);
}

ZTEST(same_negative, test_white_noise_10min)
{
	assert_quiet("noise_10min");
}

ZTEST(same_negative, test_silence_10min)
{
	assert_quiet("silence_10min");
}

ZTEST(same_negative, test_1050hz_tone_10min)
{
	assert_quiet("tone1050_10min");
}

ZTEST(same_negative, test_eas_two_tone_attention_10min)
{
	assert_quiet("eas_attention_10min");
}

ZTEST_SUITE(same_negative, NULL, NULL, NULL, NULL, NULL);
