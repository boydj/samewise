/*
 * alert_out fake: a timestamped log of every buzzer, vibration and LED call.
 */

#ifndef FAKES_ALERT_OUT_FAKE_H_
#define FAKES_ALERT_OUT_FAKE_H_

#include <stdbool.h>
#include <stdint.h>

#include "hal/alert_out.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ALERT_OUT_FAKE_LOG 512

enum alert_out_device {
	ALERT_OUT_BUZZER,
	ALERT_OUT_VIBRATE,
	ALERT_OUT_LED,
};

struct alert_out_event {
	int64_t uptime_ms;
	uint8_t device; /* enum alert_out_device */
	uint8_t value;  /* enum hal_alert_pattern, or 0/1 for the LED */
};

void alert_out_fake_init(void);

/** Events logged since init (the log keeps the first ALERT_OUT_FAKE_LOG). */
uint32_t alert_out_fake_count(void);
const struct alert_out_event *alert_out_fake_get(uint32_t i);

/** Events for one device with a given value, since init. */
uint32_t alert_out_fake_count_of(enum alert_out_device device, uint8_t value);

/** Current state of each output. */
enum hal_alert_pattern alert_out_fake_buzzer(void);
enum hal_alert_pattern alert_out_fake_vibrate(void);
bool alert_out_fake_led(void);

#ifdef __cplusplus
}
#endif

#endif /* FAKES_ALERT_OUT_FAKE_H_ */
