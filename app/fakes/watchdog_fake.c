/*
 * watchdog fake: starvation in simulated time triggers a simulated reset.
 */

#include <errno.h>
#include <stddef.h>

#include "fakes/watchdog_fake.h"
#include "hal/clock.h"
#include "hal/watchdog.h"

static struct {
	struct hal_clock_timer timer;
	watchdog_fake_reset_fn on_reset;
	void *user;
	bool running;
	uint32_t timeout_ms;
	uint32_t feeds;
	int64_t last_feed_ms;
	uint32_t resets;
	enum hal_reset_reason reason;
} s;

static void starved(struct hal_clock_timer *timer, void *user)
{
	(void)timer;
	(void)user;
	/* A real reset stops the watchdog until the application starts it again. */
	s.running = false;
	s.resets++;
	s.reason = HAL_RESET_WATCHDOG;
	if (s.on_reset != NULL) {
		s.on_reset(s.user);
	}
}

void watchdog_fake_init(watchdog_fake_reset_fn on_reset, void *user)
{
	if (s.running) {
		hal_clock_timer_stop(&s.timer);
	}
	s.on_reset = on_reset;
	s.user = user;
	s.running = false;
	s.timeout_ms = 0;
	s.feeds = 0;
	s.last_feed_ms = -1;
	s.resets = 0;
	s.reason = HAL_RESET_POWER_ON;
	hal_clock_timer_init(&s.timer, starved, NULL);
}

bool watchdog_fake_running(void)
{
	return s.running;
}

uint32_t watchdog_fake_feeds(void)
{
	return s.feeds;
}

int64_t watchdog_fake_last_feed_ms(void)
{
	return s.last_feed_ms;
}

uint32_t watchdog_fake_resets(void)
{
	return s.resets;
}

int hal_watchdog_start(uint32_t timeout_ms)
{
	if (timeout_ms == 0U) {
		return -EINVAL;
	}
	if (s.running) {
		return -EALREADY;
	}
	/* The timer may have been wiped by a simulated reset: set it up again. */
	hal_clock_timer_init(&s.timer, starved, NULL);
	s.timeout_ms = timeout_ms;
	s.running = true;
	return hal_clock_timer_start(&s.timer, timeout_ms, 0);
}

int hal_watchdog_feed(void)
{
	if (!s.running) {
		return -EINVAL;
	}
	s.feeds++;
	s.last_feed_ms = hal_clock_uptime_ms();
	return hal_clock_timer_start(&s.timer, s.timeout_ms, 0);
}

enum hal_reset_reason hal_watchdog_reset_reason(void)
{
	return s.reason;
}
