/*
 * Watchdog interface: the nRF52 hardware watchdog on the board.
 *
 * Only the health supervisor starts and feeds it. Once started it cannot be
 * stopped or reconfigured, and it keeps running during sleep. The native_sim
 * fake (fakes/watchdog_fake.h) records feeds and, when starved, triggers a
 * simulated reset that restarts the application.
 */

#ifndef HAL_WATCHDOG_H_
#define HAL_WATCHDOG_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Starvation time before a reset (spec: Health monitoring). */
#define HAL_WATCHDOG_TIMEOUT_MS 8000U

/** Why the current boot happened. */
enum hal_reset_reason {
	HAL_RESET_POWER_ON,
	HAL_RESET_WATCHDOG,
	HAL_RESET_SOFTWARE,
	HAL_RESET_PIN,
	HAL_RESET_OTHER,
};

/**
 * Start the watchdog. The first feed is due within timeout_ms.
 *
 * @return 0; -EALREADY if already running (it cannot be reconfigured);
 *         -EINVAL for a zero timeout.
 */
int hal_watchdog_start(uint32_t timeout_ms);

/** Feed the watchdog. -EINVAL if it was never started. */
int hal_watchdog_feed(void);

/** Reason for the current boot, read once at startup and logged. */
enum hal_reset_reason hal_watchdog_reset_reason(void);

#ifdef __cplusplus
}
#endif

#endif /* HAL_WATCHDOG_H_ */
