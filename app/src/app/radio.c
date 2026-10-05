/*
 * The radio's composition root.
 */

#include "app/alert_log.h"
#include "app/clock_sync.h"
#include "app/radio.h"
#include "hal/audio_in.h"
#include "hal/input.h"
#include "hal/tuner.h"

static void on_header(const struct same_header *h, void *user)
{
	struct radio *r = user;

	alert_mgr_on_header(&r->alerts, h);
}

static void on_eom(void *user)
{
	struct radio *r = user;

	alert_mgr_on_eom(&r->alerts);
}

static void on_input(const struct hal_input_event *e, void *user)
{
	struct radio *r = user;

	alert_mgr_on_input(&r->alerts, e);
}

uint32_t radio_weather_khz(uint8_t channel)
{
	return (channel >= 1U && channel <= 7U) ? 162400U + 25U * (channel - 1U) : 0U;
}

void radio_boot(struct radio *r, const struct health_config *cfg, int64_t firmware_epoch_utc)
{
	uint32_t khz;

	r->settings_errors = (uint32_t)settings_load(&r->settings);
	r->idle_samples = 0;
	ui_model_init(&r->ui);
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

void radio_low_priority(struct radio *r)
{
	(void)r;
	(void)alert_log_flush();
	(void)clock_sync_flush();
}
