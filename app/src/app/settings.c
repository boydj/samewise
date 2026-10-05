/*
 * Settings model: validation and persistence.
 */

#include <errno.h>
#include <string.h>

#include "app/settings.h"
#include "hal/storage.h"

#define SCHEMA 1U

#define KEY_HOME    "cty/home"
#define KEY_TRAVEL  "cty/travel"
#define KEY_MODE    "mode"
#define KEY_CHANNEL "channel"
#define KEY_FILTER  "filter"
#define KEY_EVENTS  "events"
#define KEY_PRESETS "presets"
#define KEY_TZ      "tz"

static const char *const all_keys[] = {KEY_HOME,   KEY_TRAVEL, KEY_MODE,    KEY_CHANNEL,
				       KEY_FILTER, KEY_EVENTS, KEY_PRESETS, KEY_TZ};

/* Largest record: the event table, 6 header bytes + 5 + name per entry. */
#define BUF_SIZE (6 + EVENT_TABLE_MAX * (5 + EVENT_NAME_MAX))

static uint8_t buf[BUF_SIZE];

static bool valid_location(const struct same_location *l)
{
	return l->subdivision <= 9U && l->state <= 99U && l->county <= 999U;
}

static size_t put_counties(const struct county_list *c)
{
	size_t n = 0;

	buf[n++] = SCHEMA;
	buf[n++] = c->count;
	for (uint8_t i = 0; i < c->count; i++) {
		buf[n++] = c->codes[i].subdivision;
		buf[n++] = c->codes[i].state;
		buf[n++] = (uint8_t)(c->codes[i].county & 0xFFU);
		buf[n++] = (uint8_t)(c->codes[i].county >> 8);
	}
	return n;
}

static bool get_counties(struct county_list *c, int len)
{
	struct county_list tmp = {0};

	if (len < 2 || buf[0] != SCHEMA || buf[1] > SETTINGS_MAX_COUNTIES ||
	    len != 2 + 4 * buf[1]) {
		return false;
	}
	tmp.count = buf[1];
	for (uint8_t i = 0; i < tmp.count; i++) {
		const uint8_t *p = &buf[2 + 4 * i];

		tmp.codes[i].subdivision = p[0];
		tmp.codes[i].state = p[1];
		tmp.codes[i].county = (uint16_t)(p[2] | (p[3] << 8));
		if (!valid_location(&tmp.codes[i])) {
			return false;
		}
	}
	*c = tmp;
	return true;
}

static size_t put_events(const struct event_table *t)
{
	size_t n = 0;

	buf[n++] = SCHEMA;
	buf[n++] = (uint8_t)(t->version & 0xFFU);
	buf[n++] = (uint8_t)(t->version >> 8);
	buf[n++] = (uint8_t)(t->count & 0xFFU);
	buf[n++] = (uint8_t)(t->count >> 8);
	for (uint16_t i = 0; i < t->count; i++) {
		const struct event_entry *e = &t->entries[i];
		size_t name_len = strlen(e->name);

		memcpy(&buf[n], e->code, 3);
		n += 3;
		buf[n++] = e->cls;
		buf[n++] = (uint8_t)name_len;
		memcpy(&buf[n], e->name, name_len);
		n += name_len;
	}
	return n;
}

/* Rebuilds the table through event_table_add(), so it is validated too. */
static bool get_events(struct event_table *t, int len)
{
	static struct event_table tmp;
	int n = 5;
	uint16_t count;

	if (len < 5 || buf[0] != SCHEMA) {
		return false;
	}
	event_table_clear(&tmp, (uint16_t)(buf[1] | (buf[2] << 8)));
	count = (uint16_t)(buf[3] | (buf[4] << 8));
	for (uint16_t i = 0; i < count; i++) {
		char code[4] = {0};
		char name[EVENT_NAME_MAX + 1] = {0};
		uint8_t cls, name_len;

		if (n + 5 > len) {
			return false;
		}
		memcpy(code, &buf[n], 3);
		cls = buf[n + 3];
		name_len = buf[n + 4];
		n += 5;
		if (name_len > EVENT_NAME_MAX || n + name_len > len) {
			return false;
		}
		memcpy(name, &buf[n], name_len);
		n += name_len;
		if (event_table_add(&tmp, code, name, (enum event_class)cls) != EVENT_TABLE_OK) {
			return false;
		}
	}
	if (n != len) {
		return false;
	}
	*t = tmp;
	return true;
}

/* Reads a record into buf: its length, 0 if absent, -1 if unreadable. */
static int read_record(const char *key)
{
	int len = hal_storage_read(key, buf, sizeof(buf));

	if (len == -ENOENT) {
		return 0;
	}
	if (len < 0 || len > (int)sizeof(buf)) {
		return -1;
	}
	return len;
}

void settings_defaults(struct settings *s)
{
	memset(s, 0, sizeof(*s));
	s->mode = SETTINGS_MODE_HOME;
	s->channel = SETTINGS_CHANNEL_AUTO;
	s->filter = FILTER_WARNINGS_WATCHES;
	event_table_load_default(&s->events);
	strcpy(s->tz, SETTINGS_TZ_DEFAULT);
	(void)tz_parse(s->tz, &s->tz_info);
}

static size_t put_presets(const struct settings *s)
{
	size_t n = 0;

	buf[n++] = SCHEMA;
	buf[n++] = s->preset_count;
	for (uint8_t i = 0; i < s->preset_count; i++) {
		buf[n++] = s->presets[i].band;
		buf[n++] = (uint8_t)(s->presets[i].khz & 0xFFU);
		buf[n++] = (uint8_t)((s->presets[i].khz >> 8) & 0xFFU);
		buf[n++] = (uint8_t)((s->presets[i].khz >> 16) & 0xFFU);
		buf[n++] = (uint8_t)(s->presets[i].khz >> 24);
	}
	return n;
}

static bool get_presets(struct settings *s, int len)
{
	struct codec_preset tmp[SETTINGS_MAX_PRESETS];
	uint8_t count;

	if (len < 2 || buf[0] != SCHEMA || buf[1] > SETTINGS_MAX_PRESETS || len != 2 + 5 * buf[1]) {
		return false;
	}
	count = buf[1];
	for (uint8_t i = 0; i < count; i++) {
		const uint8_t *p = &buf[2 + 5 * i];

		tmp[i].band = p[0];
		tmp[i].khz = (uint32_t)p[1] | ((uint32_t)p[2] << 8) | ((uint32_t)p[3] << 16) |
			     ((uint32_t)p[4] << 24);
		if (!codec_preset_valid(tmp[i].band, tmp[i].khz)) {
			return false;
		}
	}
	s->preset_count = count;
	memcpy(s->presets, tmp, count * sizeof(tmp[0]));
	return true;
}

static bool get_tz(struct settings *s, int len)
{
	char tz[SETTINGS_TZ_MAX + 1];
	struct tz_info info;

	if (len < 2 || len > 1 + SETTINGS_TZ_MAX || buf[0] != SCHEMA) {
		return false;
	}
	memcpy(tz, &buf[1], (size_t)len - 1U);
	tz[len - 1] = '\0';
	if (strlen(tz) != (size_t)len - 1U || tz_parse(tz, &info) != 0) {
		return false;
	}
	strcpy(s->tz, tz);
	s->tz_info = info;
	return true;
}

int settings_restore(struct settings *s)
{
	int bad = 0;
	int len;

	settings_defaults(s);

	len = read_record(KEY_HOME);
	if (len != 0 && !(len > 0 && get_counties(&s->home, len))) {
		bad++;
	}
	len = read_record(KEY_TRAVEL);
	if (len != 0 && !(len > 0 && get_counties(&s->travel, len))) {
		bad++;
	}
	len = read_record(KEY_MODE);
	if (len != 0) {
		if (len == 2 && buf[0] == SCHEMA && buf[1] <= SETTINGS_MODE_TRAVEL) {
			s->mode = buf[1];
		} else {
			bad++;
		}
	}
	len = read_record(KEY_CHANNEL);
	if (len != 0) {
		if (len == 2 && buf[0] == SCHEMA && buf[1] <= SETTINGS_CHANNEL_MAX) {
			s->channel = buf[1];
		} else {
			bad++;
		}
	}
	len = read_record(KEY_FILTER);
	if (len != 0) {
		if (len == 2 + SETTINGS_FILTER_BYTES && buf[0] == SCHEMA &&
		    buf[1] < FILTER_PRESET_COUNT) {
			s->filter = buf[1];
			memcpy(s->custom, &buf[2], SETTINGS_FILTER_BYTES);
		} else {
			bad++;
		}
	}
	len = read_record(KEY_EVENTS);
	if (len != 0 && !(len > 0 && get_events(&s->events, len))) {
		bad++;
	}
	len = read_record(KEY_PRESETS);
	if (len != 0 && !(len > 0 && get_presets(s, len))) {
		bad++;
	}
	len = read_record(KEY_TZ);
	if (len != 0 && !(len > 0 && get_tz(s, len))) {
		bad++;
	}
	return bad;
}

int settings_set_counties(struct settings *s, enum settings_mode which,
			  const struct same_location *codes, uint8_t count)
{
	struct county_list *list;

	if (count > SETTINGS_MAX_COUNTIES || (codes == NULL && count > 0U) ||
	    (which != SETTINGS_MODE_HOME && which != SETTINGS_MODE_TRAVEL)) {
		return -EINVAL;
	}
	for (uint8_t i = 0; i < count; i++) {
		if (!valid_location(&codes[i])) {
			return -EINVAL;
		}
	}
	list = which == SETTINGS_MODE_HOME ? &s->home : &s->travel;
	list->count = count;
	if (count > 0U) {
		memcpy(list->codes, codes, count * sizeof(codes[0]));
	}
	return hal_storage_write(which == SETTINGS_MODE_HOME ? KEY_HOME : KEY_TRAVEL, buf,
				 put_counties(list));
}

int settings_set_mode(struct settings *s, enum settings_mode mode)
{
	if (mode != SETTINGS_MODE_HOME && mode != SETTINGS_MODE_TRAVEL) {
		return -EINVAL;
	}
	s->mode = (uint8_t)mode;
	buf[0] = SCHEMA;
	buf[1] = s->mode;
	return hal_storage_write(KEY_MODE, buf, 2);
}

int settings_set_channel(struct settings *s, uint8_t channel)
{
	if (channel > SETTINGS_CHANNEL_MAX) {
		return -EINVAL;
	}
	s->channel = channel;
	buf[0] = SCHEMA;
	buf[1] = channel;
	return hal_storage_write(KEY_CHANNEL, buf, 2);
}

int settings_set_filter(struct settings *s, enum filter_preset preset,
			const uint8_t custom[SETTINGS_FILTER_BYTES])
{
	if ((int)preset < 0 || preset >= FILTER_PRESET_COUNT ||
	    (preset == FILTER_CUSTOM && custom == NULL)) {
		return -EINVAL;
	}
	s->filter = (uint8_t)preset;
	if (custom != NULL) {
		memcpy(s->custom, custom, SETTINGS_FILTER_BYTES);
	}
	buf[0] = SCHEMA;
	buf[1] = s->filter;
	memcpy(&buf[2], s->custom, SETTINGS_FILTER_BYTES);
	return hal_storage_write(KEY_FILTER, buf, 2 + SETTINGS_FILTER_BYTES);
}

int settings_set_event_table(struct settings *s, const struct event_table *table)
{
	if (table == NULL || table->count > EVENT_TABLE_MAX) {
		return -EINVAL;
	}
	s->events = *table;
	return hal_storage_write(KEY_EVENTS, buf, put_events(&s->events));
}

int settings_set_presets(struct settings *s, const struct codec_preset *presets, uint8_t count)
{
	if (count > SETTINGS_MAX_PRESETS || (presets == NULL && count > 0U)) {
		return -EINVAL;
	}
	for (uint8_t i = 0; i < count; i++) {
		if (!codec_preset_valid(presets[i].band, presets[i].khz)) {
			return -EINVAL;
		}
	}
	s->preset_count = count;
	if (count > 0U) {
		memcpy(s->presets, presets, count * sizeof(presets[0]));
	}
	return hal_storage_write(KEY_PRESETS, buf, put_presets(s));
}

int settings_set_tz(struct settings *s, const char *tz)
{
	struct tz_info info;
	size_t len;

	if (tz == NULL || (len = strlen(tz)) > SETTINGS_TZ_MAX || tz_parse(tz, &info) != 0) {
		return -EINVAL;
	}
	strcpy(s->tz, tz);
	s->tz_info = info;
	buf[0] = SCHEMA;
	memcpy(&buf[1], tz, len);
	return hal_storage_write(KEY_TZ, buf, 1U + len);
}

void settings_factory_reset(struct settings *s)
{
	for (size_t i = 0; i < sizeof(all_keys) / sizeof(all_keys[0]); i++) {
		(void)hal_storage_delete(all_keys[i]);
	}
	settings_defaults(s);
}

int64_t settings_local_time(const struct settings *s, int64_t utc, bool *dst)
{
	return tz_local(&s->tz_info, utc, dst);
}

const struct county_list *settings_active_counties(const struct settings *s)
{
	return s->mode == SETTINGS_MODE_TRAVEL ? &s->travel : &s->home;
}
