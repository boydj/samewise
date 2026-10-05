/*
 * Bluetooth value codec: every characteristic in services/ble/gatt_table.h.
 *
 * Plain C99, no Zephyr includes. Each value starts with a 1-byte schema
 * version (CODEC_SCHEMA); fixed fields are little-endian; lists are
 * length-prefixed. Decoders validate everything (exact length, schema,
 * counts, ranges, characters) and fill a caller's staging struct only on
 * success, so a rejected write never touches settings. Encoders return the
 * number of bytes written; buffers must hold the characteristic's max_len.
 */

#ifndef SERVICES_BLE_CODEC_H_
#define SERVICES_BLE_CODEC_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "services/ble/gatt_table.h"
#include "services/match/event_table.h"
#include "services/match/filter.h"
#include "services/same/same_header.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CODEC_SCHEMA       1U
#define CODEC_MAX_COUNTIES 16U
#define CODEC_MAX_PRESETS  8U
#define CODEC_TZ_MAX       48U
#define CODEC_CHANNEL_MAX  7U

enum codec_result {
	CODEC_OK = 0,
	CODEC_ERR_LENGTH = -1, /* truncated, oversized or trailing bytes */
	CODEC_ERR_SCHEMA = -2, /* unknown schema version */
	CODEC_ERR_VALUE = -3,  /* a field out of range or malformed */
};

struct codec_counties {
	uint8_t count;
	struct same_location codes[CODEC_MAX_COUNTIES];
};

struct codec_mode {
	uint8_t mode;    /* 0 home, 1 travel */
	uint8_t channel; /* 0 auto-scan, 1-7 */
};

struct codec_filter {
	uint8_t preset; /* enum filter_preset */
	uint8_t bitmap[FILTER_CUSTOM_BYTES];
};

struct codec_time {
	int64_t utc;
	char tz[CODEC_TZ_MAX + 1]; /* NUL-terminated POSIX TZ string */
};

enum codec_band {
	CODEC_BAND_FM,
	CODEC_BAND_AM,
	CODEC_BAND_WB,
};

struct codec_preset {
	uint8_t band; /* enum codec_band */
	uint32_t khz;
};

struct codec_presets {
	uint8_t count;
	struct codec_preset p[CODEC_MAX_PRESETS];
};

struct codec_status {
	uint8_t battery_percent;
	uint16_t hours_left;
	int8_t snr_db;
	int8_t rssi_dbuv;
	uint8_t channel;
	int64_t last_rwt_utc; /* -1 none */
	uint32_t health_flags;
	uint8_t locked;
};

enum codec_et_op {
	CODEC_ET_SELECT,
	CODEC_ET_BEGIN,
	CODEC_ET_ENTRY,
	CODEC_ET_COMMIT,
	CODEC_ET_ABORT,
};

/** A write to the event table characteristic. */
struct codec_et_write {
	uint8_t op;      /* enum codec_et_op */
	uint8_t index;   /* SELECT, ENTRY */
	uint16_t version; /* BEGIN */
	uint8_t count;   /* BEGIN */
	struct event_entry entry; /* ENTRY */
};

/** An event table read: one entry with the table's version and size. */
struct codec_et_read {
	uint16_t version;
	uint8_t count;
	uint8_t index;
	struct event_entry entry;
};

/** An alert log read or notification: one entry. */
struct codec_log_entry {
	uint8_t count;
	uint8_t index; /* 0 = newest */
	int64_t received_utc;
	uint8_t outcome;
	uint8_t flags;
	char raw[SAME_HEADER_MAX_LEN + 1];
};

enum codec_command {
	CODEC_CMD_TEST_ALERT = 1,
	CODEC_CMD_CLEAR_LOG = 2,
	CODEC_CMD_FACTORY_RESET = 3,
};

enum codec_control_result {
	CODEC_CTRL_DONE,
	CODEC_CTRL_AWAITING_CONFIRMATION,
	CODEC_CTRL_CANCELLED,
	CODEC_CTRL_REJECTED,
};

struct codec_control_indication {
	uint8_t command;
	uint8_t result;
};

/* Counties and travel counties: u8 schema, u8 count, count x "PSSCCC". */
size_t codec_encode_counties(const struct codec_counties *v, uint8_t *buf);
int codec_decode_counties(const uint8_t *buf, size_t len, struct codec_counties *out);

/* Mode: u8 schema, u8 mode, u8 channel. */
size_t codec_encode_mode(const struct codec_mode *v, uint8_t *buf);
int codec_decode_mode(const uint8_t *buf, size_t len, struct codec_mode *out);

/* Event filter: u8 schema, u8 preset, 16-byte bitmap. */
size_t codec_encode_filter(const struct codec_filter *v, uint8_t *buf);
int codec_decode_filter(const uint8_t *buf, size_t len, struct codec_filter *out);

/* Time: u8 schema, i64 UTC, u8 tz_len, TZ characters (validated by tz.h separately). */
size_t codec_encode_time(const struct codec_time *v, uint8_t *buf);
int codec_decode_time(const uint8_t *buf, size_t len, struct codec_time *out);

/** Whether kHz is a tunable frequency in band (FM 87.5-108 MHz, AM 520-1710 kHz, WB 25 kHz raster). */
bool codec_preset_valid(uint8_t band, uint32_t khz);

/* Presets: u8 schema, u8 count, count x (u8 band, u32 kHz in band range). */
size_t codec_encode_presets(const struct codec_presets *v, uint8_t *buf);
int codec_decode_presets(const uint8_t *buf, size_t len, struct codec_presets *out);

/* Status (read, notify). */
size_t codec_encode_status(const struct codec_status *v, uint8_t *buf);
int codec_decode_status(const uint8_t *buf, size_t len, struct codec_status *out);

/* Event table: writes carry an operation, reads one entry. */
size_t codec_encode_et_write(const struct codec_et_write *v, uint8_t *buf);
int codec_decode_et_write(const uint8_t *buf, size_t len, struct codec_et_write *out);
size_t codec_encode_et_read(const struct codec_et_read *v, uint8_t *buf);
int codec_decode_et_read(const uint8_t *buf, size_t len, struct codec_et_read *out);

/* Alert log: a write selects an index; reads and notifications carry one entry. */
size_t codec_encode_log_select(uint8_t index, uint8_t *buf);
int codec_decode_log_select(const uint8_t *buf, size_t len, uint8_t *index);
size_t codec_encode_log_entry(const struct codec_log_entry *v, uint8_t *buf);
int codec_decode_log_entry(const uint8_t *buf, size_t len, struct codec_log_entry *out);

/* Control: write a command, indicate its result. */
size_t codec_encode_command(enum codec_command cmd, uint8_t *buf);
int codec_decode_command(const uint8_t *buf, size_t len, enum codec_command *out);
size_t codec_encode_control_indication(const struct codec_control_indication *v, uint8_t *buf);
int codec_decode_control_indication(const uint8_t *buf, size_t len,
				    struct codec_control_indication *out);

#ifdef __cplusplus
}
#endif

#endif /* SERVICES_BLE_CODEC_H_ */
