/*
 * battery fake: a scripted discharge or charge curve, piecewise linear in
 * simulated uptime, and a record of ship mode.
 */

#ifndef FAKES_BATTERY_FAKE_H_
#define FAKES_BATTERY_FAKE_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "hal/battery.h"

#ifdef __cplusplus
extern "C" {
#endif

struct battery_point {
	int64_t at_ms; /* uptime */
	uint8_t soc_percent;
	uint16_t voltage_mv;
};

#define BATTERY_FAKE_MAX_POINTS 16

/** 100%, 4100 mV, discharging, no script, not shipped. */
void battery_fake_init(void);

/** Fixed reading (clears any script). */
void battery_fake_set(uint8_t soc_percent, uint16_t voltage_mv, enum hal_battery_charge charge);

/** Follow points (sorted by time); holds the last point afterwards. */
void battery_fake_script(const struct battery_point *points, size_t n);

/** The fuel gauge's time to empty (HAL_BATTERY_HOURS_UNKNOWN, the default, for none). */
void battery_fake_set_hours(uint16_t hours_left);

bool battery_fake_shipped(void);
int64_t battery_fake_ship_ms(void);

#ifdef __cplusplus
}
#endif

#endif /* FAKES_BATTERY_FAKE_H_ */
