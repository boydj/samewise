/*
 * clock fake: simulated time behind hal/clock.h.
 *
 * Time stands still until a test calls clock_fake_advance_ms(), which fires
 * every due timer in order, so a week of standby runs in milliseconds.
 */

#ifndef FAKES_CLOCK_FAKE_H_
#define FAKES_CLOCK_FAKE_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Back to uptime 0, UTC unset, no timers. */
void clock_fake_reset(void);

/**
 * Advance simulated time by ms, firing due timers in order of due time
 * (ties in the order they were started). Timer callbacks see the uptime at
 * which they were due.
 */
void clock_fake_advance_ms(int64_t ms);

/** Number of timer callbacks run since reset. */
uint32_t clock_fake_fired_count(void);

#ifdef __cplusplus
}
#endif

#endif /* FAKES_CLOCK_FAKE_H_ */
