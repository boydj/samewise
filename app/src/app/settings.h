/*
 * Settings model: counties, mode, weather channel, event filter, the event
 * table, station presets and the time zone, kept in RAM and persisted
 * through hal/storage.h.
 *
 * Each part is its own record, starting with a 1-byte schema version
 * (fixed fields little-endian, lists length-prefixed, as for Bluetooth).
 * A missing or unreadable record falls back to its default. Bluetooth
 * writes reach the setters through app/ble_link.c.
 */

#ifndef APP_SETTINGS_H_
#define APP_SETTINGS_H_

#include <stdbool.h>
#include <stdint.h>

#include "services/ble/codec.h"
#include "services/ble/tz.h"
#include "services/match/event_table.h"
#include "services/match/filter.h"
#include "services/same/same_header.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Most home or travel counties (spec: Counties characteristic). */
#define SETTINGS_MAX_COUNTIES 16
/** Weather channels 1-7; 0 means auto-scan. */
#define SETTINGS_CHANNEL_AUTO 0U
#define SETTINGS_CHANNEL_MAX  7U
/** Bytes in the custom filter bitmap, one bit per event table index. */
#define SETTINGS_FILTER_BYTES FILTER_CUSTOM_BYTES
/** Station presets (spec: Presets characteristic). */
#define SETTINGS_MAX_PRESETS CODEC_MAX_PRESETS
/** Longest POSIX TZ string. */
#define SETTINGS_TZ_MAX CODEC_TZ_MAX
#define SETTINGS_TZ_DEFAULT "UTC0"

enum settings_mode {
	SETTINGS_MODE_HOME,
	SETTINGS_MODE_TRAVEL,
};

struct county_list {
	uint8_t count;
	struct same_location codes[SETTINGS_MAX_COUNTIES];
};

struct settings {
	struct county_list home;
	struct county_list travel;
	uint8_t mode;          /* enum settings_mode */
	uint8_t channel;       /* SETTINGS_CHANNEL_AUTO or 1-7 */
	uint8_t filter;        /* enum filter_preset */
	uint8_t custom[SETTINGS_FILTER_BYTES];
	struct event_table events;
	uint8_t preset_count;
	struct codec_preset presets[SETTINGS_MAX_PRESETS]; /* band is enum codec_band */
	char tz[SETTINGS_TZ_MAX + 1];
	struct tz_info tz_info; /* tz, parsed */
};

/**
 * Load every record into s; missing ones take their defaults.
 *
 * @return number of records that were present but unreadable (wrong schema
 *         or size) and were replaced by defaults; 0 when all is well.
 */
int settings_load(struct settings *s);

/**
 * Defaults: no counties, home mode, auto channel, warnings and watches,
 * default table, no presets, UTC.
 */
void settings_defaults(struct settings *s);

/*
 * Setters validate, update s and persist the record. They return 0,
 * -EINVAL (s unchanged), or the storage error (s updated in RAM only).
 */
int settings_set_counties(struct settings *s, enum settings_mode which,
			  const struct same_location *codes, uint8_t count);
int settings_set_mode(struct settings *s, enum settings_mode mode);
int settings_set_channel(struct settings *s, uint8_t channel);
int settings_set_filter(struct settings *s, enum filter_preset preset,
			const uint8_t custom[SETTINGS_FILTER_BYTES]);
int settings_set_event_table(struct settings *s, const struct event_table *table);
int settings_set_presets(struct settings *s, const struct codec_preset *presets, uint8_t count);
/** A POSIX TZ string (services/ble/tz.h); -EINVAL if it doesn't parse. */
int settings_set_tz(struct settings *s, const char *tz);

/** Delete every settings record and return to the defaults. */
void settings_factory_reset(struct settings *s);

/** Local wall-clock seconds for utc in the configured time zone. */
int64_t settings_local_time(const struct settings *s, int64_t utc, bool *dst);

/** The county list in use: travel in travel mode, else home. */
const struct county_list *settings_active_counties(const struct settings *s);

#ifdef __cplusplus
}
#endif

#endif /* APP_SETTINGS_H_ */
