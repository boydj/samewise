/*
 * Bluetooth value codec. Plain C99.
 */

#include <stdbool.h>
#include <string.h>

#include "services/ble/codec.h"

/* ---- Little-endian helpers ---- */

static void put16(uint8_t *p, uint16_t v)
{
	p[0] = (uint8_t)v;
	p[1] = (uint8_t)(v >> 8);
}

static void put32(uint8_t *p, uint32_t v)
{
	put16(p, (uint16_t)v);
	put16(p + 2, (uint16_t)(v >> 16));
}

static void put64(uint8_t *p, int64_t v)
{
	uint64_t u = (uint64_t)v;

	put32(p, (uint32_t)u);
	put32(p + 4, (uint32_t)(u >> 32));
}

static uint16_t get16(const uint8_t *p)
{
	return (uint16_t)(p[0] | (p[1] << 8));
}

static uint32_t get32(const uint8_t *p)
{
	return (uint32_t)get16(p) | ((uint32_t)get16(p + 2) << 16);
}

static int64_t get64(const uint8_t *p)
{
	return (int64_t)((uint64_t)get32(p) | ((uint64_t)get32(p + 4) << 32));
}

/* Common prefix check: at least min bytes and the right schema. */
static int head(const uint8_t *buf, size_t len, size_t min)
{
	if (buf == NULL || len < 1U) {
		return CODEC_ERR_LENGTH;
	}
	if (buf[0] != CODEC_SCHEMA) {
		return CODEC_ERR_SCHEMA;
	}
	return len < min ? CODEC_ERR_LENGTH : CODEC_OK;
}

static bool printable(const uint8_t *p, size_t n)
{
	for (size_t i = 0; i < n; i++) {
		if (p[i] < 0x20U || p[i] > 0x7EU) {
			return false;
		}
	}
	return true;
}

/* ---- Counties ---- */

size_t codec_encode_counties(const struct codec_counties *v, uint8_t *buf)
{
	size_t n = 0;

	buf[n++] = CODEC_SCHEMA;
	buf[n++] = v->count;
	for (uint8_t i = 0; i < v->count; i++) {
		const struct same_location *l = &v->codes[i];

		buf[n++] = (uint8_t)('0' + l->subdivision);
		buf[n++] = (uint8_t)('0' + l->state / 10U);
		buf[n++] = (uint8_t)('0' + l->state % 10U);
		buf[n++] = (uint8_t)('0' + l->county / 100U);
		buf[n++] = (uint8_t)('0' + l->county / 10U % 10U);
		buf[n++] = (uint8_t)('0' + l->county % 10U);
	}
	return n;
}

int codec_decode_counties(const uint8_t *buf, size_t len, struct codec_counties *out)
{
	struct codec_counties tmp;
	int err = head(buf, len, 2);

	if (err != CODEC_OK) {
		return err;
	}
	if (buf[1] > CODEC_MAX_COUNTIES) {
		return CODEC_ERR_VALUE;
	}
	if (len != 2U + 6U * buf[1]) {
		return CODEC_ERR_LENGTH;
	}
	memset(&tmp, 0, sizeof(tmp));
	tmp.count = buf[1];
	for (uint8_t i = 0; i < tmp.count; i++) {
		const uint8_t *p = &buf[2 + 6 * i];

		for (int k = 0; k < 6; k++) {
			if (p[k] < '0' || p[k] > '9') {
				return CODEC_ERR_VALUE; /* six digits, subdivision 0-9 */
			}
		}
		tmp.codes[i].subdivision = (uint8_t)(p[0] - '0');
		tmp.codes[i].state = (uint8_t)((p[1] - '0') * 10 + (p[2] - '0'));
		tmp.codes[i].county = (uint16_t)((p[3] - '0') * 100 + (p[4] - '0') * 10 + (p[5] - '0'));
	}
	*out = tmp;
	return CODEC_OK;
}

/* ---- Mode ---- */

size_t codec_encode_mode(const struct codec_mode *v, uint8_t *buf)
{
	buf[0] = CODEC_SCHEMA;
	buf[1] = v->mode;
	buf[2] = v->channel;
	return 3;
}

int codec_decode_mode(const uint8_t *buf, size_t len, struct codec_mode *out)
{
	int err = head(buf, len, 3);

	if (err != CODEC_OK) {
		return err;
	}
	if (len != 3U) {
		return CODEC_ERR_LENGTH;
	}
	if (buf[1] > 1U || buf[2] > CODEC_CHANNEL_MAX) {
		return CODEC_ERR_VALUE;
	}
	out->mode = buf[1];
	out->channel = buf[2];
	return CODEC_OK;
}

/* ---- Event filter ---- */

size_t codec_encode_filter(const struct codec_filter *v, uint8_t *buf)
{
	buf[0] = CODEC_SCHEMA;
	buf[1] = v->preset;
	memcpy(&buf[2], v->bitmap, FILTER_CUSTOM_BYTES);
	return 2U + FILTER_CUSTOM_BYTES;
}

int codec_decode_filter(const uint8_t *buf, size_t len, struct codec_filter *out)
{
	int err = head(buf, len, 2U + FILTER_CUSTOM_BYTES);

	if (err != CODEC_OK) {
		return err;
	}
	if (len != 2U + FILTER_CUSTOM_BYTES) {
		return CODEC_ERR_LENGTH;
	}
	if (buf[1] >= FILTER_PRESET_COUNT) {
		return CODEC_ERR_VALUE;
	}
	out->preset = buf[1];
	memcpy(out->bitmap, &buf[2], FILTER_CUSTOM_BYTES);
	return CODEC_OK;
}

/* ---- Time ---- */

size_t codec_encode_time(const struct codec_time *v, uint8_t *buf)
{
	size_t tz_len = strlen(v->tz);

	buf[0] = CODEC_SCHEMA;
	put64(&buf[1], v->utc);
	buf[9] = (uint8_t)tz_len;
	memcpy(&buf[10], v->tz, tz_len);
	return 10U + tz_len;
}

int codec_decode_time(const uint8_t *buf, size_t len, struct codec_time *out)
{
	int err = head(buf, len, 10);
	uint8_t tz_len;
	int64_t utc;

	if (err != CODEC_OK) {
		return err;
	}
	tz_len = buf[9];
	if (tz_len < 1U || tz_len > CODEC_TZ_MAX) {
		return CODEC_ERR_VALUE;
	}
	if (len != 10U + tz_len) {
		return CODEC_ERR_LENGTH;
	}
	utc = get64(&buf[1]);
	if (utc < 0 || !printable(&buf[10], tz_len)) {
		return CODEC_ERR_VALUE;
	}
	out->utc = utc;
	memcpy(out->tz, &buf[10], tz_len);
	out->tz[tz_len] = '\0';
	return CODEC_OK;
}

/* ---- Presets ---- */

static bool preset_valid(uint8_t band, uint32_t khz)
{
	switch (band) {
	case CODEC_BAND_FM:
		return khz >= 87500U && khz <= 108000U;
	case CODEC_BAND_AM:
		return khz >= 520U && khz <= 1710U;
	case CODEC_BAND_WB:
		return khz >= 162400U && khz <= 162550U && (khz - 162400U) % 25U == 0U;
	default:
		return false;
	}
}

size_t codec_encode_presets(const struct codec_presets *v, uint8_t *buf)
{
	size_t n = 0;

	buf[n++] = CODEC_SCHEMA;
	buf[n++] = v->count;
	for (uint8_t i = 0; i < v->count; i++) {
		buf[n++] = v->p[i].band;
		put32(&buf[n], v->p[i].khz);
		n += 4;
	}
	return n;
}

int codec_decode_presets(const uint8_t *buf, size_t len, struct codec_presets *out)
{
	struct codec_presets tmp;
	int err = head(buf, len, 2);

	if (err != CODEC_OK) {
		return err;
	}
	if (buf[1] > CODEC_MAX_PRESETS) {
		return CODEC_ERR_VALUE;
	}
	if (len != 2U + 5U * buf[1]) {
		return CODEC_ERR_LENGTH;
	}
	memset(&tmp, 0, sizeof(tmp));
	tmp.count = buf[1];
	for (uint8_t i = 0; i < tmp.count; i++) {
		tmp.p[i].band = buf[2 + 5 * i];
		tmp.p[i].khz = get32(&buf[3 + 5 * i]);
		if (!preset_valid(tmp.p[i].band, tmp.p[i].khz)) {
			return CODEC_ERR_VALUE;
		}
	}
	*out = tmp;
	return CODEC_OK;
}

/* ---- Status ---- */

#define STATUS_LEN 20U

size_t codec_encode_status(const struct codec_status *v, uint8_t *buf)
{
	buf[0] = CODEC_SCHEMA;
	buf[1] = v->battery_percent;
	put16(&buf[2], v->hours_left);
	buf[4] = (uint8_t)v->snr_db;
	buf[5] = (uint8_t)v->rssi_dbuv;
	buf[6] = v->channel;
	put64(&buf[7], v->last_rwt_utc);
	put32(&buf[15], v->health_flags);
	buf[19] = v->locked;
	return STATUS_LEN;
}

int codec_decode_status(const uint8_t *buf, size_t len, struct codec_status *out)
{
	int err = head(buf, len, STATUS_LEN);

	if (err != CODEC_OK) {
		return err;
	}
	if (len != STATUS_LEN) {
		return CODEC_ERR_LENGTH;
	}
	if (buf[1] > 100U || buf[6] > CODEC_CHANNEL_MAX || buf[19] > 1U) {
		return CODEC_ERR_VALUE;
	}
	out->battery_percent = buf[1];
	out->hours_left = get16(&buf[2]);
	out->snr_db = (int8_t)buf[4];
	out->rssi_dbuv = (int8_t)buf[5];
	out->channel = buf[6];
	out->last_rwt_utc = get64(&buf[7]);
	out->health_flags = get32(&buf[15]);
	out->locked = buf[19];
	return CODEC_OK;
}

/* ---- Event table ---- */

static bool valid_code(const char *c)
{
	for (int i = 0; i < 3; i++) {
		if (c[i] < 'A' || c[i] > 'Z') {
			return false;
		}
	}
	return true;
}

/* code(3) class(1) name_len(1) name: shared by entry writes and reads. */
static size_t put_entry(const struct event_entry *e, uint8_t *p)
{
	size_t name_len = strlen(e->name);

	memcpy(p, e->code, 3);
	p[3] = e->cls;
	p[4] = (uint8_t)name_len;
	memcpy(&p[5], e->name, name_len);
	return 5U + name_len;
}

/* Parses an entry occupying exactly len bytes. */
static int get_entry(const uint8_t *p, size_t len, struct event_entry *e)
{
	uint8_t name_len;

	if (len < 5U) {
		return CODEC_ERR_LENGTH;
	}
	name_len = p[4];
	if (name_len < 1U || name_len > EVENT_NAME_MAX) {
		return CODEC_ERR_VALUE;
	}
	if (len != 5U + name_len) {
		return CODEC_ERR_LENGTH;
	}
	if (!valid_code((const char *)p) || p[3] >= EVENT_CLASS_COUNT ||
	    !printable(&p[5], name_len)) {
		return CODEC_ERR_VALUE;
	}
	memset(e, 0, sizeof(*e));
	memcpy(e->code, p, 3);
	e->cls = p[3];
	memcpy(e->name, &p[5], name_len);
	return CODEC_OK;
}

size_t codec_encode_et_write(const struct codec_et_write *v, uint8_t *buf)
{
	size_t n = 0;

	buf[n++] = CODEC_SCHEMA;
	buf[n++] = v->op;
	switch (v->op) {
	case CODEC_ET_SELECT:
		buf[n++] = v->index;
		break;
	case CODEC_ET_BEGIN:
		put16(&buf[n], v->version);
		n += 2;
		buf[n++] = v->count;
		break;
	case CODEC_ET_ENTRY:
		buf[n++] = v->index;
		n += put_entry(&v->entry, &buf[n]);
		break;
	default:
		break;
	}
	return n;
}

int codec_decode_et_write(const uint8_t *buf, size_t len, struct codec_et_write *out)
{
	struct codec_et_write tmp;
	int err = head(buf, len, 2);

	if (err != CODEC_OK) {
		return err;
	}
	memset(&tmp, 0, sizeof(tmp));
	tmp.op = buf[1];
	switch (tmp.op) {
	case CODEC_ET_SELECT:
		if (len != 3U) {
			return CODEC_ERR_LENGTH;
		}
		tmp.index = buf[2];
		break;
	case CODEC_ET_BEGIN:
		if (len != 5U) {
			return CODEC_ERR_LENGTH;
		}
		tmp.version = get16(&buf[2]);
		tmp.count = buf[4];
		if (tmp.count > EVENT_TABLE_MAX) {
			return CODEC_ERR_VALUE;
		}
		break;
	case CODEC_ET_ENTRY:
		if (len < 3U) {
			return CODEC_ERR_LENGTH;
		}
		tmp.index = buf[2];
		err = get_entry(&buf[3], len - 3U, &tmp.entry);
		if (err != CODEC_OK) {
			return err;
		}
		break;
	case CODEC_ET_COMMIT:
	case CODEC_ET_ABORT:
		if (len != 2U) {
			return CODEC_ERR_LENGTH;
		}
		break;
	default:
		return CODEC_ERR_VALUE;
	}
	*out = tmp;
	return CODEC_OK;
}

size_t codec_encode_et_read(const struct codec_et_read *v, uint8_t *buf)
{
	buf[0] = CODEC_SCHEMA;
	put16(&buf[1], v->version);
	buf[3] = v->count;
	buf[4] = v->index;
	return 5U + put_entry(&v->entry, &buf[5]);
}

int codec_decode_et_read(const uint8_t *buf, size_t len, struct codec_et_read *out)
{
	struct codec_et_read tmp;
	int err = head(buf, len, 5);

	if (err != CODEC_OK) {
		return err;
	}
	memset(&tmp, 0, sizeof(tmp));
	tmp.version = get16(&buf[1]);
	tmp.count = buf[3];
	tmp.index = buf[4];
	err = get_entry(&buf[5], len - 5U, &tmp.entry);
	if (err != CODEC_OK) {
		return err;
	}
	if (tmp.index >= tmp.count) {
		return CODEC_ERR_VALUE;
	}
	*out = tmp;
	return CODEC_OK;
}

/* ---- Alert log ---- */

size_t codec_encode_log_select(uint8_t index, uint8_t *buf)
{
	buf[0] = CODEC_SCHEMA;
	buf[1] = index;
	return 2;
}

int codec_decode_log_select(const uint8_t *buf, size_t len, uint8_t *index)
{
	int err = head(buf, len, 2);

	if (err != CODEC_OK) {
		return err;
	}
	if (len != 2U) {
		return CODEC_ERR_LENGTH;
	}
	*index = buf[1];
	return CODEC_OK;
}

size_t codec_encode_log_entry(const struct codec_log_entry *v, uint8_t *buf)
{
	size_t raw_len = strlen(v->raw);

	buf[0] = CODEC_SCHEMA;
	buf[1] = v->count;
	buf[2] = v->index;
	put64(&buf[3], v->received_utc);
	buf[11] = v->outcome;
	buf[12] = v->flags;
	buf[13] = (uint8_t)raw_len;
	memcpy(&buf[14], v->raw, raw_len);
	return 14U + raw_len;
}

int codec_decode_log_entry(const uint8_t *buf, size_t len, struct codec_log_entry *out)
{
	struct codec_log_entry tmp;
	int err = head(buf, len, 14);
	uint8_t raw_len;

	if (err != CODEC_OK) {
		return err;
	}
	raw_len = buf[13];
	if (raw_len > SAME_HEADER_MAX_LEN) {
		return CODEC_ERR_VALUE;
	}
	if (len != 14U + raw_len) {
		return CODEC_ERR_LENGTH;
	}
	if (!printable(&buf[14], raw_len) || (buf[1] != 0U && buf[2] >= buf[1])) {
		return CODEC_ERR_VALUE;
	}
	memset(&tmp, 0, sizeof(tmp));
	tmp.count = buf[1];
	tmp.index = buf[2];
	tmp.received_utc = get64(&buf[3]);
	tmp.outcome = buf[11];
	tmp.flags = buf[12];
	memcpy(tmp.raw, &buf[14], raw_len);
	*out = tmp;
	return CODEC_OK;
}

/* ---- Control ---- */

size_t codec_encode_command(enum codec_command cmd, uint8_t *buf)
{
	buf[0] = CODEC_SCHEMA;
	buf[1] = (uint8_t)cmd;
	return 2;
}

int codec_decode_command(const uint8_t *buf, size_t len, enum codec_command *out)
{
	int err = head(buf, len, 2);

	if (err != CODEC_OK) {
		return err;
	}
	if (len != 2U) {
		return CODEC_ERR_LENGTH;
	}
	if (buf[1] < CODEC_CMD_TEST_ALERT || buf[1] > CODEC_CMD_FACTORY_RESET) {
		return CODEC_ERR_VALUE;
	}
	*out = (enum codec_command)buf[1];
	return CODEC_OK;
}

size_t codec_encode_control_indication(const struct codec_control_indication *v, uint8_t *buf)
{
	buf[0] = CODEC_SCHEMA;
	buf[1] = v->command;
	buf[2] = v->result;
	return 3;
}

int codec_decode_control_indication(const uint8_t *buf, size_t len,
				    struct codec_control_indication *out)
{
	int err = head(buf, len, 3);

	if (err != CODEC_OK) {
		return err;
	}
	if (len != 3U) {
		return CODEC_ERR_LENGTH;
	}
	if (buf[1] < CODEC_CMD_TEST_ALERT || buf[1] > CODEC_CMD_FACTORY_RESET ||
	    buf[2] > CODEC_CTRL_REJECTED) {
		return CODEC_ERR_VALUE;
	}
	out->command = buf[1];
	out->result = buf[2];
	return CODEC_OK;
}
