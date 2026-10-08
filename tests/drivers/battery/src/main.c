/*
 * drivers/battery_zephyr.c against Zephyr's MAX17048 emulator and our
 * BQ25180 emulator on native_sim's emulated I2C bus.
 *
 * Zephyr's MAX17048 emulator holds fixed charge and voltage registers and
 * writes its words little-endian, while the chip (and the driver) are
 * big-endian, so the driver reads its SOC 0x3525 as 0x2535 (37 %) and its
 * VCELL 0x4387 as 0x8743 (2,705 mV). The tests pin those, and swap the
 * charge rate they set so the driver reads the value meant.
 */

#include <zephyr/sys/byteorder.h>
#include <zephyr/ztest.h>

#include "emul_bq25180.h"
#include "hal/battery.h"

/* From drivers/fuel_gauge/max17048/emul_max17048.c (no header). */
void emul_max17048_set_crate_status(int value);

#define STAT0     0x00
#define STAT1     0x01
#define SHIP_RST  0x09
#define IC_CTRL   0x07
#define ICHG_CTRL 0x04

/* STAT0 (Table 8-9): CHG_STAT bits 6:5, VIN_PGOOD bit 0. */
#define VIN_GOOD      0x01
#define CHG_NOT       (0x0U << 5)
#define CHG_CC        (0x1U << 5)
#define CHG_CV        (0x2U << 5)
#define CHG_DONE      (0x3U << 5)

/* CRATE: 0.208 %/hour per LSB, signed (negative = discharging). */
static void set_crate(int16_t lsb)
{
	emul_max17048_set_crate_status((int)sys_cpu_to_be16((uint16_t)lsb));
}

static void before(void *f)
{
	ARG_UNUSED(f);
	emul_bq25180_fail(false);
	emul_bq25180_set(STAT0, CHG_NOT);
	emul_bq25180_set(STAT1, 0);
	emul_bq25180_set(SHIP_RST, 0x11);
	set_crate(0);
}

ZTEST_SUITE(battery_zephyr, NULL, NULL, before, NULL, NULL);

ZTEST(battery_zephyr, test_charger_set_up_at_boot)
{
	/* Zephyr's driver: watchdog off, 6-hour safety timer, TS auto (IC_CTRL),
	 * and 500 mA from the devicetree: 40 + (code - 31) * 10 mA (ICHG_CTRL). */
	zassert_equal(emul_bq25180_get(IC_CTRL) & 0x87, 0x87);
	zassert_equal(emul_bq25180_get(ICHG_CTRL) & 0x7F, 31 + (500 - 40) / 10);
}

ZTEST(battery_zephyr, test_discharging_with_the_gauges_rate)
{
	struct hal_battery_status st;

	set_crate(-10); /* -2.08 %/hour */
	zassert_ok(hal_battery_get(&st));
	zassert_equal(st.soc_percent, 37);
	zassert_equal(st.voltage_mv, 2705);
	zassert_equal(st.charge, HAL_BATTERY_DISCHARGING, "no input power");
	zassert_false(st.temp_fault);
	/* 37 % at 2.08 %/hour: 17.8 hours, which the driver gives as 1,067 minutes. */
	zassert_equal(st.hours_left, 17);
}

ZTEST(battery_zephyr, test_no_rate_yet_is_unknown)
{
	struct hal_battery_status st;

	zassert_ok(hal_battery_get(&st));
	zassert_equal(st.hours_left, HAL_BATTERY_HOURS_UNKNOWN, "a zero rate: not known yet");
}

ZTEST(battery_zephyr, test_charging_and_charged)
{
	struct hal_battery_status st;

	set_crate(100); /* charging */
	emul_bq25180_set(STAT0, VIN_GOOD | CHG_CC);
	zassert_ok(hal_battery_get(&st));
	zassert_equal(st.charge, HAL_BATTERY_CHARGING, "constant current");
	zassert_equal(st.hours_left, HAL_BATTERY_HOURS_UNKNOWN, "no time to empty while charging");

	emul_bq25180_set(STAT0, VIN_GOOD | CHG_CV);
	zassert_ok(hal_battery_get(&st));
	zassert_equal(st.charge, HAL_BATTERY_CHARGING, "constant voltage");

	emul_bq25180_set(STAT0, VIN_GOOD | CHG_DONE);
	zassert_ok(hal_battery_get(&st));
	zassert_equal(st.charge, HAL_BATTERY_CHARGED);

	emul_bq25180_set(STAT0, VIN_GOOD | CHG_NOT);
	set_crate(-10);
	zassert_ok(hal_battery_get(&st));
	zassert_equal(st.charge, HAL_BATTERY_DISCHARGING, "plugged in, not charging");
}

ZTEST(battery_zephyr, test_temperature_fault_only_when_charging_is_suspended)
{
	struct hal_battery_status st;

	/* STAT1 TS_STAT, bits 4:3 (Table 8-10). */
	emul_bq25180_set(STAT1, 0x1U << 3);
	zassert_ok(hal_battery_get(&st));
	zassert_true(st.temp_fault, "too hot or too cold: charging suspended");

	emul_bq25180_set(STAT1, 0x2U << 3);
	zassert_ok(hal_battery_get(&st));
	zassert_false(st.temp_fault, "cool: current reduced, not a fault");

	emul_bq25180_set(STAT1, 0x3U << 3);
	zassert_ok(hal_battery_get(&st));
	zassert_false(st.temp_fault, "warm: voltage reduced, not a fault");

	emul_bq25180_set(STAT1, 0xE7); /* every other bit set */
	zassert_ok(hal_battery_get(&st));
	zassert_false(st.temp_fault, "only TS_STAT counts");
}

ZTEST(battery_zephyr, test_shutdown_mode_wakes_only_on_usb)
{
	zassert_ok(hal_battery_ship_mode());
	/* SHIP_RST bits 6:5 = 2b01, the rest kept (Table 8-18). */
	zassert_equal(emul_bq25180_get(SHIP_RST), 0x11 | 0x20);

	emul_bq25180_set(SHIP_RST, 0x5F);
	zassert_ok(hal_battery_ship_mode());
	zassert_equal(emul_bq25180_get(SHIP_RST), 0x3F, "bits 6:5 replaced, not ORed");
}

ZTEST(battery_zephyr, test_a_charger_that_stops_answering_is_an_error)
{
	struct hal_battery_status st;

	emul_bq25180_fail(true);
	zassert_equal(hal_battery_get(&st), -EIO);
	zassert_not_equal(hal_battery_ship_mode(), 0);
}
