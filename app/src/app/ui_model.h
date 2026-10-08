/*
 * Screen model: what the display should show, as data. Tests assert on it,
 * and app/screens.c draws it.
 *
 * The alert manager fills the alert fields, the health supervisor the
 * warnings, battery and tuner signal, the Bluetooth service its prompts, and
 * radio_ui_update() the rest (settings, log, time); ui_model_refresh() then
 * picks the screen by precedence: ALERTS OFF, alert, Bluetooth, RESTARTED,
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

#define UI_RDS_MAX            64U
#define UI_SIGNAL_BARS        4U
#define UI_VOLUME_UNKNOWN     0xFFU
#define UI_HOURS_UNKNOWN      0xFFFFU
/** Standby hours on a full charge, before the fuel gauge has a rate (spec: 5-6 days). */
#define UI_STANDBY_HOURS_FULL 132U

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

	/* Battery time left, from the health supervisor: the fuel gauge's time to
	 * empty, or charge x UI_STANDBY_HOURS_FULL until it has a rate. */
	uint16_t hours_left;
	bool charging;

	/* Tuner, from the health supervisor (band from whoever sets it). */
	uint8_t band;        /* enum hal_tuner_band */
	uint32_t freq_khz;   /* 0 until the tuner reports */
	bool stereo;
	uint8_t signal_bars; /* 0-UI_SIGNAL_BARS, from SNR (see health.c) */

	/* From radio_ui_update(). */
	char rds[UI_RDS_MAX + 1]; /* FM radio text, "" if none */
	uint8_t preset;           /* 1-based station preset matching the frequency, 0 none */
	uint8_t volume;           /* UI_VOLUME_UNKNOWN until the radio UI sets it */
	bool travel;              /* travel counties are active */
	uint8_t county_count;     /* active list; 0 = every location */
	uint8_t filter;           /* enum filter_preset */
	char last_event[4];       /* newest logged header's event code, "" if none */
	int64_t last_local_s;     /* when it was received, local time; -1 if unknown */
	bool phone_connected;
	uint16_t ble_seconds;     /* left in the window or confirmation shown */
	int64_t uptime_ms;        /* when radio_ui_update() ran */
	int64_t local_s;          /* local time then, -1 while the clock is unset */
	uint32_t key_presses;     /* every key event, ignored ones too (backlight) */

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
