/*
 * Bluetooth settings service: characteristics and connection policy.
 */

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "services/ble/ble_service.h"
#include "services/ble/tz.h"
#include "services/power/power.h"

static const uint8_t props[WX_GATT_CHR_COUNT] = {
#define WX_GATT_PROPS(id, name, offset, p, max_len, schema, desc) [WX_GATT_CHR_##id] = (p),
	WX_GATT_CHARACTERISTICS(WX_GATT_PROPS)
#undef WX_GATT_PROPS
};

/* The event table being written; one connection at a time. */
static struct event_table staging;

/* ---- Screen ---- */

static void update_screen(struct ble *b)
{
	enum ble_screen screen = BLE_SCREEN_NONE;

	if (b->confirm != BLE_SCREEN_NONE) {
		screen = (enum ble_screen)b->confirm;
	} else if (b->has_passkey) {
		screen = BLE_SCREEN_PASSKEY;
	} else if (b->window == BLE_WINDOW_PAIRING) {
		screen = BLE_SCREEN_PAIRING;
	} else if (b->window == BLE_WINDOW_CONNECT) {
		screen = BLE_SCREEN_CONNECT;
	}
	if (screen != b->screen || screen == BLE_SCREEN_PASSKEY) {
		b->screen = (uint8_t)screen;
		b->app->screen(b->app_user, screen, b->has_passkey ? b->passkey : 0U);
	}
}

/* ---- Notifications ---- */

static void notify(struct ble *b, enum wx_gatt_chr chr, const uint8_t *data, size_t len)
{
	if (b->connected && b->secure && b->port->notify(b->port_user, chr, data, len) == 0) {
		b->stats.notifications++;
	}
}

static void indicate_control(struct ble *b, enum codec_command cmd, enum codec_control_result r)
{
	struct codec_control_indication ind = {.command = (uint8_t)cmd, .result = (uint8_t)r};
	uint8_t buf[WX_GATT_MAX_CONTROL];

	notify(b, WX_GATT_CHR_CONTROL, buf, codec_encode_control_indication(&ind, buf));
}

static void send_status(struct ble *b, const struct codec_status *st)
{
	uint8_t buf[WX_GATT_MAX_STATUS];

	b->last_status = *st;
	b->status_sent = true;
	notify(b, WX_GATT_CHR_STATUS, buf, codec_encode_status(st, buf));
}

static void force_status(struct ble *b)
{
	union ble_value v;

	memset(&v, 0, sizeof(v));
	if (b->app->read(b->app_user, WX_GATT_CHR_STATUS, &v) == 0) {
		send_status(b, &v.status);
	}
}

void ble_poll_status(struct ble *b)
{
	const struct codec_status *last = &b->last_status;
	union ble_value v;
	const struct codec_status *now = &v.status;

	if (!b->connected || !b->secure) {
		return;
	}
	memset(&v, 0, sizeof(v));
	if (b->app->read(b->app_user, WX_GATT_CHR_STATUS, &v) != 0) {
		return;
	}
	if (!b->status_sent || now->battery_percent != last->battery_percent ||
	    now->health_flags != last->health_flags || now->locked != last->locked ||
	    now->channel != last->channel ||
	    abs(now->snr_db - last->snr_db) >= BLE_SIGNAL_NOTIFY_DB ||
	    abs(now->rssi_dbuv - last->rssi_dbuv) >= BLE_SIGNAL_NOTIFY_DB) {
		send_status(b, now);
	}
}

void ble_log_added(struct ble *b)
{
	union ble_value v;
	uint8_t buf[WX_GATT_MAX_ALERT_LOG];

	memset(&v, 0, sizeof(v));
	v.log.index = 0;
	if (b->app->read(b->app_user, WX_GATT_CHR_ALERT_LOG, &v) == 0) {
		notify(b, WX_GATT_CHR_ALERT_LOG, buf, codec_encode_log_entry(&v.log, buf));
	}
}

/* ---- Windows ---- */

static void close_window(struct ble *b)
{
	uint8_t was = b->window;

	if (was == BLE_WINDOW_NONE) {
		return;
	}
	hal_clock_timer_stop(&b->window_timer);
	b->window = BLE_WINDOW_NONE;
	b->port->adv_stop(b->port_user);
	if (was == BLE_WINDOW_PAIRING) {
		b->port->set_pairable(b->port_user, false);
		b->has_passkey = false;
		if (b->connected && !b->secure && !b->bonded) {
			b->port->disconnect(b->port_user); /* an unpaired phone */
		}
	}
	power_hfxo_release(POWER_HFXO_BLE_WINDOW);
	update_screen(b);
}

static void window_expired(struct hal_clock_timer *t, void *user)
{
	(void)t;
	close_window(user);
}

static void open_window(struct ble *b, enum ble_window w)
{
	power_hfxo_request(POWER_HFXO_BLE_WINDOW);
	b->window = (uint8_t)w;
	if (w == BLE_WINDOW_PAIRING) {
		b->port->set_pairable(b->port_user, true);
	}
	(void)b->port->adv_start(b->port_user, w == BLE_WINDOW_CONNECT);
	(void)hal_clock_timer_start(&b->window_timer,
				    w == BLE_WINDOW_CONNECT ? BLE_CONNECT_WINDOW_MS
							    : BLE_PAIRING_WINDOW_MS,
				    0);
	update_screen(b);
}

/* ---- Confirmations ---- */

static void end_confirm(struct ble *b, bool confirmed)
{
	uint8_t what = b->confirm;

	hal_clock_timer_stop(&b->confirm_timer);
	b->confirm = BLE_SCREEN_NONE;
	if (what == BLE_SCREEN_CONFIRM_BOND) {
		if (confirmed) {
			open_window(b, BLE_WINDOW_PAIRING);
		}
	} else if (what == BLE_SCREEN_CONFIRM_RESET) {
		if (confirmed) {
			(void)b->app->command(b->app_user, CODEC_CMD_FACTORY_RESET);
			indicate_control(b, CODEC_CMD_FACTORY_RESET, CODEC_CTRL_DONE);
			b->port->unpair_all(b->port_user);
		} else {
			indicate_control(b, CODEC_CMD_FACTORY_RESET, CODEC_CTRL_CANCELLED);
		}
	}
	update_screen(b);
}

static void confirm_expired(struct hal_clock_timer *t, void *user)
{
	(void)t;
	end_confirm(user, false);
}

static void ask(struct ble *b, enum ble_screen what)
{
	b->confirm = (uint8_t)what;
	(void)hal_clock_timer_start(&b->confirm_timer, BLE_CONFIRM_MS, 0);
	update_screen(b);
}

/* ---- Keys ---- */

#define PAIR_COMBO (HAL_INPUT_KEY_BAND | HAL_INPUT_KEY_STBY)

void ble_on_input(struct ble *b, const struct hal_input_event *e)
{
	switch (e->type) {
	case HAL_INPUT_LOCK_ON:
	case HAL_INPUT_LOCK_OFF:
		b->locked = e->type == HAL_INPUT_LOCK_ON;
		return;
	case HAL_INPUT_PRESS:
	case HAL_INPUT_LONG_PRESS:
	case HAL_INPUT_COMBO:
		break;
	default:
		return;
	}
	if (b->locked) {
		return;
	}
	if (b->confirm != BLE_SCREEN_NONE) {
		end_confirm(b, e->type == HAL_INPUT_LONG_PRESS && e->keys == HAL_INPUT_KEY_STBY);
		return;
	}
	if (b->connected || b->window != BLE_WINDOW_NONE) {
		return; /* one connection; a window already open runs its course */
	}
	if (e->type == HAL_INPUT_LONG_PRESS && e->keys == HAL_INPUT_KEY_BAND) {
		open_window(b, BLE_WINDOW_CONNECT);
	} else if (e->type == HAL_INPUT_COMBO && e->keys == PAIR_COMBO) {
		if (b->port->bond_count(b->port_user) >= BLE_MAX_BONDS) {
			ask(b, BLE_SCREEN_CONFIRM_BOND);
		} else {
			open_window(b, BLE_WINDOW_PAIRING);
		}
	}
}

/* ---- Stack events ---- */

void ble_init(struct ble *b, const struct ble_app_ops *app, void *app_user,
	      const struct ble_port *port, void *port_user)
{
	memset(b, 0, sizeof(*b));
	b->app = app;
	b->app_user = app_user;
	b->port = port;
	b->port_user = port_user;
	b->locked = hal_input_locked() > 0;
	hal_clock_timer_init(&b->window_timer, window_expired, b);
	hal_clock_timer_init(&b->confirm_timer, confirm_expired, b);
	b->port->adv_stop(b->port_user);
	b->port->set_pairable(b->port_user, false);
}

void ble_on_connected(struct ble *b)
{
	b->connected = true;
	b->secure = false;
	b->bonded = false;
	b->status_sent = false;
	power_hfxo_request(POWER_HFXO_BLE_CONN);
	if (b->window == BLE_WINDOW_CONNECT) {
		close_window(b);
	} else if (b->window == BLE_WINDOW_PAIRING) {
		b->port->adv_stop(b->port_user); /* stay pairable until the window ends */
	}
}

void ble_on_disconnected(struct ble *b)
{
	b->connected = false;
	b->secure = false;
	b->bonded = false;
	b->et_staging = false;
	b->et_index = 0;
	b->log_index = 0;
	power_hfxo_release(POWER_HFXO_BLE_CONN);
	if (b->confirm == BLE_SCREEN_CONFIRM_RESET) {
		end_confirm(b, false); /* the phone that asked has gone */
	}
	if (b->window == BLE_WINDOW_PAIRING) {
		b->has_passkey = false;
		(void)b->port->adv_start(b->port_user, false);
		update_screen(b);
	}
}

void ble_on_security(struct ble *b, bool lesc_authenticated)
{
	b->secure = b->connected && lesc_authenticated;
}

void ble_on_passkey(struct ble *b, uint32_t passkey)
{
	if (b->window != BLE_WINDOW_PAIRING) {
		return;
	}
	b->passkey = passkey;
	b->has_passkey = true;
	update_screen(b);
}

void ble_on_pairing_done(struct ble *b, bool bonded)
{
	b->has_passkey = false;
	b->bonded = b->connected && bonded;
	if (b->window == BLE_WINDOW_PAIRING && bonded) {
		close_window(b);
	} else {
		update_screen(b);
	}
}

/* ---- GATT ---- */

static int codec_err(int err)
{
	switch (err) {
	case CODEC_OK:
		return 0;
	case CODEC_ERR_LENGTH:
		return WX_GATT_ERR_LENGTH;
	case CODEC_ERR_SCHEMA:
		return WX_GATT_ERR_SCHEMA;
	default:
		return WX_GATT_ERR_VALUE;
	}
}

static int app_err(int err)
{
	if (err == 0) {
		return 0;
	}
	return err == -EINVAL ? WX_GATT_ERR_VALUE : WX_GATT_ERR_STORAGE;
}

int ble_read(struct ble *b, enum wx_gatt_chr chr, uint8_t *buf, size_t *len)
{
	union ble_value v;

	if ((unsigned int)chr >= WX_GATT_CHR_COUNT || !(props[chr] & WX_GATT_READ)) {
		return BLE_ATT_ERR_READ_NOT_PERMITTED;
	}
	if (!b->secure) {
		b->stats.refused++;
		return BLE_ATT_ERR_AUTHENTICATION;
	}
	memset(&v, 0, sizeof(v));
	if (chr == WX_GATT_CHR_EVENT_TABLE) {
		v.et.index = b->et_index;
	} else if (chr == WX_GATT_CHR_ALERT_LOG) {
		v.log.index = b->log_index;
	}
	if (b->app->read(b->app_user, chr, &v) != 0) {
		return WX_GATT_ERR_INDEX;
	}
	switch (chr) {
	case WX_GATT_CHR_COUNTIES:
	case WX_GATT_CHR_TRAVEL_COUNTIES:
		*len = codec_encode_counties(&v.counties, buf);
		break;
	case WX_GATT_CHR_MODE:
		*len = codec_encode_mode(&v.mode, buf);
		break;
	case WX_GATT_CHR_EVENT_FILTER:
		*len = codec_encode_filter(&v.filter, buf);
		break;
	case WX_GATT_CHR_EVENT_TABLE:
		*len = codec_encode_et_read(&v.et, buf);
		break;
	case WX_GATT_CHR_PRESETS:
		*len = codec_encode_presets(&v.presets, buf);
		break;
	case WX_GATT_CHR_STATUS:
		*len = codec_encode_status(&v.status, buf);
		break;
	case WX_GATT_CHR_ALERT_LOG:
		*len = codec_encode_log_entry(&v.log, buf);
		break;
	default:
		return BLE_ATT_ERR_READ_NOT_PERMITTED;
	}
	return 0;
}

static int write_event_table(struct ble *b, const uint8_t *buf, size_t len)
{
	struct codec_et_write w;
	union ble_value v;
	int err = codec_decode_et_write(buf, len, &w);

	if (err != CODEC_OK) {
		return codec_err(err);
	}
	switch (w.op) {
	case CODEC_ET_SELECT:
		memset(&v, 0, sizeof(v));
		v.et.index = w.index;
		if (b->app->read(b->app_user, WX_GATT_CHR_EVENT_TABLE, &v) != 0) {
			return WX_GATT_ERR_INDEX;
		}
		b->et_index = w.index;
		return 0;
	case CODEC_ET_BEGIN:
		if (w.count == 0U) {
			return WX_GATT_ERR_VALUE; /* an empty table would silence every alert */
		}
		event_table_clear(&staging, w.version);
		b->et_staging = true;
		b->et_expected = w.count;
		return 0;
	case CODEC_ET_ENTRY:
		if (!b->et_staging || w.index != staging.count || staging.count >= b->et_expected) {
			return WX_GATT_ERR_SEQUENCE;
		}
		if (event_table_add(&staging, w.entry.code, w.entry.name,
				    (enum event_class)w.entry.cls) != EVENT_TABLE_OK) {
			return WX_GATT_ERR_VALUE; /* a repeated code */
		}
		return 0;
	case CODEC_ET_COMMIT:
		if (!b->et_staging || staging.count != b->et_expected) {
			return WX_GATT_ERR_SEQUENCE;
		}
		b->et_staging = false;
		b->et_index = 0;
		v.table = &staging;
		err = app_err(b->app->write(b->app_user, WX_GATT_CHR_EVENT_TABLE, &v));
		if (err != WX_GATT_ERR_VALUE) {
			b->stats.writes++;
			force_status(b);
		}
		return err;
	default: /* CODEC_ET_ABORT */
		b->et_staging = false;
		return 0;
	}
}

static int write_control(struct ble *b, const uint8_t *buf, size_t len)
{
	enum codec_command cmd;
	int err = codec_decode_command(buf, len, &cmd);

	if (err != CODEC_OK) {
		return codec_err(err);
	}
	if (b->confirm != BLE_SCREEN_NONE) {
		return WX_GATT_ERR_BUSY;
	}
	if (cmd == CODEC_CMD_FACTORY_RESET) {
		ask(b, BLE_SCREEN_CONFIRM_RESET);
		indicate_control(b, cmd, CODEC_CTRL_AWAITING_CONFIRMATION);
		return 0;
	}
	indicate_control(b, cmd,
			 b->app->command(b->app_user, cmd) == 0 ? CODEC_CTRL_DONE
								 : CODEC_CTRL_REJECTED);
	return 0;
}

/* Decodes a settings value into v. */
static int decode_setting(enum wx_gatt_chr chr, const uint8_t *buf, size_t len,
			  union ble_value *v)
{
	struct tz_info tz;
	int err;

	switch (chr) {
	case WX_GATT_CHR_COUNTIES:
	case WX_GATT_CHR_TRAVEL_COUNTIES:
		return codec_err(codec_decode_counties(buf, len, &v->counties));
	case WX_GATT_CHR_MODE:
		return codec_err(codec_decode_mode(buf, len, &v->mode));
	case WX_GATT_CHR_EVENT_FILTER:
		return codec_err(codec_decode_filter(buf, len, &v->filter));
	case WX_GATT_CHR_PRESETS:
		return codec_err(codec_decode_presets(buf, len, &v->presets));
	case WX_GATT_CHR_TIME:
		err = codec_err(codec_decode_time(buf, len, &v->time));
		if (err == 0 && tz_parse(v->time.tz, &tz) != 0) {
			err = WX_GATT_ERR_VALUE;
		}
		return err;
	default:
		return BLE_ATT_ERR_WRITE_NOT_PERMITTED;
	}
}

int ble_write(struct ble *b, enum wx_gatt_chr chr, const uint8_t *buf, size_t len)
{
	union ble_value v;
	uint8_t index;
	int err;

	if ((unsigned int)chr >= WX_GATT_CHR_COUNT || !(props[chr] & WX_GATT_WRITE)) {
		b->stats.rejected++;
		return BLE_ATT_ERR_WRITE_NOT_PERMITTED;
	}
	if (!b->secure) {
		b->stats.refused++;
		return BLE_ATT_ERR_AUTHENTICATION;
	}
	switch (chr) {
	case WX_GATT_CHR_EVENT_TABLE:
		err = write_event_table(b, buf, len);
		break;
	case WX_GATT_CHR_CONTROL:
		err = write_control(b, buf, len);
		break;
	case WX_GATT_CHR_ALERT_LOG:
		err = codec_err(codec_decode_log_select(buf, len, &index));
		if (err == 0) {
			b->log_index = index;
		}
		break;
	default:
		memset(&v, 0, sizeof(v));
		err = decode_setting(chr, buf, len, &v);
		if (err == 0) {
			err = app_err(b->app->write(b->app_user, chr, &v));
			if (err != WX_GATT_ERR_VALUE) {
				b->stats.writes++;
				force_status(b);
			}
		}
		break;
	}
	if (err == WX_GATT_ERR_STORAGE) {
		b->stats.storage_errors++;
	} else if (err != 0) {
		b->stats.rejected++;
	}
	return err;
}
