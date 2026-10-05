/*
 * watchdog fake: records feeds and simulates a reset when starved.
 *
 * Runs on hal/clock.h timers, so starvation happens in simulated time. When
 * the timeout passes without a feed, the fake stops, sets the next boot's
 * reset reason to HAL_RESET_WATCHDOG and calls the reset handler, which is
 * expected to restart the application (see clock_fake_simulate_reset()).
 */

#ifndef FAKES_WATCHDOG_FAKE_H_
#define FAKES_WATCHDOG_FAKE_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*watchdog_fake_reset_fn)(void *user);

/** Power-on state: stopped, no feeds, reason HAL_RESET_POWER_ON. */
void watchdog_fake_init(watchdog_fake_reset_fn on_reset, void *user);

bool watchdog_fake_running(void);
uint32_t watchdog_fake_feeds(void);
int64_t watchdog_fake_last_feed_ms(void);
/** Simulated resets since init. */
uint32_t watchdog_fake_resets(void);

#ifdef __cplusplus
}
#endif

#endif /* FAKES_WATCHDOG_FAKE_H_ */
