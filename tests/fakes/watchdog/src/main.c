/*
 * watchdog fake: feeds keep it alive; starvation for the timeout triggers
 * exactly one simulated reset, with HAL_RESET_WATCHDOG as the next reason.
 */

#include <zephyr/ztest.h>

#include "fakes/clock_fake.h"
#include "fakes/watchdog_fake.h"
#include "hal/clock.h"
#include "hal/watchdog.h"

static int resets_seen;
static int64_t reset_at;

static void on_reset(void *user)
{
	ARG_UNUSED(user);
	resets_seen++;
	reset_at = hal_clock_uptime_ms();
	clock_fake_simulate_reset();
}

static void before(void *f)
{
	ARG_UNUSED(f);
	clock_fake_reset();
	watchdog_fake_init(on_reset, NULL);
	resets_seen = 0;
	reset_at = -1;
}

ZTEST_SUITE(watchdog_fake, NULL, NULL, before, NULL, NULL);

ZTEST(watchdog_fake, test_power_on_state)
{
	zassert_false(watchdog_fake_running());
	zassert_equal(hal_watchdog_reset_reason(), HAL_RESET_POWER_ON);
	zassert_equal(hal_watchdog_feed(), -EINVAL, "feed before start");
	zassert_equal(hal_watchdog_start(0), -EINVAL);
}

ZTEST(watchdog_fake, test_cannot_be_reconfigured)
{
	zassert_ok(hal_watchdog_start(HAL_WATCHDOG_TIMEOUT_MS));
	zassert_equal(hal_watchdog_start(1000), -EALREADY);
}

ZTEST(watchdog_fake, test_regular_feeds_keep_it_alive)
{
	zassert_ok(hal_watchdog_start(HAL_WATCHDOG_TIMEOUT_MS));
	for (int i = 0; i < 3600; i++) {
		clock_fake_advance_ms(1000);
		zassert_ok(hal_watchdog_feed());
	}
	zassert_equal(resets_seen, 0);
	zassert_equal(watchdog_fake_feeds(), 3600);
	zassert_equal(watchdog_fake_last_feed_ms(), 3600 * 1000);
}

ZTEST(watchdog_fake, test_starvation_resets_once_after_timeout)
{
	zassert_ok(hal_watchdog_start(HAL_WATCHDOG_TIMEOUT_MS));
	clock_fake_advance_ms(5000);
	zassert_ok(hal_watchdog_feed());
	clock_fake_advance_ms(HAL_WATCHDOG_TIMEOUT_MS - 1);
	zassert_equal(resets_seen, 0, "not before the timeout");
	clock_fake_advance_ms(60000);
	zassert_equal(resets_seen, 1, "exactly one reset");
	zassert_equal(reset_at, 5000 + HAL_WATCHDOG_TIMEOUT_MS);
	zassert_equal(watchdog_fake_resets(), 1);
	zassert_equal(hal_watchdog_reset_reason(), HAL_RESET_WATCHDOG);
	zassert_false(watchdog_fake_running(), "stopped until the app starts it");
}

ZTEST(watchdog_fake, test_restart_after_reset)
{
	zassert_ok(hal_watchdog_start(HAL_WATCHDOG_TIMEOUT_MS));
	clock_fake_advance_ms(10000);
	zassert_equal(resets_seen, 1);

	/* The rebooted application starts it again and keeps it fed. */
	zassert_ok(hal_watchdog_start(HAL_WATCHDOG_TIMEOUT_MS));
	for (int i = 0; i < 100; i++) {
		clock_fake_advance_ms(1000);
		zassert_ok(hal_watchdog_feed());
	}
	zassert_equal(resets_seen, 1);
	zassert_equal(hal_watchdog_reset_reason(), HAL_RESET_WATCHDOG, "reason of this boot");
}
