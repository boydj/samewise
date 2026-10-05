/*
 * The application side of the Bluetooth settings service.
 */

#include <errno.h>
#include <string.h>

#include "app/alert_log.h"
#include "app/ble_link.h"
#include "app/radio.h"
#include "hal/clock.h"
#include "hal/tuner.h"

static int8_t clamp8(int16_t v)
{
	return (int8_t)(v < -128 ? -128 : (v > 127 ? 127 : v));
}

static uint8_t tuned_channel(const struct radio *r)
{
	uint32_t khz = r->health.tuner_khz;

	if (khz < 162400U || khz > 162550U || (khz - 162400U) % 25U != 0U) {
		return 0;
	}
	return (uint8_t)((khz - 162400U) / 25U + 1U);
}

static void read_status(const struct radio *r, struct codec_status *st)
{
	st->battery_percent = r->ui.battery_percent;
	st->hours_left = BLE_LINK_HOURS_UNKNOWN;
	st->snr_db = clamp8(r->health.snr_db);
	st->rssi_dbuv = clamp8(r->health.rssi_dbuv);
	st->channel = tuned_channel(r);
	st->last_rwt_utc = r->alerts.last_rwt_utc;
	st->health_flags = r->ui.warnings;
	st->locked = r->ui.locked ? 1U : 0U;
}

static int read_log(struct codec_log_entry *out)
{
	struct alert_log_entry e;

	if (alert_log_read(out->index, &e) != 0) {
		return -ENOENT;
	}
	out->count = (uint8_t)alert_log_stored();
	out->received_utc = e.received_utc;
	out->outcome = e.outcome;
	out->flags = e.flags;
	memcpy(out->raw, e.raw, sizeof(out->raw));
	return 0;
}

static int link_read(void *user, enum wx_gatt_chr chr, union ble_value *v)
{
	const struct radio *r = user;
	const struct settings *s = &r->settings;
	const struct county_list *c;

	switch (chr) {
	case WX_GATT_CHR_COUNTIES:
	case WX_GATT_CHR_TRAVEL_COUNTIES:
		c = chr == WX_GATT_CHR_COUNTIES ? &s->home : &s->travel;
		v->counties.count = c->count;
		memcpy(v->counties.codes, c->codes, c->count * sizeof(c->codes[0]));
		return 0;
	case WX_GATT_CHR_MODE:
		v->mode.mode = s->mode;
		v->mode.channel = s->channel;
		return 0;
	case WX_GATT_CHR_EVENT_FILTER:
		v->filter.preset = s->filter;
		memcpy(v->filter.bitmap, s->custom, sizeof(v->filter.bitmap));
		return 0;
	case WX_GATT_CHR_EVENT_TABLE:
		if (v->et.index >= s->events.count) {
			return -ENOENT;
		}
		v->et.version = s->events.version;
		v->et.count = (uint8_t)s->events.count;
		v->et.entry = s->events.entries[v->et.index];
		return 0;
	case WX_GATT_CHR_PRESETS:
		v->presets.count = s->preset_count;
		memcpy(v->presets.p, s->presets, s->preset_count * sizeof(s->presets[0]));
		return 0;
	case WX_GATT_CHR_STATUS:
		read_status(r, &v->status);
		return 0;
	case WX_GATT_CHR_ALERT_LOG:
		return read_log(&v->log);
	default:
		return -ENOTSUP;
	}
}

static int write_mode(struct radio *r, const struct codec_mode *m)
{
	int err = settings_set_mode(&r->settings, (enum settings_mode)m->mode);
	int err2 = settings_set_channel(&r->settings, m->channel);
	uint32_t khz = radio_weather_khz(m->channel);

	if (khz != 0U && alert_mgr_state(&r->alerts) != ALERT_STATE_LISTENING) {
		(void)hal_tuner_tune(khz);
	}
	return err != 0 ? err : err2;
}

static int write_time(struct radio *r, const struct codec_time *t)
{
	int err = settings_set_tz(&r->settings, t->tz);

	if (err == -EINVAL) {
		return err;
	}
	(void)hal_clock_set_utc(t->utc);
	return err;
}

static int link_write(void *user, enum wx_gatt_chr chr, const union ble_value *v)
{
	struct radio *r = user;
	struct settings *s = &r->settings;

	switch (chr) {
	case WX_GATT_CHR_COUNTIES:
	case WX_GATT_CHR_TRAVEL_COUNTIES:
		return settings_set_counties(s,
					     chr == WX_GATT_CHR_COUNTIES ? SETTINGS_MODE_HOME
									 : SETTINGS_MODE_TRAVEL,
					     v->counties.codes, v->counties.count);
	case WX_GATT_CHR_MODE:
		return write_mode(r, &v->mode);
	case WX_GATT_CHR_EVENT_FILTER:
		return settings_set_filter(s, (enum filter_preset)v->filter.preset, v->filter.bitmap);
	case WX_GATT_CHR_EVENT_TABLE:
		return settings_set_event_table(s, v->table);
	case WX_GATT_CHR_TIME:
		return write_time(r, &v->time);
	case WX_GATT_CHR_PRESETS:
		return settings_set_presets(s, v->presets.p, v->presets.count);
	default:
		return -EINVAL;
	}
}

static int link_command(void *user, enum codec_command cmd)
{
	struct radio *r = user;

	switch (cmd) {
	case CODEC_CMD_TEST_ALERT:
		return alert_mgr_test_alert(&r->alerts);
	case CODEC_CMD_CLEAR_LOG:
		alert_log_clear();
		return 0;
	case CODEC_CMD_FACTORY_RESET:
		settings_factory_reset(&r->settings);
		alert_log_clear();
		return 0;
	default:
		return -EINVAL;
	}
}

static void link_screen(void *user, enum ble_screen screen, uint32_t passkey)
{
	struct radio *r = user;

	r->ui.ble_screen = (uint8_t)screen;
	r->ui.passkey = passkey;
	ui_model_refresh(&r->ui);
}

const struct ble_app_ops ble_link_ops = {
	.read = link_read,
	.write = link_write,
	.command = link_command,
	.screen = link_screen,
};
