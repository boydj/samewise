/*
 * Screen model.
 */

#include <string.h>

#include "app/ui_model.h"

void ui_model_init(struct ui_model *ui)
{
	memset(ui, 0, sizeof(*ui));
	ui->screen = UI_SCREEN_STANDBY;
	ui->battery_percent = 100;
}

void ui_model_refresh(struct ui_model *ui)
{
	if (ui->alerts_off) {
		ui->screen = UI_SCREEN_ALERTS_OFF;
	} else if (ui->alert) {
		ui->screen = UI_SCREEN_ALERT;
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
