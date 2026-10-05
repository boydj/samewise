/*
 * All nine hal/ headers compile together, stand alone (no Zephyr includes
 * needed) and agree with the SAME timing in the spec.
 */

#include "hal/alert_out.h"
#include "hal/audio_in.h"
#include "hal/audio_out.h"
#include "hal/battery.h"
#include "hal/clock.h"
#include "hal/display.h"
#include "hal/input.h"
#include "hal/storage.h"
#include "hal/tuner.h"

#include <zephyr/ztest.h>

/* 16 MHz / 1536 = 10,416.67 Hz */
BUILD_ASSERT(HAL_AUDIO_IN_RATE_MHZ == 10416667U);
/* 20 samples per bit gives 520.83 baud: 16e6 / 1536 / 20 = 520.833 */
BUILD_ASSERT(HAL_AUDIO_IN_RATE_NUM / HAL_AUDIO_IN_RATE_DEN / HAL_AUDIO_IN_SAMPLES_PER_BIT == 520U);
BUILD_ASSERT(HAL_AUDIO_IN_BLOCK_SAMPLES % HAL_AUDIO_IN_SAMPLES_PER_BIT == 0U);
BUILD_ASSERT(HAL_DISPLAY_FB_SIZE == 144U * 168U / 8U);

ZTEST(hal_headers, test_mark_and_space_are_whole_cycles_per_bit)
{
	/* Mark 2083.33 Hz = 4 cycles per bit, space 1562.5 Hz = 3 cycles per bit. */
	float bit_s = (float)HAL_AUDIO_IN_SAMPLES_PER_BIT * HAL_AUDIO_IN_RATE_DEN /
		      HAL_AUDIO_IN_RATE_NUM;

	zassert_within(2083.333f * bit_s, 4.0f, 0.001f);
	zassert_within(1562.5f * bit_s, 3.0f, 0.001f);
}

ZTEST_SUITE(hal_headers, NULL, NULL, NULL, NULL, NULL);
