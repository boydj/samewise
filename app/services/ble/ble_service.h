/*
 * Bluetooth settings service (spec: Bluetooth settings service): what every
 * characteristic does and the connection policy, independent of the
 * Bluetooth stack.
 *
 * services/ble/ble_zephyr.c binds it to Zephyr's host (the GATT table with
 * LE Secure Connections permissions, advertising, pairing callbacks)
 * through struct ble_port; the application supplies and applies values
 * through struct ble_app_ops. Both are function tables, so native_sim tests
 * drive the whole policy with a fake port, and nrf52_bsim runs it over the
 * real host.
 *
 * Policy:
 *   - Advertising is off until a window opens.
 *   - Long-press BAND: 2-minute connect window, bonded phones only.
 *   - BAND + STBY: 60-second pairing window, passkey on the screen. With
 *     BLE_MAX_BONDS bonds already, the radio first asks to replace the
 *     least recently used phone; the stack replaces it when the new phone
 *     bonds.
 *   - Confirmations: long-press STBY confirms; any other key, or
 *     BLE_CONFIRM_MS, cancels. Keys are ignored while the lock is on.
 *   - The 32 MHz crystal is requested from the power manager while a window
 *     is open or a phone is connected.
 *   - Characteristics answer only an LE Secure Connections authenticated
 *     link (the stack enforces this too).
 *
 * One connection at a time. Runs in the Bluetooth host's context and the
 * clock's timer context; never called from the alert path.
 */

#ifndef SERVICES_BLE_BLE_SERVICE_H_
#define SERVICES_BLE_BLE_SERVICE_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "hal/clock.h"
#include "hal/input.h"
#include "services/ble/codec.h"
#include "services/ble/gatt_table.h"
#include "services/match/event_table.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BLE_CONNECT_WINDOW_MS (2U * 60U * 1000U)
#define BLE_PAIRING_WINDOW_MS (60U * 1000U)
#define BLE_CONFIRM_MS        (30U * 1000U)
#define BLE_MAX_BONDS         2U
/** Status notifies when SNR or RSSI moves this far from the last notified value. */
#define BLE_SIGNAL_NOTIFY_DB  3

/* Standard ATT errors the service returns itself. */
#define BLE_ATT_ERR_READ_NOT_PERMITTED  0x02
#define BLE_ATT_ERR_WRITE_NOT_PERMITTED 0x03
#define BLE_ATT_ERR_AUTHENTICATION      0x05

enum ble_screen {
	BLE_SCREEN_NONE,
	BLE_SCREEN_CONNECT,       /* connect window open */
	BLE_SCREEN_PAIRING,       /* pairing window open, no passkey yet */
	BLE_SCREEN_PASSKEY,       /* show the 6-digit passkey */
	BLE_SCREEN_CONFIRM_BOND,  /* "replace the least recently used phone?" */
	BLE_SCREEN_CONFIRM_RESET, /* "factory reset?" */
};

enum ble_window {
	BLE_WINDOW_NONE,
	BLE_WINDOW_CONNECT,
	BLE_WINDOW_PAIRING,
};

/** A decoded value, by characteristic. */
union ble_value {
	struct codec_counties counties; /* COUNTIES, TRAVEL_COUNTIES */
	struct codec_mode mode;
	struct codec_filter filter;
	struct codec_time time;         /* tz already checked by tz_parse() */
	struct codec_presets presets;
	struct codec_status status;
	struct codec_et_read et;        /* read: index in, the rest out */
	const struct event_table *table; /* write: the whole staged table */
	struct codec_log_entry log;     /* read: index in, the rest out */
};

struct ble_app_ops {
	/**
	 * Fill v for chr. EVENT_TABLE and ALERT_LOG come with v->et.index or
	 * v->log.index set. Returns 0, or -ENOENT for an index past the end.
	 */
	int (*read)(void *user, enum wx_gatt_chr chr, union ble_value *v);
	/**
	 * Apply a validated value. Returns 0, -EINVAL (rejected, nothing
	 * changed) or another negative errno (applied, but not saved).
	 */
	int (*write)(void *user, enum wx_gatt_chr chr, const union ble_value *v);
	/** Run a command; a factory reset only after confirmation. 0 or -EBUSY. */
	int (*command)(void *user, enum codec_command cmd);
	/** The Bluetooth part of the screen changed. */
	void (*screen)(void *user, enum ble_screen screen, uint32_t passkey);
};

struct ble_port {
	/** Connectable advertising; bonded_only applies the filter accept list. */
	int (*adv_start)(void *user, bool bonded_only);
	void (*adv_stop)(void *user);
	/** Accept pairing requests (LE Secure Connections, display only). */
	void (*set_pairable)(void *user, bool on);
	uint8_t (*bond_count)(void *user);
	void (*disconnect)(void *user);
	void (*unpair_all)(void *user);
	/** Notify chr (indicate, for Control); the stack drops it if unsubscribed. */
	int (*notify)(void *user, enum wx_gatt_chr chr, const uint8_t *data, size_t len);
};

struct ble_stats {
	uint32_t writes;        /* applied */
	uint32_t rejected;      /* writes refused with an ATT error */
	uint32_t refused;       /* reads or writes on an unauthenticated link */
	uint32_t notifications;
	uint32_t storage_errors;
};

struct ble {
	const struct ble_app_ops *app;
	void *app_user;
	const struct ble_port *port;
	void *port_user;

	bool locked;
	bool connected;
	bool secure; /* LE Secure Connections, authenticated */
	bool bonded; /* this link just bonded; its security event may follow */
	uint8_t window;  /* enum ble_window */
	uint8_t confirm; /* BLE_SCREEN_CONFIRM_* or BLE_SCREEN_NONE */
	uint32_t passkey; /* shown while has_passkey */
	bool has_passkey;
	uint8_t screen;  /* enum ble_screen, last reported */
	struct hal_clock_timer window_timer;
	struct hal_clock_timer confirm_timer;

	uint8_t et_index;
	uint8_t log_index;
	bool et_staging;
	uint8_t et_expected;

	bool status_sent;
	struct codec_status last_status;
	struct ble_stats stats;
};

/** Advertising off, no windows. The app and port tables must outlive b. */
void ble_init(struct ble *b, const struct ble_app_ops *app, void *app_user,
	      const struct ble_port *port, void *port_user);

/** A hal/input.h event (the radio forwards keys the alert manager didn't use). */
void ble_on_input(struct ble *b, const struct hal_input_event *e);

/* From the stack. */
void ble_on_connected(struct ble *b);
void ble_on_disconnected(struct ble *b);
void ble_on_security(struct ble *b, bool lesc_authenticated);
void ble_on_passkey(struct ble *b, uint32_t passkey);
void ble_on_pairing_done(struct ble *b, bool bonded);

/**
 * GATT read: encode chr into buf (WX_GATT_MAX_* bytes) and set *len.
 * Returns 0 or an ATT error (BLE_ATT_ERR_*, WX_GATT_ERR_*).
 */
int ble_read(struct ble *b, enum wx_gatt_chr chr, uint8_t *buf, size_t *len);

/** GATT write of a complete value. Returns 0 or an ATT error. */
int ble_write(struct ble *b, enum wx_gatt_chr chr, const uint8_t *buf, size_t len);

/** Re-read Status and notify if anything but the signal changed, or the signal moved 3 dB. */
void ble_poll_status(struct ble *b);

/** A new alert log entry was stored: notify it. */
void ble_log_added(struct ble *b);

#ifdef __cplusplus
}
#endif

#endif /* SERVICES_BLE_BLE_SERVICE_H_ */
