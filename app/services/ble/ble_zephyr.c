/*
 * Bluetooth settings service on Zephyr's Bluetooth host.
 *
 * Stack callbacks run in the host's threads and call straight into the
 * service; on native targets only one thread runs at a time. The board
 * build (milestone 5) serialises them with the input and timer work.
 */

#include <errno.h>
#include <string.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/util.h>

#include "services/ble/ble_zephyr.h"

#define N WX_GATT_CHR_COUNT

/* A long write at the minimum MTU carries 18 bytes per prepared write. */
BUILD_ASSERT(CONFIG_BT_ATT_PREPARE_COUNT * 18 >= WX_GATT_MAX_COUNTIES,
	     "CONFIG_BT_ATT_PREPARE_COUNT too small for a 16-county write at the minimum MTU");

static struct ble *svc_b;
static struct bt_conn *conn;
static bool pairable;

/* ---- GATT database, built from gatt_table.h ---- */

static const uint16_t offsets[N] = {
#define WX_Z_OFFSET(id, name, offset, p, max_len, schema, desc) [WX_GATT_CHR_##id] = (offset),
	WX_GATT_CHARACTERISTICS(WX_Z_OFFSET)
#undef WX_Z_OFFSET
};

static const uint8_t props[N] = {
#define WX_Z_PROPS(id, name, offset, p, max_len, schema, desc) [WX_GATT_CHR_##id] = (p),
	WX_GATT_CHARACTERISTICS(WX_Z_PROPS)
#undef WX_Z_PROPS
};

/*
 * BT_UUID_GATT_* are compound literals: built inside a function they would
 * not outlive it, so the attribute table points at these instead.
 */
static const struct bt_uuid_16 uuid_primary = BT_UUID_INIT_16(BT_UUID_GATT_PRIMARY_VAL);
static const struct bt_uuid_16 uuid_chrc = BT_UUID_INIT_16(BT_UUID_GATT_CHRC_VAL);
static const struct bt_uuid_16 uuid_ccc = BT_UUID_INIT_16(BT_UUID_GATT_CCC_VAL);

static struct bt_uuid_128 svc_uuid;
static struct bt_uuid_128 chr_uuid[N];
static struct bt_gatt_chrc chrc[N];
static struct bt_gatt_ccc_managed_user_data ccc[N];
static struct bt_gatt_attr attrs[1 + 3 * N];
static const struct bt_gatt_attr *value_attr[N];
static struct bt_gatt_service svc;
/*
 * Control results are indications, one outstanding at a time on the link;
 * the stack queues the rest, each with its own parameters. A confirmation
 * on the radio can follow "awaiting confirmation" before the phone has
 * acknowledged it, so keep a few.
 */
#define IND_SLOTS 4
static struct {
	struct bt_gatt_indicate_params params;
	uint8_t data[WX_GATT_MAX_CONTROL];
	bool used;
} ind[IND_SLOTS];
static unsigned int ind_in_flight;
static bool unpair_pending; /* after the factory reset's indication is delivered */

/* gatt_table.h gives the UUID big-endian; Bluetooth stores it little-endian. */
static void set_uuid(struct bt_uuid_128 *u, uint16_t offset)
{
	static const uint8_t base[16] = WX_GATT_UUID_BYTES(0);

	u->uuid.type = BT_UUID_TYPE_128;
	for (int i = 0; i < 16; i++) {
		u->val[i] = base[15 - i];
	}
	u->val[13] = (uint8_t)(offset >> 8);
	u->val[12] = (uint8_t)offset;
}

static ssize_t on_read(struct bt_conn *c, const struct bt_gatt_attr *attr, void *buf,
		       uint16_t len, uint16_t offset)
{
	enum wx_gatt_chr chr = (enum wx_gatt_chr)(uintptr_t)attr->user_data;
	static uint8_t value[WX_GATT_MAX_ALERT_LOG];
	size_t n = 0;
	int err;

	ARG_UNUSED(c);
	err = ble_read(svc_b, chr, value, &n);
	if (err != 0) {
		return BT_GATT_ERR(err);
	}
	return bt_gatt_attr_read(c, attr, buf, len, offset, value, (uint16_t)n);
}

/* Zephyr reassembles a long write and calls once with EXECUTE at offset 0. */
static ssize_t on_write(struct bt_conn *c, const struct bt_gatt_attr *attr, const void *buf,
			uint16_t len, uint16_t offset, uint8_t flags)
{
	enum wx_gatt_chr chr = (enum wx_gatt_chr)(uintptr_t)attr->user_data;
	int err;

	ARG_UNUSED(c);
	if (flags & BT_GATT_WRITE_FLAG_PREPARE) {
		return 0;
	}
	if (offset != 0U) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
	}
	err = ble_write(svc_b, chr, buf, len);
	return err != 0 ? BT_GATT_ERR(err) : (ssize_t)len;
}

static void build_service(void)
{
	size_t k = 0;

	set_uuid(&svc_uuid, WX_GATT_SERVICE_OFFSET);
	attrs[k++] = (struct bt_gatt_attr)BT_GATT_ATTRIBUTE(
		&uuid_primary.uuid, BT_GATT_PERM_READ, bt_gatt_attr_read_service, NULL, &svc_uuid);
	for (int i = 0; i < N; i++) {
		uint8_t p = 0;
		uint16_t perm = 0;

		set_uuid(&chr_uuid[i], offsets[i]);
		if (props[i] & WX_GATT_READ) {
			p |= BT_GATT_CHRC_READ;
			perm |= BT_GATT_PERM_READ_LESC;
		}
		if (props[i] & WX_GATT_WRITE) {
			p |= BT_GATT_CHRC_WRITE;
			perm |= BT_GATT_PERM_WRITE_LESC | BT_GATT_PERM_PREPARE_WRITE;
		}
		if (props[i] & WX_GATT_NOTIFY) {
			p |= BT_GATT_CHRC_NOTIFY;
		}
		if (props[i] & WX_GATT_INDICATE) {
			p |= BT_GATT_CHRC_INDICATE;
		}
		chrc[i] = (struct bt_gatt_chrc)BT_GATT_CHRC_INIT(&chr_uuid[i].uuid, 0U, p);
		attrs[k++] = (struct bt_gatt_attr)BT_GATT_ATTRIBUTE(
			&uuid_chrc.uuid, BT_GATT_PERM_READ, bt_gatt_attr_read_chrc, NULL, &chrc[i]);
		value_attr[i] = &attrs[k];
		attrs[k++] = (struct bt_gatt_attr)BT_GATT_ATTRIBUTE(
			&chr_uuid[i].uuid, perm, (props[i] & WX_GATT_READ) ? on_read : NULL,
			(props[i] & WX_GATT_WRITE) ? on_write : NULL, (void *)(uintptr_t)i);
		if (props[i] & (WX_GATT_NOTIFY | WX_GATT_INDICATE)) {
			ccc[i] = (struct bt_gatt_ccc_managed_user_data)
				BT_GATT_CCC_MANAGED_USER_DATA_INIT(NULL, NULL, NULL);
			attrs[k++] = (struct bt_gatt_attr)BT_GATT_ATTRIBUTE(
				&uuid_ccc.uuid, BT_GATT_PERM_READ_LESC | BT_GATT_PERM_WRITE_LESC,
				bt_gatt_attr_read_ccc, bt_gatt_attr_write_ccc, &ccc[i]);
		}
	}
	svc.attrs = attrs;
	svc.attr_count = k;
}

/* ---- Port ---- */

static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA(BT_DATA_UUID128_ALL, svc_uuid.val, sizeof(svc_uuid.val)),
};

static const struct bt_data sd[] = {
	BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME, sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};

static void accept_bond(const struct bt_bond_info *info, void *user)
{
	ARG_UNUSED(user);
	(void)bt_le_filter_accept_list_add(&info->addr);
}

static int port_adv_start(void *user, bool bonded_only)
{
	struct bt_le_adv_param param = BT_LE_ADV_PARAM_INIT(
		BT_LE_ADV_OPT_CONN | (bonded_only ? BT_LE_ADV_OPT_FILTER_CONN : 0),
		BT_GAP_ADV_FAST_INT_MIN_2, BT_GAP_ADV_FAST_INT_MAX_2, NULL);

	ARG_UNUSED(user);
	(void)bt_le_adv_stop();
	if (bonded_only) {
		(void)bt_le_filter_accept_list_clear();
		bt_foreach_bond(BT_ID_DEFAULT, accept_bond, NULL);
	}
	return bt_le_adv_start(&param, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
}

static void port_adv_stop(void *user)
{
	ARG_UNUSED(user);
	(void)bt_le_adv_stop();
}

static void port_set_pairable(void *user, bool on)
{
	ARG_UNUSED(user);
	pairable = on;
	bt_set_bondable(on);
}

static void count_bond(const struct bt_bond_info *info, void *user)
{
	ARG_UNUSED(info);
	(*(uint8_t *)user)++;
}

static uint8_t port_bond_count(void *user)
{
	uint8_t count = 0;

	ARG_UNUSED(user);
	bt_foreach_bond(BT_ID_DEFAULT, count_bond, &count);
	return count;
}

static void port_disconnect(void *user)
{
	ARG_UNUSED(user);
	if (conn != NULL) {
		(void)bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
	}
}

static void unpair_all(void)
{
	unpair_pending = false;
	(void)bt_unpair(BT_ID_DEFAULT, BT_ADDR_LE_ANY); /* disconnects too */
}

/* Unpairing drops the link: let the phone receive the result first. */
static void port_unpair_all(void *user)
{
	ARG_UNUSED(user);
	if (ind_in_flight > 0U) {
		unpair_pending = true;
	} else {
		unpair_all();
	}
}

static void indicated(struct bt_conn *c, struct bt_gatt_indicate_params *p, uint8_t err)
{
	ARG_UNUSED(c);
	ARG_UNUSED(p);
	ARG_UNUSED(err);
}

static void indication_done(struct bt_gatt_indicate_params *p)
{
	for (int i = 0; i < IND_SLOTS; i++) {
		if (&ind[i].params == p) {
			ind[i].used = false;
			ind_in_flight--;
		}
	}
	if (unpair_pending && ind_in_flight == 0U) {
		unpair_all();
	}
}

static int port_notify(void *user, enum wx_gatt_chr chr, const uint8_t *data, size_t len)
{
	const struct bt_gatt_attr *attr = value_attr[chr];

	ARG_UNUSED(user);
	if (conn == NULL) {
		return -ENOTCONN;
	}
	if (props[chr] & WX_GATT_INDICATE) {
		int slot = -1;
		int err;

		for (int i = 0; i < IND_SLOTS && slot < 0; i++) {
			slot = ind[i].used ? -1 : i;
		}
		if (!bt_gatt_is_subscribed(conn, attr, BT_GATT_CCC_INDICATE) ||
		    len > sizeof(ind[0].data) || slot < 0) {
			return -EINVAL;
		}
		memcpy(ind[slot].data, data, len);
		memset(&ind[slot].params, 0, sizeof(ind[slot].params));
		ind[slot].params.attr = attr;
		ind[slot].params.data = ind[slot].data;
		ind[slot].params.len = (uint16_t)len;
		ind[slot].params.func = indicated;
		ind[slot].params.destroy = indication_done;
		err = bt_gatt_indicate(conn, &ind[slot].params);
		if (err == 0) {
			ind[slot].used = true;
			ind_in_flight++;
		}
		return err;
	}
	if (!bt_gatt_is_subscribed(conn, attr, BT_GATT_CCC_NOTIFY)) {
		return -EINVAL;
	}
	return bt_gatt_notify(conn, attr, data, (uint16_t)len);
}

const struct ble_port ble_zephyr_port = {
	.adv_start = port_adv_start,
	.adv_stop = port_adv_stop,
	.set_pairable = port_set_pairable,
	.bond_count = port_bond_count,
	.disconnect = port_disconnect,
	.unpair_all = port_unpair_all,
	.notify = port_notify,
};

/* ---- Stack events ---- */

static void connected(struct bt_conn *c, uint8_t err)
{
	if (err != 0U || conn != NULL || svc_b == NULL) {
		return;
	}
	conn = bt_conn_ref(c);
	ble_on_connected(svc_b);
}

static void disconnected(struct bt_conn *c, uint8_t reason)
{
	ARG_UNUSED(reason);
	if (c != conn) {
		return;
	}
	bt_conn_unref(conn);
	conn = NULL;
	ble_on_disconnected(svc_b);
	if (unpair_pending) {
		unpair_all(); /* the phone left before confirming the indication */
	}
}

static void security_changed(struct bt_conn *c, bt_security_t level, enum bt_security_err err)
{
	if (c == conn) {
		ble_on_security(svc_b, err == BT_SECURITY_ERR_SUCCESS && level >= BT_SECURITY_L4);
	}
}

BT_CONN_CB_DEFINE(wx_conn_cb) = {
	.connected = connected,
	.disconnected = disconnected,
	.security_changed = security_changed,
};

static enum bt_security_err pairing_accept(struct bt_conn *c,
					   const struct bt_conn_pairing_feat *const feat)
{
	ARG_UNUSED(c);
	ARG_UNUSED(feat);
	return pairable ? BT_SECURITY_ERR_SUCCESS : BT_SECURITY_ERR_PAIR_NOT_ALLOWED;
}

static void passkey_display(struct bt_conn *c, unsigned int passkey)
{
	if (c == conn) {
		ble_on_passkey(svc_b, passkey);
	}
}

static void auth_cancel(struct bt_conn *c)
{
	ARG_UNUSED(c);
}

static const struct bt_conn_auth_cb auth_cb = {
	.pairing_accept = pairing_accept,
	.passkey_display = passkey_display,
	.cancel = auth_cancel,
};

static void pairing_complete(struct bt_conn *c, bool bonded)
{
	if (c == conn) {
		ble_on_pairing_done(svc_b, bonded);
	}
}

static void pairing_failed(struct bt_conn *c, enum bt_security_err reason)
{
	ARG_UNUSED(reason);
	if (c == conn) {
		ble_on_pairing_done(svc_b, false);
	}
}

static struct bt_conn_auth_info_cb auth_info_cb = {
	.pairing_complete = pairing_complete,
	.pairing_failed = pairing_failed,
};

int ble_zephyr_init(struct ble *b)
{
	int err;

	svc_b = b;
	build_service();
	err = bt_enable(NULL);
	if (err != 0) {
		return err;
	}
	if (IS_ENABLED(CONFIG_BT_SETTINGS)) {
		(void)settings_load();
	}
	err = bt_conn_auth_cb_register(&auth_cb);
	if (err == 0) {
		err = bt_conn_auth_info_cb_register(&auth_info_cb);
	}
	if (err == 0) {
		err = bt_gatt_service_register(&svc);
	}
	bt_set_bondable(false);
	return err;
}
