/*
 * alert_out fake: timestamped event log.
 */

#include <stddef.h>
#include <string.h>

#include "fakes/alert_out_fake.h"
#include "hal/clock.h"

static struct {
	struct alert_out_event log[ALERT_OUT_FAKE_LOG];
	uint32_t count;
	enum hal_alert_pattern buzzer;
	enum hal_alert_pattern vibrate;
	bool led;
} s;

static void record(enum alert_out_device device, uint8_t value)
{
	if (s.count < ALERT_OUT_FAKE_LOG) {
		s.log[s.count].uptime_ms = hal_clock_uptime_ms();
		s.log[s.count].device = (uint8_t)device;
		s.log[s.count].value = value;
	}
	s.count++;
}

void alert_out_fake_init(void)
{
	memset(&s, 0, sizeof(s));
}

uint32_t alert_out_fake_count(void)
{
	return s.count;
}

const struct alert_out_event *alert_out_fake_get(uint32_t i)
{
	return (i < s.count && i < ALERT_OUT_FAKE_LOG) ? &s.log[i] : NULL;
}

uint32_t alert_out_fake_count_of(enum alert_out_device device, uint8_t value)
{
	uint32_t n = 0;

	for (uint32_t i = 0; i < s.count && i < ALERT_OUT_FAKE_LOG; i++) {
		n += (s.log[i].device == device && s.log[i].value == value) ? 1U : 0U;
	}
	return n;
}

enum hal_alert_pattern alert_out_fake_buzzer(void)
{
	return s.buzzer;
}

enum hal_alert_pattern alert_out_fake_vibrate(void)
{
	return s.vibrate;
}

bool alert_out_fake_led(void)
{
	return s.led;
}

int hal_alert_out_buzzer(enum hal_alert_pattern pattern)
{
	s.buzzer = pattern;
	record(ALERT_OUT_BUZZER, (uint8_t)pattern);
	return 0;
}

int hal_alert_out_vibrate(enum hal_alert_pattern pattern)
{
	s.vibrate = pattern;
	record(ALERT_OUT_VIBRATE, (uint8_t)pattern);
	return 0;
}

int hal_alert_out_led(bool on)
{
	s.led = on;
	record(ALERT_OUT_LED, on ? 1U : 0U);
	return 0;
}
