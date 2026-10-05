/*
 * Power manager: the 32 MHz crystal runs while any client holds it.
 */

#include <zephyr/ztest.h>

#include "fakes/clock_fake.h"
#include "services/power/power.h"

static void before(void *f)
{
	(void)f;
	clock_fake_reset();
	power_init();
}

ZTEST(power, test_crystal_follows_clients)
{
	zassert_false(clock_fake_hfxo_on(), "off at boot");

	power_hfxo_request(POWER_HFXO_BLE_WINDOW);
	zassert_true(clock_fake_hfxo_on());
	power_hfxo_request(POWER_HFXO_BLE_CONN);
	power_hfxo_request(POWER_HFXO_BLE_CONN);
	zassert_equal(clock_fake_hfxo_starts(), 1, "started once");

	power_hfxo_release(POWER_HFXO_BLE_WINDOW);
	zassert_true(clock_fake_hfxo_on(), "still connected");
	power_hfxo_release(POWER_HFXO_BLE_WINDOW);
	zassert_true(power_hfxo_held());
	power_hfxo_release(POWER_HFXO_BLE_CONN);
	zassert_false(clock_fake_hfxo_on());
	zassert_false(power_hfxo_held());
	power_hfxo_release(POWER_HFXO_BLE_CONN);
	zassert_equal(clock_fake_hfxo_misuse(), 0, "never double-started or double-stopped");
}

ZTEST_SUITE(power, NULL, NULL, before, NULL, NULL);
