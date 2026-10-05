/*
 * battery fake.
 */

#include <string.h>

#include "fakes/battery_fake.h"
#include "hal/clock.h"

static struct {
	struct battery_point points[BATTERY_FAKE_MAX_POINTS];
	size_t n;
	struct hal_battery_status fixed;
	bool shipped;
	int64_t ship_ms;
} s;

void battery_fake_init(void)
{
	memset(&s, 0, sizeof(s));
	s.fixed.soc_percent = 100;
	s.fixed.voltage_mv = 4100;
	s.fixed.charge = HAL_BATTERY_DISCHARGING;
	s.ship_ms = -1;
}

void battery_fake_set(uint8_t soc_percent, uint16_t voltage_mv, enum hal_battery_charge charge)
{
	s.n = 0;
	s.fixed.soc_percent = soc_percent;
	s.fixed.voltage_mv = voltage_mv;
	s.fixed.charge = charge;
}

void battery_fake_script(const struct battery_point *points, size_t n)
{
	s.n = n < BATTERY_FAKE_MAX_POINTS ? n : BATTERY_FAKE_MAX_POINTS;
	memcpy(s.points, points, s.n * sizeof(points[0]));
	s.fixed.charge = HAL_BATTERY_DISCHARGING;
}

bool battery_fake_shipped(void)
{
	return s.shipped;
}

int64_t battery_fake_ship_ms(void)
{
	return s.ship_ms;
}

int hal_battery_get(struct hal_battery_status *status)
{
	int64_t now = hal_clock_uptime_ms();

	*status = s.fixed;
	if (s.n == 0U) {
		return 0;
	}
	if (now <= s.points[0].at_ms) {
		status->soc_percent = s.points[0].soc_percent;
		status->voltage_mv = s.points[0].voltage_mv;
		return 0;
	}
	for (size_t i = 1; i < s.n; i++) {
		const struct battery_point *a = &s.points[i - 1U], *b = &s.points[i];

		if (now <= b->at_ms) {
			int64_t span = b->at_ms - a->at_ms;
			int64_t off = now - a->at_ms;

			status->soc_percent = (uint8_t)(a->soc_percent +
				(int64_t)(b->soc_percent - a->soc_percent) * off / span);
			status->voltage_mv = (uint16_t)(a->voltage_mv +
				(int64_t)(b->voltage_mv - a->voltage_mv) * off / span);
			return 0;
		}
	}
	status->soc_percent = s.points[s.n - 1U].soc_percent;
	status->voltage_mv = s.points[s.n - 1U].voltage_mv;
	return 0;
}

int hal_battery_ship_mode(void)
{
	if (!s.shipped) {
		s.shipped = true;
		s.ship_ms = hal_clock_uptime_ms();
	}
	return 0;
}
