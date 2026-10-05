/*
 * Clock interface: monotonic uptime, UTC wall time and timers.
 *
 * The board uses the nRF52 RTC on the 32.768 kHz crystal. The native_sim
 * fake (fakes/clock_fake.h) runs simulated time that advances only when a
 * test asks, so days can pass in milliseconds.
 */

#ifndef HAL_CLOCK_H_
#define HAL_CLOCK_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Milliseconds since boot; never goes backwards. */
int64_t hal_clock_uptime_ms(void);

/** UTC seconds since 1970-01-01, or -1 if never set. */
int64_t hal_clock_utc_s(void);

/** Set UTC (phone sync). */
int hal_clock_set_utc(int64_t utc_s);

/**
 * Slew UTC by delta_s (a SAME issue time corrects the clock to the minute
 * between phone syncs). -EINVAL if UTC was never set.
 */
int hal_clock_adjust_utc(int32_t delta_s);

struct hal_clock_timer;

typedef void (*hal_clock_timer_cb)(struct hal_clock_timer *timer, void *user);

/**
 * A timer owned by the caller (no heap). Fields are private to the clock
 * implementation; initialise with hal_clock_timer_init().
 */
struct hal_clock_timer {
	hal_clock_timer_cb cb;
	void *user;
	int64_t due_ms;     /* uptime at which it fires */
	uint32_t period_ms; /* 0 for one-shot */
	bool active;
	struct hal_clock_timer *next;
};

void hal_clock_timer_init(struct hal_clock_timer *timer, hal_clock_timer_cb cb, void *user);

/**
 * Start (or restart) a timer. It first fires delay_ms from now, then every
 * period_ms if period_ms is non-zero. Callbacks run in the clock's timer
 * context and must not block; they may start or stop timers.
 */
int hal_clock_timer_start(struct hal_clock_timer *timer, uint32_t delay_ms, uint32_t period_ms);

/** Stop a timer. Safe to call on a stopped timer and from its own callback. */
void hal_clock_timer_stop(struct hal_clock_timer *timer);

#ifdef __cplusplus
}
#endif

#endif /* HAL_CLOCK_H_ */
