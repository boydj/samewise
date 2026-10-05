/*
 * input fake: scripted events.
 */

#include <stddef.h>

#include "fakes/input_fake.h"
#include "hal/clock.h"

static struct {
	hal_input_cb cb;
	void *user;
	int locked;
	int headphones;
} s;

void input_fake_init(void)
{
	s.cb = NULL;
	s.user = NULL;
	s.locked = 0;
	s.headphones = 0;
}

void input_fake_emit(enum hal_input_event_type type, uint32_t keys)
{
	struct hal_input_event e = {.type = type, .keys = keys, .uptime_ms = hal_clock_uptime_ms()};

	switch (type) {
	case HAL_INPUT_LOCK_ON:
		s.locked = 1;
		break;
	case HAL_INPUT_LOCK_OFF:
		s.locked = 0;
		break;
	case HAL_INPUT_HEADPHONES_IN:
		s.headphones = 1;
		break;
	case HAL_INPUT_HEADPHONES_OUT:
		s.headphones = 0;
		break;
	default:
		break;
	}
	if (s.cb != NULL) {
		s.cb(&e, s.user);
	}
}

int hal_input_init(hal_input_cb cb, void *user)
{
	s.cb = cb;
	s.user = user;
	return 0;
}

int hal_input_locked(void)
{
	return s.locked;
}

int hal_input_headphones(void)
{
	return s.headphones;
}
