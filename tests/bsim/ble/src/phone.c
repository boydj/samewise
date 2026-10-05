/*
 * Devices 1-3: phones (Bluetooth centrals). Phone 1 scripts the test;
 * phones 2 and 3 bond later to exercise the two-bond limit and run the
 * factory reset. They ask the radio, over the backchannel, to press keys
 * and report its state, and read the passkey off its screen.
 */

#include <errno.h>
#include <string.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/kernel.h>

#include "babblekit/testcase.h"
#include "bstests.h"
#include "hal/input.h"
#include "link.h"
#include "services/ble/ble_service.h"
#include "services/ble/codec.h"
#include "services/ble/gatt_table.h"

#define SMP_CHR (WX_GATT_CHR_COUNT) /* index of the MCUmgr SMP characteristic */
#define NO_ATT_ERR 0xFFFF

static const struct bt_uuid_128 smp_uuid = BT_UUID_INIT_128(
	BT_UUID_128_ENCODE(0xda2e7828, 0xfbce, 0x4e01, 0xae9e, 0x261174997c48));

static bool active;
static struct bt_conn *conn;
static bt_addr_le_t radio_addr;
static uint8_t svc_uuid_le[16];
static uint16_t handles[WX_GATT_CHR_COUNT + 1];

static atomic_t found, connected_flag, conn_failed, disconnected_flag, passkey_wanted;
static atomic_t pairing_done, pairing_ok, secure;

static bool wait_flag(atomic_t *flag, int timeout_ms);

/* ---- Notifications ---- */

static struct bt_gatt_subscribe_params subs[WX_GATT_CHR_COUNT];
static uint8_t last_value[WX_GATT_CHR_COUNT][WX_GATT_MAX_ALERT_LOG];
static uint16_t last_len[WX_GATT_CHR_COUNT];
static atomic_t notes[WX_GATT_CHR_COUNT];

static uint8_t on_notify(struct bt_conn *c, struct bt_gatt_subscribe_params *p, const void *data,
			 uint16_t len)
{
	int chr = p - subs;

	ARG_UNUSED(c);
	if (data == NULL) {
		return BT_GATT_ITER_STOP; /* unsubscribed */
	}
	memcpy(last_value[chr], data, MIN(len, sizeof(last_value[chr])));
	last_len[chr] = len;
	atomic_inc(&notes[chr]);
	return BT_GATT_ITER_CONTINUE;
}

static atomic_t sub_err, sub_done;

static void on_subscribed(struct bt_conn *c, uint8_t err, struct bt_gatt_subscribe_params *p)
{
	ARG_UNUSED(c);
	ARG_UNUSED(p);
	atomic_set(&sub_err, err);
	atomic_set(&sub_done, 1);
}

static void subscribe(enum wx_gatt_chr chr, uint16_t value)
{
	struct bt_gatt_subscribe_params *p = &subs[chr];
	int err;

	memset(p, 0, sizeof(*p));
	atomic_set(&sub_done, 0);
	p->subscribe = on_subscribed;
	p->notify = on_notify;
	p->value = value;
	p->value_handle = handles[chr];
	p->ccc_handle = handles[chr] + 1; /* the CCC follows the value in gatt_table order */
	err = bt_gatt_subscribe(conn, p);
	TEST_ASSERT(err == 0 || err == -EALREADY, "subscribe %d failed (%d)", chr, err);
	if (err == 0) {
		TEST_ASSERT(wait_flag(&sub_done, 5000), "subscribe %d never answered", chr);
		TEST_ASSERT(atomic_get(&sub_err) == 0, "subscribe %d: ATT error %d", chr,
			    (int)atomic_get(&sub_err));
	}
}

/* Wait for the next notification on chr; returns false on timeout. */
static bool wait_note(enum wx_gatt_chr chr, atomic_val_t before, int timeout_ms)
{
	for (int t = 0; t < timeout_ms; t += 10) {
		if (atomic_get(&notes[chr]) > before) {
			return true;
		}
		k_sleep(K_MSEC(10));
	}
	return false;
}

/* ---- Connection ---- */

static void connected(struct bt_conn *c, uint8_t err)
{
	if (!active) {
		return;
	}
	if (err != 0U) {
		if (conn != NULL) {
			bt_conn_unref(conn);
			conn = NULL;
		}
		atomic_set(&conn_failed, 1);
		return;
	}
	atomic_set(&connected_flag, 1);
}

static void disconnected(struct bt_conn *c, uint8_t reason)
{
	ARG_UNUSED(reason);
	if (!active || c != conn) {
		return;
	}
	bt_conn_unref(conn);
	conn = NULL;
	atomic_set(&connected_flag, 0);
	atomic_set(&secure, 0);
	atomic_set(&disconnected_flag, 1);
}

static void security_changed(struct bt_conn *c, bt_security_t level, enum bt_security_err err)
{
	ARG_UNUSED(c);
	if (active) {
		atomic_set(&secure, err == BT_SECURITY_ERR_SUCCESS && level >= BT_SECURITY_L4);
	}
}

BT_CONN_CB_DEFINE(phone_conn_cb) = {
	.connected = connected,
	.disconnected = disconnected,
	.security_changed = security_changed,
};

static void passkey_entry(struct bt_conn *c)
{
	ARG_UNUSED(c);
	atomic_set(&passkey_wanted, 1);
}

static void auth_cancel(struct bt_conn *c)
{
	ARG_UNUSED(c);
}

static struct bt_conn_auth_cb auth_cb = {
	.passkey_entry = passkey_entry,
	.cancel = auth_cancel,
};

static void pairing_complete(struct bt_conn *c, bool bonded)
{
	ARG_UNUSED(c);
	atomic_set(&pairing_ok, bonded);
	atomic_set(&pairing_done, 1);
}

static void pairing_failed(struct bt_conn *c, enum bt_security_err reason)
{
	ARG_UNUSED(c);
	ARG_UNUSED(reason);
	atomic_set(&pairing_ok, 0);
	atomic_set(&pairing_done, 1);
}

static struct bt_conn_auth_info_cb auth_info_cb = {
	.pairing_complete = pairing_complete,
	.pairing_failed = pairing_failed,
};

static bool wait_flag(atomic_t *flag, int timeout_ms)
{
	for (int t = 0; t < timeout_ms; t += 10) {
		if (atomic_get(flag)) {
			return true;
		}
		k_sleep(K_MSEC(10));
	}
	return false;
}

static bool ad_has_service(struct bt_data *data, void *user)
{
	if (data->type == BT_DATA_UUID128_ALL && data->data_len == 16 &&
	    memcmp(data->data, svc_uuid_le, 16) == 0) {
		*(bool *)user = true;
		return false;
	}
	return true;
}

static void on_scan(const bt_addr_le_t *addr, int8_t rssi, uint8_t type, struct net_buf_simple *ad)
{
	bool ours = false;

	ARG_UNUSED(rssi);
	ARG_UNUSED(type);
	bt_data_parse(ad, ad_has_service, &ours);
	if (ours && !atomic_get(&found)) {
		bt_addr_le_copy(&radio_addr, addr);
		atomic_set(&found, 1);
	}
}

/* Scan for the radio's connectable advertising. */
static bool scan_for(int ms)
{
	bool seen;

	atomic_set(&found, 0);
	TEST_ASSERT(bt_le_scan_start(BT_LE_SCAN_PASSIVE, on_scan) == 0, "scan");
	seen = wait_flag(&found, ms);
	(void)bt_le_scan_stop();
	return seen;
}

static bool connect_radio(void)
{
	int err;

	atomic_set(&connected_flag, 0);
	atomic_set(&conn_failed, 0);
	atomic_set(&disconnected_flag, 0);
	err = bt_conn_le_create(&radio_addr, BT_CONN_LE_CREATE_CONN, BT_LE_CONN_PARAM_DEFAULT,
				&conn);
	TEST_ASSERT(err == 0, "create connection (%d)", err);
	for (int t = 0; t < 10000; t += 10) {
		if (atomic_get(&connected_flag)) {
			return true;
		}
		if (atomic_get(&conn_failed)) {
			return false;
		}
		k_sleep(K_MSEC(10));
	}
	TEST_FAIL("connection attempt never ended");
	return false;
}

static void disconnect(void)
{
	TEST_ASSERT(conn != NULL, "not connected");
	(void)bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
	TEST_ASSERT(wait_flag(&disconnected_flag, 5000), "no disconnect");
	k_sleep(K_MSEC(100));
}

static void mtu_done(struct bt_conn *c, uint8_t err, struct bt_gatt_exchange_params *p)
{
	ARG_UNUSED(c);
	ARG_UNUSED(err);
	ARG_UNUSED(p);
}

/* ---- Discovery, reads and writes ---- */

static struct bt_gatt_discover_params disc;
static atomic_t disc_done;

static uint8_t on_discover(struct bt_conn *c, const struct bt_gatt_attr *attr,
			   struct bt_gatt_discover_params *p)
{
	const struct bt_gatt_chrc *chrc;

	ARG_UNUSED(c);
	ARG_UNUSED(p);
	if (attr == NULL) {
		atomic_set(&disc_done, 1);
		return BT_GATT_ITER_STOP;
	}
	chrc = attr->user_data;
	if (bt_uuid_cmp(chrc->uuid, &smp_uuid.uuid) == 0) {
		handles[SMP_CHR] = chrc->value_handle;
		return BT_GATT_ITER_CONTINUE;
	}
	if (chrc->uuid->type == BT_UUID_TYPE_128) {
		const uint8_t *v = BT_UUID_128(chrc->uuid)->val;
		uint16_t offset = (uint16_t)(v[13] << 8 | v[12]);

		if (memcmp(v, svc_uuid_le, 12) == 0 && memcmp(&v[14], &svc_uuid_le[14], 2) == 0 &&
		    offset >= 1 && offset <= WX_GATT_CHR_COUNT) {
			handles[offset - 1] = chrc->value_handle; /* offsets are 1..N in table order */
		}
	}
	return BT_GATT_ITER_CONTINUE;
}

/* big_mtu: negotiate 247 bytes, as iOS does; otherwise stay at 23. */
static void discover(bool big_mtu)
{
	static struct bt_gatt_exchange_params mtu = {.func = mtu_done};

	if (big_mtu) {
		(void)bt_gatt_exchange_mtu(conn, &mtu);
		k_sleep(K_MSEC(200));
	}
	memset(handles, 0, sizeof(handles));
	memset(&disc, 0, sizeof(disc));
	disc.func = on_discover;
	disc.start_handle = BT_ATT_FIRST_ATTRIBUTE_HANDLE;
	disc.end_handle = BT_ATT_LAST_ATTRIBUTE_HANDLE;
	disc.type = BT_GATT_DISCOVER_CHARACTERISTIC;
	atomic_set(&disc_done, 0);
	TEST_ASSERT(bt_gatt_discover(conn, &disc) == 0, "discover");
	TEST_ASSERT(wait_flag(&disc_done, 5000), "discovery never ended");
	for (int i = 0; i <= SMP_CHR; i++) {
		TEST_ASSERT(handles[i] != 0, "characteristic %d not found", i);
	}
}

static struct bt_gatt_read_params read_params;
static uint8_t read_buf[512];
static uint16_t read_len;
static atomic_t read_err, read_done;

static uint8_t on_read(struct bt_conn *c, uint8_t err, struct bt_gatt_read_params *p,
		       const void *data, uint16_t len)
{
	ARG_UNUSED(c);
	ARG_UNUSED(p);
	if (err != 0U || data == NULL) {
		atomic_set(&read_err, err);
		atomic_set(&read_done, 1);
		return BT_GATT_ITER_STOP;
	}
	if (read_len + len <= sizeof(read_buf)) {
		memcpy(&read_buf[read_len], data, len);
	}
	read_len += len;
	return BT_GATT_ITER_CONTINUE;
}

/* Returns 0 or the ATT error; the value lands in read_buf/read_len. */
static int rd(int chr)
{
	memset(&read_params, 0, sizeof(read_params));
	read_params.func = on_read;
	read_params.handle_count = 1;
	read_params.single.handle = handles[chr];
	read_len = 0;
	atomic_set(&read_err, 0);
	atomic_set(&read_done, 0);
	TEST_ASSERT(bt_gatt_read(conn, &read_params) == 0, "read %d", chr);
	TEST_ASSERT(wait_flag(&read_done, 5000), "read %d never ended", chr);
	return (int)atomic_get(&read_err);
}

static struct bt_gatt_write_params write_params;
static atomic_t write_err, write_done;

static void on_write(struct bt_conn *c, uint8_t err, struct bt_gatt_write_params *p)
{
	ARG_UNUSED(c);
	ARG_UNUSED(p);
	atomic_set(&write_err, err);
	atomic_set(&write_done, 1);
}

static int wr(int chr, const uint8_t *data, size_t len)
{
	memset(&write_params, 0, sizeof(write_params));
	write_params.func = on_write;
	write_params.handle = handles[chr];
	write_params.data = data;
	write_params.length = (uint16_t)len;
	atomic_set(&write_err, 0);
	atomic_set(&write_done, 0);
	TEST_ASSERT(bt_gatt_write(conn, &write_params) == 0, "write %d", chr);
	TEST_ASSERT(wait_flag(&write_done, 5000), "write %d never ended", chr);
	return (int)atomic_get(&write_err);
}

/* ---- Pairing ---- */

/* Pair with the passkey shown on the radio's screen, or a wrong one. */
static bool pair(bool right_passkey)
{
	struct link_msg s;
	unsigned int passkey;

	atomic_set(&passkey_wanted, 0);
	atomic_set(&pairing_done, 0);
	atomic_set(&pairing_ok, 0);
	TEST_ASSERT(bt_conn_set_security(conn, BT_SECURITY_L4) == 0, "set security");
	TEST_ASSERT(wait_flag(&passkey_wanted, 5000), "no passkey request");
	do {
		k_sleep(K_MSEC(50));
		s = radio_ask(LINK_QUERY, 0, 0);
	} while (s.d == 0U);
	TEST_ASSERT(s.b == BLE_SCREEN_PASSKEY, "passkey screen (%u)", s.b);
	passkey = right_passkey ? s.d : (s.d + 1U) % 1000000U;
	TEST_ASSERT(bt_conn_auth_passkey_entry(conn, passkey) == 0, "passkey entry");
	TEST_ASSERT(wait_flag(&pairing_done, 10000), "pairing never ended");
	k_sleep(K_MSEC(200));
	return atomic_get(&pairing_ok) && atomic_get(&secure);
}

/* A bonded phone reconnecting: encryption from the stored keys, no passkey. */
static void encrypt(void)
{
	int err = 0;

	/* The radio may already have asked for encryption on reconnecting. */
	if (bt_conn_get_security(conn) < BT_SECURITY_L4) {
		err = bt_conn_set_security(conn, BT_SECURITY_L4);
	}
	TEST_ASSERT(err == 0 || err == -EBUSY || err == -EALREADY, "set security (%d)", err);
	for (int t = 0; t < 5000 && bt_conn_get_security(conn) < BT_SECURITY_L4; t += 10) {
		k_sleep(K_MSEC(10));
	}
	TEST_ASSERT(bt_conn_get_security(conn) >= BT_SECURITY_L4,
		    "bonded link never reached LESC level 4");
}

static void key(enum hal_input_event_type type, uint32_t keys, uint8_t want_screen);

/* A bonded phone in the connect window (opened now, or still open). */
static void reconnect_bonded(void)
{
	key(HAL_INPUT_LONG_PRESS, HAL_INPUT_KEY_BAND, BLE_SCREEN_CONNECT);
	TEST_ASSERT(scan_for(3000), "connect window not advertising");
	TEST_ASSERT(connect_radio(), "bonded phone refused in the connect window");
	encrypt();
}

/*
 * A phone whose bond was replaced, in the connect window. Our controller
 * reports a connection as soon as it sends the request; the radio's accept
 * list ignores it, so the link fails to establish.
 */
static void refused_by_accept_list(void)
{
	key(HAL_INPUT_LONG_PRESS, HAL_INPUT_KEY_BAND, BLE_SCREEN_CONNECT);
	TEST_ASSERT(scan_for(3000), "connect window not advertising");
	if (connect_radio()) {
		TEST_ASSERT(wait_flag(&disconnected_flag, 5000), "a replaced bond reconnected");
	}
	TEST_ASSERT(!(radio_ask(LINK_QUERY, 0, 0).a & LINK_CONNECTED), "the radio accepted a replaced bond");
}

static void key(enum hal_input_event_type type, uint32_t keys, uint8_t want_screen)
{
	struct link_msg s = radio_ask(LINK_KEY, (uint8_t)type, (uint16_t)keys);

	TEST_ASSERT(s.b == want_screen, "after key: screen %u, wanted %u", s.b, want_screen);
}

#define COMBO HAL_INPUT_KEY_BAND | HAL_INPUT_KEY_STBY

static void start(const char *name)
{
	static const uint8_t base[16] = WX_GATT_UUID_BYTES(WX_GATT_SERVICE_OFFSET);

	for (int i = 0; i < 16; i++) {
		svc_uuid_le[i] = base[15 - i];
	}
	active = true;
	link_init();
	TEST_ASSERT(bt_enable(NULL) == 0, "bt_enable");
	TEST_ASSERT(bt_conn_auth_cb_register(&auth_cb) == 0, "auth callbacks");
	TEST_ASSERT(bt_conn_auth_info_cb_register(&auth_info_cb) == 0, "auth info callbacks");
	TEST_START("%s", name);
}

static void go(unsigned int dev)
{
	struct link_msg m = {.cmd = LINK_GO};

	link_send(dev, &m);
}

static void wait_go(unsigned int dev)
{
	struct link_msg m = link_wait(dev);

	TEST_ASSERT(m.cmd == LINK_GO, "expected GO from %u", dev);
}

/* ---- Phone 1: the script ---- */

static void unbonded_refused_everywhere(void)
{
	static const uint8_t junk[2] = {CODEC_SCHEMA, 1};

	for (int chr = 0; chr <= SMP_CHR; chr++) {
		int err;

		if (chr == SMP_CHR || (chr != WX_GATT_CHR_TIME && chr != WX_GATT_CHR_CONTROL)) {
			err = rd(chr);
			TEST_ASSERT(err == BT_ATT_ERR_AUTHENTICATION || err == BT_ATT_ERR_ENCRYPTION_KEY_SIZE ||
					    err == BT_ATT_ERR_INSUFFICIENT_ENCRYPTION ||
					    err == BT_ATT_ERR_READ_NOT_PERMITTED,
				    "unbonded read of %d returned %d", chr, err);
		}
		err = wr(chr, junk, sizeof(junk));
		TEST_ASSERT(err == BT_ATT_ERR_AUTHENTICATION || err == BT_ATT_ERR_INSUFFICIENT_ENCRYPTION ||
				    err == BT_ATT_ERR_WRITE_NOT_PERMITTED,
			    "unbonded write of %d returned %d", chr, err);
	}
}

static void characteristics(void)
{
	static uint8_t v[WX_GATT_MAX_ALERT_LOG];
	struct codec_counties in = {.count = 16}, out;
	struct codec_time t = {.utc = 1791172800}; /* 2026-10-05 04:00 UTC */
	struct codec_status st;
	struct codec_control_indication ind;
	struct codec_log_entry log;
	struct link_msg s;
	atomic_val_t before;
	size_t n;

	/* 16 counties: a 98-byte value, written and read back intact. */
	for (uint8_t i = 0; i < 16; i++) {
		in.codes[i] = (struct same_location){.subdivision = i % 10, .state = 48,
						     .county = (uint16_t)(1 + 2 * i)};
	}
	n = codec_encode_counties(&in, v);
	TEST_ASSERT(wr(WX_GATT_CHR_COUNTIES, v, n) == 0, "16-county write");
	TEST_ASSERT(rd(WX_GATT_CHR_COUNTIES) == 0, "county read");
	TEST_ASSERT(codec_decode_counties(read_buf, read_len, &out) == CODEC_OK, "decode");
	TEST_ASSERT(out.count == 16 && memcmp(out.codes, in.codes, sizeof(in.codes)) == 0,
		    "counties came back changed");

	/* Invalid writes: ATT errors, nothing changes. */
	v[0] = 2;
	TEST_ASSERT(wr(WX_GATT_CHR_COUNTIES, v, n) == WX_GATT_ERR_SCHEMA, "schema 2 accepted");
	v[0] = CODEC_SCHEMA;
	TEST_ASSERT(wr(WX_GATT_CHR_COUNTIES, v, n - 3) == WX_GATT_ERR_LENGTH, "truncated accepted");
	in.count = 1;
	n = codec_encode_counties(&in, v);
	v[2] = 'X';
	TEST_ASSERT(wr(WX_GATT_CHR_COUNTIES, v, n) == WX_GATT_ERR_VALUE, "bad digit accepted");
	TEST_ASSERT(rd(WX_GATT_CHR_COUNTIES) == 0 && read_buf[1] == 16, "settings changed");

	/* Time sets the clock. */
	strcpy(t.tz, "EST5EDT,M3.2.0,M11.1.0");
	TEST_ASSERT(wr(WX_GATT_CHR_TIME, v, codec_encode_time(&t, v)) == 0, "time write");
	s = radio_ask(LINK_QUERY, 0, 0);
	TEST_ASSERT(s.c >= (uint32_t)t.utc && s.c <= (uint32_t)t.utc + 2U, "clock %u", s.c);

	subscribe(WX_GATT_CHR_STATUS, BT_GATT_CCC_NOTIFY);
	subscribe(WX_GATT_CHR_ALERT_LOG, BT_GATT_CCC_NOTIFY);
	subscribe(WX_GATT_CHR_CONTROL, BT_GATT_CCC_INDICATE);

	/* Status notifies on battery and signal changes. */
	before = atomic_get(&notes[WX_GATT_CHR_STATUS]);
	(void)radio_ask(LINK_BATTERY, 57, 0);
	do {
		TEST_ASSERT(wait_note(WX_GATT_CHR_STATUS, before, 90000), "no battery notification");
		before = atomic_get(&notes[WX_GATT_CHR_STATUS]);
		TEST_ASSERT(codec_decode_status(last_value[WX_GATT_CHR_STATUS],
						last_len[WX_GATT_CHR_STATUS], &st) == CODEC_OK, "status");
	} while (st.battery_percent != 57);
	(void)radio_ask(LINK_SIGNAL, 50, 4);
	do {
		TEST_ASSERT(wait_note(WX_GATT_CHR_STATUS, before, 10000), "no signal notification");
		before = atomic_get(&notes[WX_GATT_CHR_STATUS]);
		(void)codec_decode_status(last_value[WX_GATT_CHR_STATUS],
					  last_len[WX_GATT_CHR_STATUS], &st);
	} while (st.snr_db != 4);
	TEST_ASSERT(st.rssi_dbuv == 50, "rssi %d", st.rssi_dbuv);

	/* Test alert: indicated done; the alert log notifies the new entry. */
	before = atomic_get(&notes[WX_GATT_CHR_ALERT_LOG]);
	TEST_ASSERT(wr(WX_GATT_CHR_CONTROL, v, codec_encode_command(CODEC_CMD_TEST_ALERT, v)) == 0,
		    "test alert");
	TEST_ASSERT(wait_note(WX_GATT_CHR_CONTROL, 0, 2000), "no control indication");
	(void)codec_decode_control_indication(last_value[WX_GATT_CHR_CONTROL],
					      last_len[WX_GATT_CHR_CONTROL], &ind);
	TEST_ASSERT(ind.command == CODEC_CMD_TEST_ALERT && ind.result == CODEC_CTRL_DONE,
		    "test alert result %u", ind.result);
	TEST_ASSERT(wait_note(WX_GATT_CHR_ALERT_LOG, before, 2000), "no alert log notification");
	TEST_ASSERT(codec_decode_log_entry(last_value[WX_GATT_CHR_ALERT_LOG],
					   last_len[WX_GATT_CHR_ALERT_LOG], &log) == CODEC_OK, "log");
	TEST_ASSERT(log.outcome == 4 /* ALERT_LOG_TEST */, "logged as %u", log.outcome);

	/* Let the test alert end so the radio is back in Standby. */
	k_sleep(K_SECONDS(125));
}

static void test_phone1_main(void)
{
	struct link_msg s;

	start("phone 1");

	/* Nothing advertises until a window opens; the crystal is off. */
	TEST_ASSERT(!scan_for(3000), "advertising by default");
	s = radio_ask(LINK_QUERY, 0, 0);
	TEST_ASSERT(!(s.a & LINK_HFXO), "crystal on with Bluetooth idle");

	/* Pairing window: an unpaired phone is refused everywhere, SMP included. */
	key(HAL_INPUT_COMBO, COMBO, BLE_SCREEN_PAIRING);
	TEST_ASSERT(radio_ask(LINK_QUERY, 0, 0).a & LINK_HFXO, "crystal off in a window");
	TEST_ASSERT(scan_for(3000), "pairing window not advertising");
	TEST_ASSERT(connect_radio(), "could not connect in the pairing window");
	discover(true);
	unbonded_refused_everywhere();

	/* A wrong passkey fails and leaves the window open; the right one bonds. */
	TEST_ASSERT(!pair(false), "paired with a wrong passkey");
	TEST_ASSERT(radio_ask(LINK_QUERY, 0, 0).b == BLE_SCREEN_PAIRING, "window closed");
	TEST_ASSERT(pair(true), "pairing with the right passkey failed");
	s = radio_ask(LINK_QUERY, 0, 0);
	TEST_ASSERT(s.b == BLE_SCREEN_NONE && (s.a & LINK_SECURE) && s.e == 1, "after pairing");

	characteristics();
	disconnect();
	s = radio_ask(LINK_QUERY, 0, 0);
	TEST_ASSERT(!(s.a & (LINK_HFXO | LINK_CONNECTED)), "crystal on after disconnect");

	/* Bonded: can't reconnect outside the connect window, can inside it. */
	TEST_ASSERT(!scan_for(3000), "advertising outside a window");
	key(HAL_INPUT_LONG_PRESS, HAL_INPUT_KEY_BAND, BLE_SCREEN_CONNECT);
	TEST_ASSERT(scan_for(3000), "connect window not advertising");
	TEST_ASSERT(connect_radio(), "bonded phone refused in the connect window");
	encrypt();
	discover(true);
	TEST_ASSERT(rd(WX_GATT_CHR_MODE) == 0, "read after reconnecting");

	/* Pairing outside the pairing window fails. */
	atomic_set(&pairing_done, 0);
	atomic_set(&passkey_wanted, 0);
	(void)bt_conn_set_security(conn, BT_SECURITY_L4 | BT_SECURITY_FORCE_PAIR);
	if (wait_flag(&passkey_wanted, 2000)) {
		TEST_FAIL("the radio offered pairing outside the window");
	}
	TEST_ASSERT(wait_flag(&pairing_done, 5000) && !atomic_get(&pairing_ok),
		    "pairing outside the window did not fail");
	if (conn != NULL) {
		disconnect();
	}
	TEST_ASSERT(radio_ask(LINK_QUERY, 0, 0).e == 1, "the radio lost its bond");

	/*
	 * The third bond replaces the least recently used one. Phone 1 paired
	 * first but reconnects after phone 2 pairs, so phone 3 replaces phone 2.
	 */
	go(DEV_PHONE2);
	wait_go(DEV_PHONE2);
	reconnect_bonded();
	disconnect();
	go(DEV_PHONE3);
	wait_go(DEV_PHONE3);
	reconnect_bonded();
	disconnect();
	TEST_ASSERT(radio_ask(LINK_QUERY, 0, 0).e == 2, "two bonds after the third pairing");

	/* Phone 2 is refused; phone 3 reconnects and runs the factory reset. */
	go(DEV_PHONE2);
	wait_go(DEV_PHONE2);
	go(DEV_PHONE3);
	wait_go(DEV_PHONE3);
	s = radio_ask(LINK_QUERY, 0, 0);
	TEST_ASSERT(s.e == 0, "bonds left after factory reset: %u", s.e);
	TEST_ASSERT(!(s.a & LINK_HFXO), "crystal on at the end");

	{
		struct link_msg done = {.cmd = LINK_DONE};

		link_send(DEV_RADIO, &done);
	}
	TEST_PASS("phone 1");
}

/* ---- Phone 2: the second bond ---- */

static void test_phone2_main(void)
{
	start("phone 2");
	wait_go(DEV_PHONE1);
	key(HAL_INPUT_COMBO, COMBO, BLE_SCREEN_PAIRING);
	TEST_ASSERT(scan_for(3000), "pairing window not advertising");
	TEST_ASSERT(connect_radio(), "connect");
	TEST_ASSERT(pair(true), "second bond");
	TEST_ASSERT(radio_ask(LINK_QUERY, 0, 0).e == 2, "two bonds");

	/* At the default 23-byte MTU a county list takes a long write and a long read. */
	{
		static uint8_t v[WX_GATT_MAX_COUNTIES];
		struct codec_counties in = {.count = 16}, out;
		size_t n;

		discover(false);
		for (uint8_t i = 0; i < 16; i++) {
			in.codes[i] = (struct same_location){.subdivision = 9 - i % 10, .state = 6,
							     .county = (uint16_t)(999 - i)};
		}
		n = codec_encode_counties(&in, v);
		TEST_ASSERT(wr(WX_GATT_CHR_COUNTIES, v, n) == 0, "long write");
		TEST_ASSERT(rd(WX_GATT_CHR_COUNTIES) == 0 && read_len == n, "long read (%u)", read_len);
		TEST_ASSERT(codec_decode_counties(read_buf, read_len, &out) == CODEC_OK &&
				    memcmp(out.codes, in.codes, sizeof(in.codes)) == 0,
			    "counties changed over a long write");
	}
	disconnect();
	go(DEV_PHONE1);

	/* Phone 1 reconnected since, so phone 3's bond replaced this one. */
	wait_go(DEV_PHONE1);
	refused_by_accept_list();
	go(DEV_PHONE1);
	TEST_PASS("phone 2");
}

/* ---- Phone 3: the third bond, then the factory reset ---- */

/* Write a command (or press a key on the radio) and check the indication it causes. */
static void control(enum codec_command cmd, enum hal_input_event_type type, uint32_t keys,
		    enum codec_control_result want)
{
	static uint8_t v[WX_GATT_MAX_CONTROL];
	struct codec_control_indication ind;
	atomic_val_t before = atomic_get(&notes[WX_GATT_CHR_CONTROL]);

	if (cmd != 0) {
		TEST_ASSERT(wr(WX_GATT_CHR_CONTROL, v, codec_encode_command(cmd, v)) == 0, "control");
	} else {
		(void)radio_ask(LINK_KEY, (uint8_t)type, (uint16_t)keys);
	}
	TEST_ASSERT(wait_note(WX_GATT_CHR_CONTROL, before, 5000), "no indication");
	(void)codec_decode_control_indication(last_value[WX_GATT_CHR_CONTROL],
					      last_len[WX_GATT_CHR_CONTROL], &ind);
	TEST_ASSERT(ind.result == want, "control result %u, wanted %u", ind.result, want);
}

static void test_phone3_main(void)
{
	start("phone 3");
	wait_go(DEV_PHONE1);

	/* Two bonds already: the radio asks first. Another key cancels. */
	key(HAL_INPUT_COMBO, COMBO, BLE_SCREEN_CONFIRM_BOND);
	TEST_ASSERT(!(radio_ask(LINK_QUERY, 0, 0).a & LINK_WINDOW), "window before confirming");
	key(HAL_INPUT_PRESS, HAL_INPUT_KEY_VOL_UP, BLE_SCREEN_NONE);
	TEST_ASSERT(!scan_for(2000), "advertising after cancelling");
	key(HAL_INPUT_COMBO, COMBO, BLE_SCREEN_CONFIRM_BOND);
	key(HAL_INPUT_LONG_PRESS, HAL_INPUT_KEY_STBY, BLE_SCREEN_PAIRING);
	TEST_ASSERT(scan_for(3000), "pairing window not advertising");
	TEST_ASSERT(connect_radio(), "connect");
	TEST_ASSERT(pair(true), "third bond");
	TEST_ASSERT(radio_ask(LINK_QUERY, 0, 0).e == 2, "still two bonds");
	disconnect();
	go(DEV_PHONE1);

	/* Factory reset: waits for the radio; another key cancels; long STBY confirms. */
	wait_go(DEV_PHONE1);
	reconnect_bonded();
	discover(true);
	subscribe(WX_GATT_CHR_CONTROL, BT_GATT_CCC_INDICATE);
	control(CODEC_CMD_FACTORY_RESET, 0, 0, CODEC_CTRL_AWAITING_CONFIRMATION);
	TEST_ASSERT(rd(WX_GATT_CHR_COUNTIES) == 0 && read_buf[1] == 16, "reset without confirmation");
	control(0, HAL_INPUT_PRESS, HAL_INPUT_KEY_VOL_DOWN, CODEC_CTRL_CANCELLED);
	TEST_ASSERT(rd(WX_GATT_CHR_COUNTIES) == 0 && read_buf[1] == 16, "reset after cancelling");
	control(CODEC_CMD_FACTORY_RESET, 0, 0, CODEC_CTRL_AWAITING_CONFIRMATION);
	atomic_set(&disconnected_flag, 0);
	control(0, HAL_INPUT_LONG_PRESS, HAL_INPUT_KEY_STBY, CODEC_CTRL_DONE);
	TEST_ASSERT(wait_flag(&disconnected_flag, 5000), "the radio kept the link after reset");
	go(DEV_PHONE1);
	TEST_PASS("phone 3");
}

static const struct bst_test_instance test_phone[] = {
	{
		.test_id = "phone1",
		.test_descr = "Phone 1: policy, pairing, characteristics, reconnection",
		.test_main_f = test_phone1_main,
	},
	{
		.test_id = "phone2",
		.test_descr = "Phone 2: the second bond, replaced as the least recently used",
		.test_main_f = test_phone2_main,
	},
	{
		.test_id = "phone3",
		.test_descr = "Phone 3: the third bond and the factory reset",
		.test_main_f = test_phone3_main,
	},
	BSTEST_END_MARKER,
};

struct bst_test_list *test_phone_install(struct bst_test_list *tests)
{
	return bst_add_tests(tests, test_phone);
}
