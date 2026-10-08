/*
 * The radio's composition root.
 */

#include "app/alert_log.h"
#include "app/ble_link.h"
#include "app/clock_sync.h"
#include "app/radio.h"

#include <string.h>

#include "hal/clock.h"
#include "hal/display.h"
#include "hal/audio_in.h"
#include "hal/input.h"
#include "hal/tuner.h"
#include "services/power/power.h"

static void on_header(const struct same_header *h, void *user)
{
	struct radio *r = user;

	if (r->trace != NULL && r->trace->header != NULL) {
		r->trace->header(r->trace->user, h);
	}
	alert_mgr_on_header(&r->alerts, h);
}

static void on_eom(void *user)
{
	struct radio *r = user;

	if (r->trace != NULL && r->trace->eom != NULL) {
		r->trace->eom(r->trace->user);
	}
	alert_mgr_on_eom(&r->alerts);
}

/*
 * Keys go to the alert manager first. Bluetooth sees a key only if no
 * alert was sounding or on screen, so a press that silences an alert never
 * also opens a window or answers a prompt.
 */
static void on_input(const struct hal_input_event *e, void *user)
{
	struct radio *r = user;
	enum alert_state before = alert_mgr_state(&r->alerts);

	if (e->type == HAL_INPUT_PRESS || e->type == HAL_INPUT_LONG_PRESS ||
	    e->type == HAL_INPUT_COMBO) {
		r->ui.key_presses++;
	}

	alert_mgr_on_input(&r->alerts, e);
	if (r->ble.app != NULL &&
	    (before == ALERT_STATE_STANDBY || before == ALERT_STATE_LISTENING)) {
		ble_on_input(&r->ble, e);
	}
}

#define ARRAY_LEN(a) (sizeof(a) / sizeof((a)[0]))

uint32_t radio_weather_khz(uint8_t channel)
{
	return (channel >= 1U && channel <= 7U) ? 162400U + 25U * (channel - 1U) : 0U;
}

void radio_boot(struct radio *r, const struct health_config *cfg, int64_t firmware_epoch_utc)
{
	uint32_t khz;

	r->settings_errors = (uint32_t)settings_restore(&r->settings);
	r->idle_samples = 0;
	r->ui_log_seen = UINT32_MAX;
	r->ui_last_utc = -1;
	r->trace = NULL;
	r->ble.app = NULL;
	power_init();
	ui_model_init(&r->ui);
	ui_render_init(&r->render);
	r->backlight = false;
	alert_log_init();
	clock_sync_init(firmware_epoch_utc);
	same_init(&r->decoder, on_header, on_eom, r);
	alert_mgr_init(&r->alerts, &r->settings, &r->ui);
	(void)hal_input_init(on_input, r);
	health_init(&r->health, cfg, &r->alerts, &r->ui);
	alert_mgr_set_rwt_hook(&r->alerts, health_note_rwt, &r->health);

	/* Auto-scan is the radio UI's job (later); until then auto means the default. */
	khz = radio_weather_khz(r->settings.channel);
	(void)hal_tuner_power(true);
	(void)hal_tuner_set_band(HAL_TUNER_BAND_WB);
	(void)hal_tuner_tune(khz != 0U ? khz : cfg->default_khz);
	(void)hal_audio_in_start();
}

void radio_set_trace(struct radio *r, const struct radio_trace *trace)
{
	r->trace = trace;
}

void radio_ble_start(struct radio *r, const struct ble_port *port, void *port_user)
{
	ble_init(&r->ble, &ble_link_ops, r, port, port_user);
}

int radio_pump_audio(struct radio *r, size_t max)
{
	static int16_t block[HAL_AUDIO_IN_BLOCK_SAMPLES];
	size_t want = max < HAL_AUDIO_IN_BLOCK_SAMPLES ? max : HAL_AUDIO_IN_BLOCK_SAMPLES;
	int n = hal_audio_in_read(block, want, 0);

	if (n <= 0) {
		return n;
	}
	health_note_audio(&r->health);
	same_feed(&r->decoder, block, (size_t)n);
	health_note_decoder(&r->health, same_get_stats(&r->decoder)->samples + r->idle_samples);
	return n;
}

void radio_idle_audio(struct radio *r, uint32_t n)
{
	r->idle_samples += n;
	health_note_audio(&r->health);
	health_note_decoder(&r->health, same_get_stats(&r->decoder)->samples + r->idle_samples);
}

static uint8_t preset_index(const struct settings *s, uint8_t band, uint32_t khz)
{
	static const uint8_t to_codec[] = {
		[HAL_TUNER_BAND_FM] = CODEC_BAND_FM,
		[HAL_TUNER_BAND_AM] = CODEC_BAND_AM,
		[HAL_TUNER_BAND_WB] = CODEC_BAND_WB,
	};

	for (uint8_t i = 0; i < s->preset_count && band < ARRAY_LEN(to_codec); i++) {
		if (s->presets[i].band == to_codec[band] && s->presets[i].khz == khz) {
			return (uint8_t)(i + 1U);
		}
	}
	return 0;
}

static void read_last_alert(struct radio *r)
{
	uint32_t appended = alert_log_appended();
	struct alert_log_entry e;

	if (appended == r->ui_log_seen && r->ui_log_seen != UINT32_MAX) {
		return;
	}
	r->ui_log_seen = appended;
	r->ui.last_event[0] = '\0';
	r->ui_last_utc = -1;
	for (uint32_t i = 0; i < ALERT_LOG_SIZE && alert_log_read(i, &e) == 0; i++) {
		/* ZCZC-ORG-EEE-...: the event code is characters 9 to 11. */
		if (e.outcome == ALERT_LOG_ALERTED && strlen(e.raw) >= 12U) {
			memcpy(r->ui.last_event, &e.raw[9], 3);
			r->ui.last_event[3] = '\0';
			r->ui_last_utc = e.received_utc;
			return;
		}
	}
}

void radio_ui_update(struct radio *r)
{
	struct ui_model *ui = &r->ui;
	const struct county_list *active = settings_active_counties(&r->settings);
	int64_t utc = hal_clock_utc_s();

	ui->uptime_ms = hal_clock_uptime_ms();
	ui->local_s = utc >= 0 ? settings_local_time(&r->settings, utc, NULL) : -1;
	ui->travel = r->settings.mode == SETTINGS_MODE_TRAVEL;
	ui->county_count = active->count;
	ui->filter = r->settings.filter;
	ui->preset = preset_index(&r->settings, ui->band, ui->freq_khz);
	if (ui->band != HAL_TUNER_BAND_FM || hal_tuner_rds_text(ui->rds, sizeof(ui->rds)) < 0) {
		ui->rds[0] = '\0';
	}
	read_last_alert(r);
	ui->last_local_s = r->ui_last_utc >= 0 ? settings_local_time(&r->settings, r->ui_last_utc, NULL)
					       : -1;
	ui->phone_connected = r->ble.app != NULL && r->ble.connected;
	ui->ble_seconds = r->ble.app != NULL ? ble_screen_seconds(&r->ble, ui->uptime_ms) : 0U;
	ui_model_refresh(ui);
}

void radio_ui_tick(struct radio *r, const struct radio_lock *lk)
{
	static struct ui_model snapshot;
	bool light;

	lk->lock(lk->user);
	radio_ui_update(r);
	snapshot = r->ui;
	lk->unlock(lk->user);

	if (ui_render_frame(&r->render, &snapshot, snapshot.uptime_ms)) {
		(void)hal_display_flush();
	}
	light = ui_render_backlight(&r->render, snapshot.uptime_ms);
	if (light != r->backlight) {
		r->backlight = light;
		(void)hal_display_backlight(light);
	}
}

void radio_low_priority(struct radio *r)
{
	int logged = alert_log_flush();

	(void)clock_sync_flush();
	if (r->ble.app != NULL) {
		if (logged > 0) {
			ble_log_added(&r->ble);
		}
		ble_poll_status(&r->ble);
	}
}
