/*
 * Battery interface: MAX17048 fuel gauge and BQ25180 charger over I2C.
 * The fake plays scripted discharge and charge curves.
 */

#ifndef HAL_BATTERY_H_
#define HAL_BATTERY_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum hal_battery_charge {
	HAL_BATTERY_DISCHARGING,
	HAL_BATTERY_CHARGING,
	HAL_BATTERY_CHARGED,
};

struct hal_battery_status {
	uint8_t soc_percent; /* state of charge, 0-100 */
	uint16_t voltage_mv; /* cell voltage */
	enum hal_battery_charge charge;
	bool temp_fault;     /* charger reported a temperature fault */
};

/** Read the fuel gauge and charger. */
int hal_battery_get(struct hal_battery_status *status);

/**
 * Put the charger into ship mode, disconnecting the battery. Used once the
 * cell reaches 3.3 V; does not return on the board.
 */
int hal_battery_ship_mode(void);

#ifdef __cplusplus
}
#endif

#endif /* HAL_BATTERY_H_ */
