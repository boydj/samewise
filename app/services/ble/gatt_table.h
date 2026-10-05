/*
 * Bluetooth settings service: the characteristics table.
 *
 * The single source of truth for the GATT layout. The firmware service
 * (services/ble) and docs/gatt.json (for the macOS mock peripheral and the
 * iPhone app) are both generated from the rows below; tools/gatt checks in
 * CI that docs/gatt.json matches. Plain C, no Zephyr includes.
 *
 * UUIDs: one random 128-bit base, frozen; the service and each
 * characteristic replace bytes 3-4 (big-endian) with their 16-bit offset:
 *   1af2XXXX-92fa-49ea-aaea-76bad817c54d
 *
 * Every value starts with a 1-byte schema version; fixed fields are
 * little-endian and lists are length-prefixed (services/ble/codec.h).
 * Values never exceed the 512-byte ATT limit: the event table and the alert
 * log are read one entry at a time after a write selects the index.
 */

#ifndef SERVICES_BLE_GATT_TABLE_H_
#define SERVICES_BLE_GATT_TABLE_H_

#include <stdint.h>

#define WX_GATT_UUID_FORMAT "1af2%04x-92fa-49ea-aaea-76bad817c54d"

/* Base UUID in big-endian byte order, bytes 2-3 replaced by the offset. */
#define WX_GATT_UUID_BYTES(offset)                                                                 \
	{0x1a, 0xf2, (uint8_t)((offset) >> 8), (uint8_t)(offset), 0x92, 0xfa, 0x49, 0xea,          \
	 0xaa, 0xea, 0x76, 0xba, 0xd8, 0x17, 0xc5, 0x4d}

#define WX_GATT_SERVICE_OFFSET 0x0000

/* Properties. */
#define WX_GATT_READ     0x01U
#define WX_GATT_WRITE    0x02U
#define WX_GATT_NOTIFY   0x04U
#define WX_GATT_INDICATE 0x08U

/*
 * Every custom characteristic needs an encrypted, authenticated link from a
 * bonded phone (LE Secure Connections): reads map to
 * BT_GATT_PERM_READ_LESC and writes to BT_GATT_PERM_WRITE_LESC.
 */
#define WX_GATT_PERM_LESC 0x01U

/*
 * X(id, name, offset, properties, max_len, schema, description)
 *   id          C identifier suffix
 *   name        stable JSON name
 *   offset      16-bit UUID offset
 *   properties  WX_GATT_* bits
 *   max_len     largest value in bytes (read, write or notification)
 *   schema      schema version of the value
 *   description value layout, for docs/gatt.json
 */
#define WX_GATT_CHARACTERISTICS(X)                                                                 \
	X(COUNTIES, "counties", 0x0001, WX_GATT_READ | WX_GATT_WRITE, 98, 1,                     \
	  "u8 schema, u8 count (0-16), count x 6 ASCII digits PSSCCC")                             \
	X(TRAVEL_COUNTIES, "travel_counties", 0x0002, WX_GATT_READ | WX_GATT_WRITE, 98, 1,       \
	  "same layout as counties; used only in travel mode")                                     \
	X(MODE, "mode", 0x0003, WX_GATT_READ | WX_GATT_WRITE, 3, 1,                              \
	  "u8 schema, u8 mode (0 home, 1 travel), u8 channel (0 auto-scan, 1-7)")                  \
	X(EVENT_FILTER, "event_filter", 0x0004, WX_GATT_READ | WX_GATT_WRITE, 18, 1,             \
	  "u8 schema, u8 preset (0 warnings, 1 warnings+watches, 2 all, 3 custom), "             \
	  "16-byte bitmap over event table indices")                                               \
	X(EVENT_TABLE, "event_table", 0x0005, WX_GATT_READ | WX_GATT_WRITE, 41, 1,               \
	  "indexed. write u8 schema, u8 op: 0 select (u8 index), 1 begin (u16 version, u8 "       \
	  "count), 2 entry (u8 index, 3 ASCII code, u8 class, u8 name_len, name), 3 commit, 4 "    \
	  "abort. read: u8 schema, u16 version, u8 count, u8 index, 3 ASCII code, u8 class, u8 "  \
	  "name_len, name")                                                                         \
	X(TIME, "time", 0x0006, WX_GATT_WRITE, 58, 1,                                             \
	  "u8 schema, i64 UTC seconds, u8 tz_len (1-48), POSIX TZ string")                         \
	X(PRESETS, "presets", 0x0007, WX_GATT_READ | WX_GATT_WRITE, 42, 1,                       \
	  "u8 schema, u8 count (0-8), count x (u8 band 0 FM 1 AM 2 WB, u32 kHz)")                  \
	X(STATUS, "status", 0x0008, WX_GATT_READ | WX_GATT_NOTIFY, 20, 1,                        \
	  "u8 schema, u8 battery %, u16 hours left, i8 SNR dB, i8 RSSI dBuV, u8 channel, i64 "    \
	  "last weekly test UTC (-1 none), u32 health flags, u8 lock")                              \
	X(ALERT_LOG, "alert_log", 0x0009, WX_GATT_READ | WX_GATT_WRITE | WX_GATT_NOTIFY, 266, 1, \
	  "indexed. write u8 schema, u8 index (0 newest). read/notify: u8 schema, u8 count, u8 "  \
	  "index, i64 received UTC (-1 unset), u8 outcome, u8 flags, u8 len, raw header")          \
	X(CONTROL, "control", 0x000A, WX_GATT_WRITE | WX_GATT_INDICATE, 3, 1,                    \
	  "write u8 schema, u8 command (1 test alert, 2 clear log, 3 factory reset). indicate: "   \
	  "u8 schema, u8 command, u8 result (0 done, 1 awaiting confirmation, 2 cancelled, 3 "    \
	  "rejected)")

enum wx_gatt_chr {
#define WX_GATT_ENUM(id, name, offset, props, max_len, schema, desc) WX_GATT_CHR_##id,
	WX_GATT_CHARACTERISTICS(WX_GATT_ENUM)
#undef WX_GATT_ENUM
	WX_GATT_CHR_COUNT,
};

/* Largest value of each characteristic, as constants for static buffers. */
enum {
#define WX_GATT_MAX(id, name, offset, props, max_len, schema, desc) WX_GATT_MAX_##id = max_len,
	WX_GATT_CHARACTERISTICS(WX_GATT_MAX)
#undef WX_GATT_MAX
};

#endif /* SERVICES_BLE_GATT_TABLE_H_ */
