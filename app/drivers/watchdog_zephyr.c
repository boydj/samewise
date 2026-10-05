/*
 * hal/watchdog.h on the SoC watchdog (nRF WDT on the board). The reset cause
 * comes from hwinfo and is read once, then cleared, so the next boot sees
 * only its own cause.
 */

#include <errno.h>

#include <zephyr/device.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/drivers/watchdog.h>

#include "hal/watchdog.h"

static const struct device *const wdt = DEVICE_DT_GET(DT_ALIAS(watchdog0));
static int channel = -1;
static bool reason_read;
static enum hal_reset_reason reason = HAL_RESET_OTHER;

int hal_watchdog_start(uint32_t timeout_ms)
{
	struct wdt_timeout_cfg cfg = {
		.window = {.min = 0, .max = timeout_ms},
		.flags = WDT_FLAG_RESET_SOC,
	};
	int err;

	if (!device_is_ready(wdt)) {
		return -ENODEV;
	}
	if (channel >= 0) {
		return -EALREADY;
	}
	channel = wdt_install_timeout(wdt, &cfg);
	if (channel < 0) {
		return channel;
	}
	err = wdt_setup(wdt, WDT_OPT_PAUSE_HALTED_BY_DBG);
	if (err != 0) {
		channel = -1;
	}
	return err;
}

int hal_watchdog_feed(void)
{
	return channel >= 0 ? wdt_feed(wdt, channel) : -EINVAL;
}

enum hal_reset_reason hal_watchdog_reset_reason(void)
{
	uint32_t cause = 0;

	if (reason_read) {
		return reason;
	}
	reason_read = true;
	if (hwinfo_get_reset_cause(&cause) != 0) {
		return reason;
	}
	(void)hwinfo_clear_reset_cause();
	if (cause & RESET_WATCHDOG) {
		reason = HAL_RESET_WATCHDOG;
	} else if (cause & RESET_SOFTWARE) {
		reason = HAL_RESET_SOFTWARE;
	} else if (cause & RESET_PIN) {
		reason = HAL_RESET_PIN;
	} else if (cause == 0U || (cause & RESET_POR)) {
		reason = HAL_RESET_POWER_ON;
	}
	return reason;
}
