/*
 * Screen model: what the display should show, as data. Tests assert on it;
 * drawing pixels from it comes in a later milestone.
 *
 * The alert manager fills the alert fields and the health supervisor the
 * warnings, the Bluetooth service its prompts; ui_model_refresh() then picks
 * the screen by precedence: ALERTS OFF, alert, Bluetooth, RESTARTED,
 * warning, listening, standby.
 */

#ifndef APP_UI_MODEL_H_
#define APP_UI_MODEL_H_

#include <stdbool.h>
#include <stdint.h>

#include "services/match/event_table.h"
#include "services/same/same_header.h"

#ifdef __cplusplus
extern "C" {
#endif

enum ui_screen {
	UI_SCREEN_STANDBY,
	UI_SCREEN_LISTENING,  /* AM/FM: alerts paused */
	UI_SCREEN_WARNING,    /* NO SIGNAL family; see warnings for the reason lines */
	UI_SCREEN_RESTARTED,  /* briefly, after a watchdog reset */
	UI_SCREEN_ALERT,
	UI_SCREEN_ALERTS_OFF, /* battery empty */
	UI_SCREEN_BLUETOOTH,  /* window, passkey or confirmation; see ble_screen */
};

/** Warning reasons, as bits. */
enum ui_warning {
	UI_WARN_NO_SIGNAL = 1U << 0,
	UI_WARN_NO_WEEKLY_TEST = 1U << 1,
	UI_WARN_BATTERY_LOW = 1U << 2,
	UI_WARN_BATTERY_CRITICAL = 1U << 3,
	UI_WARN_TUNER_FAULT = 1U << 4,
};

struct ui_model {
	enum ui_screen screen;

	/* Mode, from the alert manager. */
	bool listening;
	bool locked;

	/* Newest active alert, from the alert manager. */
	bool alert;
	bool alert_silenced;
	bool alert_audio;
	uint8_t active_alerts;
	char event[4];
	char event_name[EVENT_NAME_MAX + 1];
	uint8_t event_class;
	uint8_t matched_count;
	struct same_location matched[SAME_MAX_LOCATIONS];
	int64_t expires_ms; /* uptime */

	/* From the health supervisor. */
	uint32_t warnings; /* enum ui_warning bits */
	bool restarted;
	bool alerts_off;
	uint8_t battery_percent;

	/* From the Bluetooth service. */
	uint8_t ble_screen; /* enum ble_screen; 0 = nothing to show */
	uint32_t passkey;   /* while ble_screen is the passkey screen */
};

void ui_model_init(struct ui_model *ui);

/** Recompute screen from the fields. */
void ui_model_refresh(struct ui_model *ui);

#ifdef __cplusplus
}
#endif

#endif /* APP_UI_MODEL_H_ */
