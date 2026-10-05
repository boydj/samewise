/*
 * Bluetooth settings service on native_sim: the whole radio with a fake
 * Bluetooth stack (struct ble_port). Covers the connection policy, every
 * characteristic through app/ble_link.c, Control and the 32 MHz crystal.
 * The same behaviour over Zephyr's real host runs on nrf52_bsim.
 */

#include <string.h>
#include <zephyr/ztest.h>

#include "app/alert_log.h"
#include "app/radio.h"
#include "fakes/alert_out_fake.h"
#include "fakes/audio_out_fake.h"
#include "fakes/battery_fake.h"
#include "fakes/clock_fake.h"
#include "fakes/input_fake.h"
#include "fakes/storage_fake.h"
#include "fakes/tuner_fake.h"
#include "fakes/watchdog_fake.h"
#include "services/ble/ble_service.h"
#include "services/same/same_decoder.h"

#define SECOND 1000
#define MINUTE (60 * SECOND)
/* 2026-10-01T00:00:00Z */
#define EPOCH 1790812800LL

/* ---- Fake stack ---- */

static struct {
	bool adv;
	bool bonded_only;
	bool pairable;
	uint8_t bonds;
	uint32_t disconnects;
	uint32_t unpair_all;
	uint32_t notes[WX_GATT_CHR_COUNT];
	uint8_t last[WX_GATT_CHR_COUNT][WX_GATT_MAX_ALERT_LOG];
	size_t last_len[WX_GATT_CHR_COUNT];
} port;

static int port_adv_start(void *user, bool bonded_only)
{
	ARG_UNUSED(user);
	port.adv = true;
	port.bonded_only = bonded_only;
	return 0;
}

static void port_adv_stop(void *user)
{
	ARG_UNUSED(user);
	port.adv = false;
}

static void port_set_pairable(void *user, bool on)
{
	ARG_UNUSED(user);
	port.pairable = on;
}

static uint8_t port_bond_count(void *user)
{
	ARG_UNUSED(user);
	return port.bonds;
}

static void port_disconnect(void *user)
{
	ARG_UNUSED(user);
	port.disconnects++;
}

static void port_unpair_all(void *user)
{
	ARG_UNUSED(user);
	port.unpair_all++;
	port.bonds = 0;
}

static int port_notify(void *user, enum wx_gatt_chr chr, const uint8_t *data, size_t len)
{
	ARG_UNUSED(user);
	port.notes[chr]++;
	memcpy(port.last[chr], data, len);
	port.last_len[chr] = len;
	return 0;
}

static const struct ble_port fake_port = {
	.adv_start = port_adv_start,
	.adv_stop = port_adv_stop,
	.set_pairable = port_set_pairable,
	.bond_count = port_bond_count,
	.disconnect = port_disconnect,
	.unpair_all = port_unpair_all,
	.notify = port_notify,
};

/* ---- Radio ---- */

static struct radio radio;
static struct radio *r = &radio;
static struct ble *b = &radio.ble;
static uint32_t watchdog_resets;
static uint8_t buf[WX_GATT_MAX_ALERT_LOG];
static size_t n;

static void on_watchdog_reset(void *user)
{
	ARG_UNUSED(user);
	watchdog_resets++;
}

/* Let time pass with healthy heartbeats and low-priority work. */
static void run_ms(int64_t ms)
{
	while (ms > 0) {
		int64_t step = ms < SECOND ? ms : SECOND;

		radio_idle_audio(r, (uint32_t)SAME_MS_TO_SAMPLES(step));
		clock_fake_advance_ms(step);
		radio_low_priority(r);
		ms -= step;
	}
}

static void before(void *f)
{
	static const struct health_config cfg = HEALTH_CONFIG_DEFAULT;

	ARG_UNUSED(f);
	memset(&port, 0, sizeof(port));
	clock_fake_reset();
	storage_fake_init();
	alert_out_fake_init();
	audio_out_fake_init();
	input_fake_init();
	tuner_fake_init();
	battery_fake_init();
	watchdog_resets = 0;
	watchdog_fake_init(on_watchdog_reset, NULL);
	radio_boot(r, &cfg, EPOCH);
	radio_ble_start(r, &fake_port, NULL);
	run_ms(2 * SECOND);
}

static void after(void *f)
{
	ARG_UNUSED(f);
	zassert_equal(watchdog_resets, 0, "the radio stayed healthy");
}

ZTEST_SUITE(ble_service, NULL, NULL, before, after, NULL);

static void press(enum hal_input_event_type type, uint32_t keys)
{
	input_fake_emit(type, keys);
}

#define LONG_BAND()  press(HAL_INPUT_LONG_PRESS, HAL_INPUT_KEY_BAND)
#define LONG_STBY()  press(HAL_INPUT_LONG_PRESS, HAL_INPUT_KEY_STBY)
#define PAIR_COMBO() press(HAL_INPUT_COMBO, HAL_INPUT_KEY_BAND | HAL_INPUT_KEY_STBY)

/* A phone pairs inside the pairing window: the stack shows a passkey, then bonds. */
static void pair_phone(void)
{
	zassert_true(port.pairable && port.adv, "pairing window open");
	ble_on_connected(b);
	ble_on_passkey(b, 123456);
	zassert_equal(r->ui.screen, UI_SCREEN_BLUETOOTH);
	zassert_equal(r->ui.ble_screen, BLE_SCREEN_PASSKEY);
	zassert_equal(r->ui.passkey, 123456);
	ble_on_security(b, true);
	ble_on_pairing_done(b, true);
	port.bonds = port.bonds < BLE_MAX_BONDS ? port.bonds + 1 : port.bonds;
}

/* A bonded phone connects and encrypts with its stored LESC keys. */
static void connect_bonded(void)
{
	zassert_true(port.adv && port.bonded_only, "connect window open");
	ble_on_connected(b);
	ble_on_security(b, true);
}

static void disconnect(void)
{
	ble_on_disconnected(b);
}

static int wr(enum wx_gatt_chr chr, size_t len)
{
	return ble_write(b, chr, buf, len);
}

static int rd(enum wx_gatt_chr chr)
{
	return ble_read(b, chr, buf, &n);
}

/* ---- Connection policy ---- */

ZTEST(ble_service, test_no_advertising_by_default)
{
	zassert_false(port.adv);
	zassert_false(port.pairable);
	zassert_false(clock_fake_hfxo_on(), "crystal off with Bluetooth idle");
	run_ms(10 * MINUTE);
	zassert_false(port.adv, "still off");
	zassert_equal(r->ui.screen, UI_SCREEN_STANDBY);
}

ZTEST(ble_service, test_connect_window_two_minutes_bonded_only)
{
	LONG_BAND();
	zassert_true(port.adv);
	zassert_true(port.bonded_only, "filter accept list");
	zassert_false(port.pairable, "no pairing in the connect window");
	zassert_equal(r->ui.ble_screen, BLE_SCREEN_CONNECT);
	zassert_true(clock_fake_hfxo_on());

	run_ms(BLE_CONNECT_WINDOW_MS - 1);
	zassert_true(port.adv);
	run_ms(1);
	zassert_false(port.adv, "window closed");
	zassert_false(clock_fake_hfxo_on(), "crystal released");
	zassert_equal(r->ui.screen, UI_SCREEN_STANDBY);
}

ZTEST(ble_service, test_bonded_phone_reconnects_only_inside_connect_window)
{
	port.bonds = 1;
	zassert_false(port.adv, "outside a window nobody can connect");

	LONG_BAND();
	connect_bonded();
	zassert_false(port.adv, "advertising stops on connect");
	zassert_equal(r->ui.ble_screen, BLE_SCREEN_NONE);
	zassert_true(clock_fake_hfxo_on(), "crystal held while connected");
	zassert_ok(rd(WX_GATT_CHR_MODE));

	run_ms(10 * MINUTE);
	zassert_true(clock_fake_hfxo_on(), "connection outlives the window");
	disconnect();
	zassert_false(clock_fake_hfxo_on());
	zassert_false(port.adv, "no advertising after disconnect");
}

ZTEST(ble_service, test_pairing_window)
{
	PAIR_COMBO();
	zassert_true(port.pairable);
	zassert_true(port.adv);
	zassert_false(port.bonded_only, "any phone may connect to pair");
	zassert_equal(r->ui.ble_screen, BLE_SCREEN_PAIRING);
	zassert_true(clock_fake_hfxo_on());

	pair_phone();
	zassert_false(port.pairable, "window closes on bonding");
	zassert_equal(r->ui.ble_screen, BLE_SCREEN_NONE);
	zassert_ok(rd(WX_GATT_CHR_COUNTIES));
	disconnect();
	zassert_false(clock_fake_hfxo_on());
}

ZTEST(ble_service, test_pairing_window_expires_and_wrong_passkey_keeps_it_open)
{
	PAIR_COMBO();
	ble_on_connected(b);
	ble_on_passkey(b, 654321);
	ble_on_pairing_done(b, false); /* the phone entered the wrong passkey */
	zassert_true(port.pairable, "still open for another try");
	zassert_equal(r->ui.ble_screen, BLE_SCREEN_PAIRING, "passkey cleared");
	zassert_equal(rd(WX_GATT_CHR_COUNTIES), BLE_ATT_ERR_AUTHENTICATION);

	run_ms(BLE_PAIRING_WINDOW_MS);
	zassert_false(port.pairable);
	zassert_false(port.adv);
	zassert_equal(port.disconnects, 1, "an unpaired phone is dropped when the window ends");
	disconnect();
	zassert_false(clock_fake_hfxo_on());
	zassert_equal(r->ui.screen, UI_SCREEN_STANDBY);
}

ZTEST(ble_service, test_passkey_outside_window_ignored)
{
	ble_on_connected(b);
	ble_on_passkey(b, 111111);
	zassert_equal(r->ui.ble_screen, BLE_SCREEN_NONE);
	disconnect();
}

ZTEST(ble_service, test_unauthenticated_link_refused_everywhere)
{
	PAIR_COMBO();
	ble_on_connected(b);
	ble_on_security(b, false); /* encrypted without LESC authentication */
	for (int c = 0; c < WX_GATT_CHR_COUNT; c++) {
		int err = ble_read(b, (enum wx_gatt_chr)c, buf, &n);

		zassert_true(err == BLE_ATT_ERR_AUTHENTICATION || err == BLE_ATT_ERR_READ_NOT_PERMITTED,
			     "read %d", c);
		buf[0] = CODEC_SCHEMA;
		buf[1] = 1;
		err = ble_write(b, (enum wx_gatt_chr)c, buf, 2);
		zassert_true(err == BLE_ATT_ERR_AUTHENTICATION ||
				     err == BLE_ATT_ERR_WRITE_NOT_PERMITTED,
			     "write %d", c);
	}
	zassert_equal(b->stats.writes, 0);
	zassert_true(b->stats.refused > 0);
}

ZTEST(ble_service, test_third_bond_needs_confirmation)
{
	port.bonds = 2;
	PAIR_COMBO();
	zassert_false(port.pairable, "not before confirming");
	zassert_equal(r->ui.ble_screen, BLE_SCREEN_CONFIRM_BOND);

	press(HAL_INPUT_PRESS, HAL_INPUT_KEY_VOL_UP);
	zassert_equal(r->ui.ble_screen, BLE_SCREEN_NONE, "any other key cancels");
	zassert_false(port.pairable);

	PAIR_COMBO();
	run_ms(BLE_CONFIRM_MS);
	zassert_equal(r->ui.ble_screen, BLE_SCREEN_NONE, "30 seconds cancels");
	zassert_false(port.pairable);
	zassert_false(clock_fake_hfxo_on());

	PAIR_COMBO();
	LONG_STBY();
	zassert_true(port.pairable, "confirmed: the pairing window opens");
	pair_phone();
	zassert_equal(port.bonds, 2, "the stack replaced the oldest");
	zassert_equal(alert_mgr_state(&r->alerts), ALERT_STATE_STANDBY, "keys didn't leave standby");
}

ZTEST(ble_service, test_keys_ignored_while_locked_or_alerting)
{
	press(HAL_INPUT_LOCK_ON, 0);
	LONG_BAND();
	PAIR_COMBO();
	zassert_false(port.adv, "lock on");
	press(HAL_INPUT_LOCK_OFF, 0);

	zassert_ok(alert_mgr_test_alert(&r->alerts));
	LONG_BAND();
	zassert_equal(alert_mgr_state(&r->alerts), ALERT_STATE_SILENCED);
	zassert_false(port.adv, "the press silenced the alert, nothing more");
}

/* ---- Characteristics ---- */

static void connected(void)
{
	port.bonds = 1;
	LONG_BAND();
	connect_bonded();
}

ZTEST(ble_service, test_sixteen_counties_write_and_read_back)
{
	struct codec_counties in = {.count = 16}, out;

	connected();
	for (uint8_t i = 0; i < 16; i++) {
		in.codes[i] = (struct same_location){.subdivision = i % 10, .state = 48,
						     .county = (uint16_t)(1 + 2 * i)};
	}
	zassert_ok(wr(WX_GATT_CHR_COUNTIES, codec_encode_counties(&in, buf)));
	memset(buf, 0, sizeof(buf));
	zassert_ok(rd(WX_GATT_CHR_COUNTIES));
	zassert_equal(n, WX_GATT_MAX_COUNTIES);
	zassert_ok(codec_decode_counties(buf, n, &out));
	zassert_equal(out.count, 16);
	zassert_mem_equal(out.codes, in.codes, sizeof(in.codes));
	zassert_equal(r->settings.home.count, 16, "in effect");

	struct settings loaded;

	zassert_equal(settings_load(&loaded), 0);
	zassert_equal(loaded.home.count, 16, "persisted");
}

ZTEST(ble_service, test_every_setting_round_trips)
{
	struct codec_counties travel = {.count = 1, .codes = {{0, 6, 37}}};
	struct codec_mode mode = {.mode = 1, .channel = 3}, m2;
	struct codec_filter filter = {.preset = FILTER_CUSTOM, .bitmap = {0x0F}}, f2;
	struct codec_presets presets = {.count = 2, .p = {{CODEC_BAND_FM, 90500},
							  {CODEC_BAND_WB, 162550}}}, p2;
	struct codec_counties c2;

	connected();
	zassert_ok(wr(WX_GATT_CHR_TRAVEL_COUNTIES, codec_encode_counties(&travel, buf)));
	zassert_ok(wr(WX_GATT_CHR_MODE, codec_encode_mode(&mode, buf)));
	zassert_ok(wr(WX_GATT_CHR_EVENT_FILTER, codec_encode_filter(&filter, buf)));
	zassert_ok(wr(WX_GATT_CHR_PRESETS, codec_encode_presets(&presets, buf)));

	zassert_ok(rd(WX_GATT_CHR_TRAVEL_COUNTIES));
	zassert_ok(codec_decode_counties(buf, n, &c2));
	zassert_equal(c2.codes[0].county, 37);
	zassert_ok(rd(WX_GATT_CHR_MODE));
	zassert_ok(codec_decode_mode(buf, n, &m2));
	zassert_mem_equal(&m2, &mode, sizeof(mode));
	zassert_equal(tuner_fake_last_tune_khz(), 162450, "channel 3 tuned");
	zassert_ok(rd(WX_GATT_CHR_EVENT_FILTER));
	zassert_ok(codec_decode_filter(buf, n, &f2));
	zassert_mem_equal(&f2, &filter, sizeof(filter));
	zassert_ok(rd(WX_GATT_CHR_PRESETS));
	zassert_ok(codec_decode_presets(buf, n, &p2));
	zassert_equal(p2.p[1].khz, 162550);
	zassert_equal(settings_active_counties(&r->settings)->codes[0].state, 6, "travel mode");
	zassert_equal(rd(WX_GATT_CHR_TIME), BLE_ATT_ERR_READ_NOT_PERMITTED, "write only");
	zassert_equal(b->stats.writes, 4);
}

ZTEST(ble_service, test_event_table_indexed_read_and_staged_write)
{
	struct codec_et_write w = {.op = CODEC_ET_BEGIN, .version = 9, .count = 2};
	struct codec_et_read e;
	uint16_t old_version = r->settings.events.version;

	connected();
	zassert_ok(rd(WX_GATT_CHR_EVENT_TABLE));
	zassert_ok(codec_decode_et_read(buf, n, &e));
	zassert_equal(e.index, 0);
	zassert_equal(e.count, r->settings.events.count);
	w.op = CODEC_ET_SELECT;
	w.index = e.count - 1;
	zassert_ok(wr(WX_GATT_CHR_EVENT_TABLE, codec_encode_et_write(&w, buf)));
	zassert_ok(rd(WX_GATT_CHR_EVENT_TABLE));
	zassert_ok(codec_decode_et_read(buf, n, &e));
	zassert_equal(e.index, e.count - 1);
	w.index = e.count;
	zassert_equal(wr(WX_GATT_CHR_EVENT_TABLE, codec_encode_et_write(&w, buf)),
		      WX_GATT_ERR_INDEX);

	/* Entry before begin, out of order, short commit: all refused, table unchanged. */
	w = (struct codec_et_write){.op = CODEC_ET_ENTRY, .index = 0,
				    .entry = {"TOR", "Tornado Warning", EVENT_CLASS_WARNING}};
	zassert_equal(wr(WX_GATT_CHR_EVENT_TABLE, codec_encode_et_write(&w, buf)),
		      WX_GATT_ERR_SEQUENCE);
	w = (struct codec_et_write){.op = CODEC_ET_BEGIN, .version = 9, .count = 2};
	zassert_ok(wr(WX_GATT_CHR_EVENT_TABLE, codec_encode_et_write(&w, buf)));
	w = (struct codec_et_write){.op = CODEC_ET_ENTRY, .index = 1,
				    .entry = {"TOR", "Tornado Warning", EVENT_CLASS_WARNING}};
	zassert_equal(wr(WX_GATT_CHR_EVENT_TABLE, codec_encode_et_write(&w, buf)),
		      WX_GATT_ERR_SEQUENCE);
	w.index = 0;
	zassert_ok(wr(WX_GATT_CHR_EVENT_TABLE, codec_encode_et_write(&w, buf)));
	w.index = 1;
	zassert_equal(wr(WX_GATT_CHR_EVENT_TABLE, codec_encode_et_write(&w, buf)),
		      WX_GATT_ERR_VALUE, "repeated code");
	w = (struct codec_et_write){.op = CODEC_ET_COMMIT};
	zassert_equal(wr(WX_GATT_CHR_EVENT_TABLE, codec_encode_et_write(&w, buf)),
		      WX_GATT_ERR_SEQUENCE, "one entry short");
	zassert_equal(r->settings.events.version, old_version, "unchanged");

	w = (struct codec_et_write){.op = CODEC_ET_ENTRY, .index = 1,
				    .entry = {"RWT", "Required Weekly Test", EVENT_CLASS_TEST}};
	zassert_ok(wr(WX_GATT_CHR_EVENT_TABLE, codec_encode_et_write(&w, buf)));
	w = (struct codec_et_write){.op = CODEC_ET_COMMIT};
	zassert_ok(wr(WX_GATT_CHR_EVENT_TABLE, codec_encode_et_write(&w, buf)));
	zassert_equal(r->settings.events.version, 9);
	zassert_equal(r->settings.events.count, 2);

	w = (struct codec_et_write){.op = CODEC_ET_BEGIN, .version = 10, .count = 0};
	zassert_equal(wr(WX_GATT_CHR_EVENT_TABLE, codec_encode_et_write(&w, buf)),
		      WX_GATT_ERR_VALUE, "an empty table would silence every alert");
}

ZTEST(ble_service, test_time_sets_clock_and_local_time)
{
	struct codec_time t = {.utc = 1783036800}; /* 2026-07-03 00:00 UTC */
	bool dst;

	connected();
	strcpy(t.tz, "EST5EDT,M3.2.0,M11.1.0");
	zassert_ok(wr(WX_GATT_CHR_TIME, codec_encode_time(&t, buf)));
	zassert_equal(hal_clock_utc_s(), 1783036800);
	zassert_equal(settings_local_time(&r->settings, hal_clock_utc_s(), &dst),
		      1783036800 - 4 * 3600);
	zassert_true(dst);

	strcpy(t.tz, "EST5EDT");
	t.utc = 5;
	zassert_equal(wr(WX_GATT_CHR_TIME, codec_encode_time(&t, buf)), WX_GATT_ERR_VALUE);
	zassert_equal(hal_clock_utc_s(), 1783036800, "clock untouched by a rejected write");
	zassert_str_equal(r->settings.tz, "EST5EDT,M3.2.0,M11.1.0");
}

ZTEST(ble_service, test_invalid_writes_leave_settings_unchanged)
{
	struct codec_counties c = {.count = 1, .codes = {{0, 48, 453}}};
	struct settings before_write;
	uint32_t writes = storage_fake_writes();

	connected();
	before_write = r->settings;
	n = codec_encode_counties(&c, buf);
	zassert_equal(wr(WX_GATT_CHR_COUNTIES, n - 1), WX_GATT_ERR_LENGTH, "truncated");
	buf[n] = 0;
	zassert_equal(wr(WX_GATT_CHR_COUNTIES, n + 1), WX_GATT_ERR_LENGTH, "trailing byte");
	buf[0] = 2;
	zassert_equal(wr(WX_GATT_CHR_COUNTIES, n), WX_GATT_ERR_SCHEMA, "schema 2");
	buf[0] = CODEC_SCHEMA;
	buf[2] = 'X';
	zassert_equal(wr(WX_GATT_CHR_COUNTIES, n), WX_GATT_ERR_VALUE, "not a digit");
	buf[0] = CODEC_SCHEMA;
	buf[1] = 1;
	buf[2] = 9;
	zassert_equal(wr(WX_GATT_CHR_MODE, 3), WX_GATT_ERR_VALUE, "channel 9");
	zassert_equal(wr(WX_GATT_CHR_STATUS, 3), BLE_ATT_ERR_WRITE_NOT_PERMITTED);
	zassert_mem_equal(&r->settings, &before_write, sizeof(before_write));
	zassert_equal(storage_fake_writes(), writes, "nothing saved");
	zassert_equal(b->stats.rejected, 6);
	zassert_equal(port.notes[WX_GATT_CHR_STATUS], 0, "no notification for a rejected write");
}

ZTEST(ble_service, test_storage_failure_reported)
{
	struct codec_mode mode = {.mode = 0, .channel = 2};

	connected();
	storage_fake_fail_writes(true);
	zassert_equal(wr(WX_GATT_CHR_MODE, codec_encode_mode(&mode, buf)), WX_GATT_ERR_STORAGE);
	storage_fake_fail_writes(false);
	zassert_equal(r->settings.channel, 2, "applied until restart");
	zassert_equal(b->stats.storage_errors, 1);
}

/* ---- Notifications ---- */

static struct codec_status last_status(void)
{
	struct codec_status st;

	zassert_ok(codec_decode_status(port.last[WX_GATT_CHR_STATUS],
				       port.last_len[WX_GATT_CHR_STATUS], &st));
	return st;
}

ZTEST(ble_service, test_status_notifies_on_battery_signal_and_writes)
{
	struct codec_mode mode = {.mode = 0, .channel = 0};
	uint32_t base;

	tuner_fake_set_signal(40, 25);
	connected();
	run_ms(2 * SECOND);
	base = port.notes[WX_GATT_CHR_STATUS];
	zassert_true(base >= 1, "first poll after connecting sends a baseline");
	zassert_equal(last_status().snr_db, 25);

	tuner_fake_set_signal(41, 23);
	run_ms(5 * SECOND);
	zassert_equal(port.notes[WX_GATT_CHR_STATUS], base, "2 dB: quiet");
	tuner_fake_set_signal(43, 22);
	run_ms(5 * SECOND);
	zassert_equal(port.notes[WX_GATT_CHR_STATUS], base + 1, "3 dB from the last notified");
	zassert_equal(last_status().rssi_dbuv, 43);

	battery_fake_set(57, 3800, HAL_BATTERY_DISCHARGING);
	run_ms(2 * MINUTE);
	zassert_equal(last_status().battery_percent, 57);
	base = port.notes[WX_GATT_CHR_STATUS];

	zassert_ok(wr(WX_GATT_CHR_MODE, codec_encode_mode(&mode, buf)));
	zassert_equal(port.notes[WX_GATT_CHR_STATUS], base + 1, "a write notifies Status");
}

ZTEST(ble_service, test_status_fields)
{
	struct codec_status st;

	zassert_ok(hal_clock_set_utc(EPOCH + 3 * 86400));
	connected();
	press(HAL_INPUT_LOCK_ON, 0);
	zassert_ok(rd(WX_GATT_CHR_STATUS));
	zassert_ok(codec_decode_status(buf, n, &st));
	zassert_equal(st.locked, 1);
	zassert_equal(st.channel, 7, "162.550 MHz");
	zassert_equal(st.last_rwt_utc, -1, "no weekly test yet");
	zassert_equal(st.hours_left, 0xFFFF, "not estimated yet");
}

ZTEST(ble_service, test_alert_log_notifies_new_alert_and_reads_by_index)
{
	struct codec_log_entry e;

	zassert_ok(hal_clock_set_utc(EPOCH + 86400));
	connected();
	zassert_ok(alert_mgr_test_alert(&r->alerts));
	run_ms(SECOND);
	zassert_equal(port.notes[WX_GATT_CHR_ALERT_LOG], 1);
	zassert_ok(codec_decode_log_entry(port.last[WX_GATT_CHR_ALERT_LOG],
					  port.last_len[WX_GATT_CHR_ALERT_LOG], &e));
	zassert_equal(e.outcome, ALERT_LOG_TEST);
	zassert_str_equal(e.raw, "TEST ALERT");

	zassert_ok(rd(WX_GATT_CHR_ALERT_LOG));
	zassert_ok(codec_decode_log_entry(buf, n, &e));
	zassert_equal(e.count, 1);
	zassert_equal(e.index, 0);
	zassert_ok(wr(WX_GATT_CHR_ALERT_LOG, codec_encode_log_select(1, buf)));
	zassert_equal(rd(WX_GATT_CHR_ALERT_LOG), WX_GATT_ERR_INDEX, "only one entry");
}

/* ---- Control ---- */

static struct codec_control_indication last_control(void)
{
	struct codec_control_indication ind;

	zassert_ok(codec_decode_control_indication(port.last[WX_GATT_CHR_CONTROL],
						   port.last_len[WX_GATT_CHR_CONTROL], &ind));
	return ind;
}

ZTEST(ble_service, test_control_test_alert_and_clear_log)
{
	connected();
	zassert_ok(wr(WX_GATT_CHR_CONTROL, codec_encode_command(CODEC_CMD_TEST_ALERT, buf)));
	zassert_equal(last_control().result, CODEC_CTRL_DONE);
	zassert_equal(alert_mgr_state(&r->alerts), ALERT_STATE_ALERTING);
	zassert_equal(alert_out_fake_buzzer(), HAL_ALERT_PATTERN_ALERT);

	zassert_ok(wr(WX_GATT_CHR_CONTROL, codec_encode_command(CODEC_CMD_TEST_ALERT, buf)));
	zassert_equal(last_control().result, CODEC_CTRL_REJECTED, "already alerting");

	run_ms(SECOND);
	zassert_equal(alert_log_count(), 1);
	zassert_ok(wr(WX_GATT_CHR_CONTROL, codec_encode_command(CODEC_CMD_CLEAR_LOG, buf)));
	zassert_equal(last_control().command, CODEC_CMD_CLEAR_LOG);
	zassert_equal(last_control().result, CODEC_CTRL_DONE);
	zassert_equal(alert_log_count(), 0);
}

ZTEST(ble_service, test_factory_reset_waits_for_confirmation)
{
	struct codec_counties c = {.count = 1, .codes = {{0, 48, 453}}};

	connected();
	zassert_ok(wr(WX_GATT_CHR_COUNTIES, codec_encode_counties(&c, buf)));
	zassert_ok(wr(WX_GATT_CHR_CONTROL, codec_encode_command(CODEC_CMD_FACTORY_RESET, buf)));
	zassert_equal(last_control().result, CODEC_CTRL_AWAITING_CONFIRMATION);
	zassert_equal(r->ui.ble_screen, BLE_SCREEN_CONFIRM_RESET);
	zassert_equal(r->settings.home.count, 1, "nothing yet");
	zassert_equal(wr(WX_GATT_CHR_CONTROL, codec_encode_command(CODEC_CMD_CLEAR_LOG, buf)),
		      WX_GATT_ERR_BUSY);

	/* 30 seconds without a key cancels. */
	run_ms(BLE_CONFIRM_MS);
	zassert_equal(last_control().result, CODEC_CTRL_CANCELLED);
	zassert_equal(r->settings.home.count, 1);

	/* Another key cancels. */
	zassert_ok(wr(WX_GATT_CHR_CONTROL, codec_encode_command(CODEC_CMD_FACTORY_RESET, buf)));
	press(HAL_INPUT_PRESS, HAL_INPUT_KEY_STBY);
	zassert_equal(last_control().result, CODEC_CTRL_CANCELLED, "a short press isn't enough");
	zassert_equal(r->settings.home.count, 1);
	zassert_equal(port.unpair_all, 0);

	/* Long-press STBY confirms. */
	zassert_ok(wr(WX_GATT_CHR_CONTROL, codec_encode_command(CODEC_CMD_FACTORY_RESET, buf)));
	LONG_STBY();
	zassert_equal(last_control().result, CODEC_CTRL_DONE);
	zassert_equal(r->settings.home.count, 0, "settings back to defaults");
	zassert_equal(port.unpair_all, 1, "bonds removed");
	zassert_equal(r->ui.ble_screen, BLE_SCREEN_NONE);
}

ZTEST(ble_service, test_factory_reset_cancelled_when_phone_leaves)
{
	connected();
	zassert_ok(wr(WX_GATT_CHR_CONTROL, codec_encode_command(CODEC_CMD_FACTORY_RESET, buf)));
	disconnect();
	zassert_equal(r->ui.ble_screen, BLE_SCREEN_NONE);
	LONG_STBY();
	zassert_equal(port.unpair_all, 0);
}

/* ---- Crystal ---- */

ZTEST(ble_service, test_crystal_only_while_window_or_connected)
{
	zassert_false(clock_fake_hfxo_on());
	PAIR_COMBO();
	zassert_true(clock_fake_hfxo_on());
	pair_phone();
	zassert_true(clock_fake_hfxo_on(), "connected");
	disconnect();
	zassert_false(clock_fake_hfxo_on());
	LONG_BAND();
	zassert_true(clock_fake_hfxo_on());
	run_ms(BLE_CONNECT_WINDOW_MS);
	zassert_false(clock_fake_hfxo_on());
	zassert_equal(clock_fake_hfxo_misuse(), 0);
}
