/*
 * Screen model.
 */

#include <string.h>

#include "app/ui_model.h"
#include "hal/tuner.h"
#include "services/match/filter.h"

void ui_model_init(struct ui_model *ui)
{
	memset(ui, 0, sizeof(*ui));
	ui->screen = UI_SCREEN_STANDBY;
	ui->battery_percent = 100;
	ui->hours_left = UI_HOURS_UNKNOWN;
	ui->band = HAL_TUNER_BAND_WB; /* the radio boots on weather */
	ui->volume = UI_VOLUME_UNKNOWN;
	ui->filter = FILTER_WARNINGS_WATCHES;
	ui->local_s = -1;
	ui->last_local_s = -1;
}

void ui_model_refresh(struct ui_model *ui)
{
	if (ui->alerts_off) {
		ui->screen = UI_SCREEN_ALERTS_OFF;
	} else if (ui->alert) {
		ui->screen = UI_SCREEN_ALERT;
	} else if (ui->ble_screen != 0U) {
		ui->screen = UI_SCREEN_BLUETOOTH;
	} else if (ui->restarted) {
		ui->screen = UI_SCREEN_RESTARTED;
	} else if (ui->warnings & (UI_WARN_NO_SIGNAL | UI_WARN_NO_WEEKLY_TEST | UI_WARN_TUNER_FAULT |
				   UI_WARN_BATTERY_LOW | UI_WARN_BATTERY_CRITICAL)) {
		ui->screen = UI_SCREEN_WARNING;
	} else if (ui->listening) {
		ui->screen = UI_SCREEN_LISTENING;
	} else {
		ui->screen = UI_SCREEN_STANDBY;
	}
}
