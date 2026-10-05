/*
 * Alert output interface: PAM8904E buzzer, coin vibration motor and LED.
 *
 * Patterns run on their own once started, so callers never block on them.
 * The fake records a timestamped event log.
 */

#ifndef HAL_ALERT_OUT_H_
#define HAL_ALERT_OUT_H_

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

enum hal_alert_pattern {
	HAL_ALERT_PATTERN_OFF,
	HAL_ALERT_PATTERN_ALERT,      /* continuous alert cadence (first 2 minutes) */
	HAL_ALERT_PATTERN_REMINDER,   /* 3-second reminder */
	HAL_ALERT_PATTERN_CHIRP,      /* single short warning chirp */
	HAL_ALERT_PATTERN_FINAL_BEEP, /* long beep before ALERTS OFF */
};

/** Start a buzzer pattern; HAL_ALERT_PATTERN_OFF stops it. Never blocks. */
int hal_alert_out_buzzer(enum hal_alert_pattern pattern);

/** Start a vibration pattern; HAL_ALERT_PATTERN_OFF stops it. Never blocks. */
int hal_alert_out_vibrate(enum hal_alert_pattern pattern);

/** Turn the alert LED on or off. */
int hal_alert_out_led(bool on);

#ifdef __cplusplus
}
#endif

#endif /* HAL_ALERT_OUT_H_ */
