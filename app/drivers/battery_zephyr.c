/*
 * hal/battery.h on the MAX17048 fuel gauge and the BQ25180 charger.
 *
 * Charge, voltage and time to empty come from Zephyr's MAX17048 driver
 * (fuel_gauge API); charging state from Zephyr's BQ25180 driver (charger
 * API). Two things that driver doesn't cover are read and written here
 * directly, per the BQ25180 datasheet (TI SLUSE99C, section 8.5.1):
 *   - temperature fault: STAT1 (0x01) TS_STAT, bits 4:3, 2b01 = "VTS <
 *     VHOT or VTS > VCOLD (charging suspended)" (Table 8-10). 2b10 and 2b11
 *     only reduce the charge current or voltage, so they aren't faults;
 *   - battery off: SHIP_RST (0x09) EN_RST_SHIP, bits 6:5, 2b01 = "shutdown
 *     mode with wake on adapter insert only" (Table 8-18): 15 nA from the
 *     cell (IBAT_SHUT), and only plugging in USB-C turns the radio back on,
 *     as the ALERTS OFF screen says.
 */

#include <errno.h>

#include <zephyr/device.h>
#include <zephyr/drivers/charger.h>
#include <zephyr/drivers/fuel_gauge.h>
#include <zephyr/drivers/i2c.h>

#include "hal/battery.h"

#define GAUGE_NODE   DT_INST(0, maxim_max17048)
#define CHARGER_NODE DT_INST(0, ti_bq25180)

#define BQ25180_STAT1             0x01U
#define BQ25180_STAT1_TS_STAT     0x18U /* bits 4:3 */
#define BQ25180_TS_SUSPENDED      0x08U /* 2b01 << 3 */
#define BQ25180_SHIP_RST          0x09U
#define BQ25180_EN_RST_SHIP       0x60U /* bits 6:5 */
#define BQ25180_SHUTDOWN_ON_INPUT 0x20U /* 2b01 << 5 */

static const struct device *const gauge = DEVICE_DT_GET(GAUGE_NODE);
static const struct device *const charger = DEVICE_DT_GET(CHARGER_NODE);
static const struct i2c_dt_spec charger_i2c = I2C_DT_SPEC_GET(CHARGER_NODE);

int hal_battery_get(struct hal_battery_status *status)
{
	fuel_gauge_prop_t props[] = {
		FUEL_GAUGE_RELATIVE_STATE_OF_CHARGE,
		FUEL_GAUGE_VOLTAGE,
		FUEL_GAUGE_RUNTIME_TO_EMPTY,
	};
	union fuel_gauge_prop_val vals[ARRAY_SIZE(props)];
	union charger_propval online;
	union charger_propval chg;
	uint8_t stat1;
	int err;

	if (!device_is_ready(gauge) || !device_is_ready(charger)) {
		return -ENODEV;
	}
	err = fuel_gauge_get_props(gauge, props, vals, ARRAY_SIZE(props));
	if (err != 0) {
		return -EIO;
	}
	if (charger_get_prop(charger, CHARGER_PROP_ONLINE, &online) != 0 ||
	    charger_get_prop(charger, CHARGER_PROP_STATUS, &chg) != 0 ||
	    i2c_reg_read_byte_dt(&charger_i2c, BQ25180_STAT1, &stat1) != 0) {
		return -EIO;
	}

	status->soc_percent = vals[0].relative_state_of_charge > 100U
				      ? 100U
				      : vals[0].relative_state_of_charge;
	status->voltage_mv = (uint16_t)(vals[1].voltage / 1000U);
	if (online.online == CHARGER_ONLINE_OFFLINE) {
		status->charge = HAL_BATTERY_DISCHARGING;
	} else if (chg.status == CHARGER_STATUS_CHARGING) {
		status->charge = HAL_BATTERY_CHARGING;
	} else if (chg.status == CHARGER_STATUS_FULL) {
		status->charge = HAL_BATTERY_CHARGED;
	} else {
		status->charge = HAL_BATTERY_DISCHARGING; /* plugged in but not charging */
	}
	status->temp_fault = (stat1 & BQ25180_STAT1_TS_STAT) == BQ25180_TS_SUSPENDED;

	/* The gauge's time to empty is in minutes, and 0 until it has a rate. */
	if (status->charge == HAL_BATTERY_DISCHARGING && vals[2].runtime_to_empty > 0U) {
		uint32_t h = vals[2].runtime_to_empty / 60U;

		status->hours_left = (uint16_t)(h >= HAL_BATTERY_HOURS_UNKNOWN
							? HAL_BATTERY_HOURS_UNKNOWN - 1U
							: h);
	} else {
		status->hours_left = HAL_BATTERY_HOURS_UNKNOWN;
	}
	return 0;
}

int hal_battery_ship_mode(void)
{
	if (!device_is_ready(charger_i2c.bus)) {
		return -ENODEV;
	}
	return i2c_reg_update_byte_dt(&charger_i2c, BQ25180_SHIP_RST, BQ25180_EN_RST_SHIP,
				      BQ25180_SHUTDOWN_ON_INPUT);
}
