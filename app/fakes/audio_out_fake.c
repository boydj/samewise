/*
 * audio_out fake: logs state changes.
 */

#include "fakes/audio_out_fake.h"
#include "hal/clock.h"

static struct {
	bool amp;
	enum hal_audio_out_route route;
	int64_t amp_on_ms;
	uint32_t changes;
} s;

void audio_out_fake_init(void)
{
	s.amp = false;
	s.route = HAL_AUDIO_OUT_ROUTE_NONE;
	s.amp_on_ms = -1;
	s.changes = 0;
}

bool audio_out_fake_amp(void)
{
	return s.amp;
}

enum hal_audio_out_route audio_out_fake_route(void)
{
	return s.route;
}

int64_t audio_out_fake_amp_on_ms(void)
{
	return s.amp_on_ms;
}

uint32_t audio_out_fake_changes(void)
{
	return s.changes;
}

int hal_audio_out_amp(bool on)
{
	if (on && !s.amp) {
		s.amp_on_ms = hal_clock_uptime_ms();
	}
	s.amp = on;
	s.changes++;
	return 0;
}

int hal_audio_out_route(enum hal_audio_out_route route)
{
	s.route = route;
	s.changes++;
	return 0;
}
