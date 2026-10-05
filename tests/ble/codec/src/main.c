/*
 * Bluetooth codec: every characteristic round-trips, malformed values are
 * rejected without touching the output, and maximum values fit the GATT
 * table's max_len.
 */

#include <string.h>
#include <zephyr/ztest.h>

#include "services/ble/codec.h"
#include "services/ble/gatt_table.h"

static uint8_t buf[512];
static size_t n;

static struct same_location loc(uint8_t p, uint8_t ss, uint16_t ccc)
{
	struct same_location l = {.subdivision = p, .state = ss, .county = ccc};

	return l;
}

/* Every prefix of a valid value is rejected as too short. */
static void assert_truncations_rejected(int (*decode)(const uint8_t *, size_t, void *),
					const uint8_t *valid, size_t len, void *out)
{
	for (size_t k = 0; k < len; k++) {
		zassert_true(decode(valid, k, out) < 0, "accepted %zu of %zu bytes", k, len);
	}
}

/* ---- Round trips ---- */

ZTEST(codec, test_counties_round_trip_16)
{
	struct codec_counties in = {.count = 16}, out;

	for (uint8_t i = 0; i < 16; i++) {
		in.codes[i] = loc(i % 10, (uint8_t)(i + 40), (uint16_t)(i * 61));
	}
	n = codec_encode_counties(&in, buf);
	zassert_equal(n, WX_GATT_MAX_COUNTIES, "16 counties is the max length");
	zassert_mem_equal(&buf[2], "040000", 6);
	zassert_ok(codec_decode_counties(buf, n, &out));
	zassert_mem_equal(&out, &in, sizeof(in));
}

ZTEST(codec, test_mode_filter_round_trip)
{
	struct codec_mode m = {.mode = 1, .channel = 7}, m2;
	struct codec_filter f = {.preset = FILTER_CUSTOM, .bitmap = {0xFF, [15] = 0x81}}, f2;

	n = codec_encode_mode(&m, buf);
	zassert_equal(n, WX_GATT_MAX_MODE);
	zassert_ok(codec_decode_mode(buf, n, &m2));
	zassert_mem_equal(&m2, &m, sizeof(m));

	n = codec_encode_filter(&f, buf);
	zassert_equal(n, WX_GATT_MAX_EVENT_FILTER);
	zassert_ok(codec_decode_filter(buf, n, &f2));
	zassert_mem_equal(&f2, &f, sizeof(f));
}

ZTEST(codec, test_time_round_trip)
{
	struct codec_time t = {.utc = 1791172800}, t2;

	strcpy(t.tz, "EST5EDT,M3.2.0,M11.1.0");
	n = codec_encode_time(&t, buf);
	zassert_equal(n, 10 + strlen(t.tz));
	zassert_ok(codec_decode_time(buf, n, &t2));
	zassert_equal(t2.utc, t.utc);
	zassert_str_equal(t2.tz, t.tz);

	memset(t.tz, 'A', CODEC_TZ_MAX);
	t.tz[CODEC_TZ_MAX] = '\0';
	zassert_equal(codec_encode_time(&t, buf), WX_GATT_MAX_TIME, "longest TZ string");
}

ZTEST(codec, test_presets_round_trip)
{
	struct codec_presets p = {.count = 8}, p2;

	for (uint8_t i = 0; i < 8; i++) {
		p.p[i].band = i % 3;
		p.p[i].khz = i % 3 == 0 ? 88100U + i : (i % 3 == 1 ? 1000U : 162475U);
	}
	n = codec_encode_presets(&p, buf);
	zassert_equal(n, WX_GATT_MAX_PRESETS);
	zassert_ok(codec_decode_presets(buf, n, &p2));
	zassert_equal(p2.count, 8);
	for (uint8_t i = 0; i < 8; i++) {
		zassert_equal(p2.p[i].band, p.p[i].band);
		zassert_equal(p2.p[i].khz, p.p[i].khz);
	}
}

ZTEST(codec, test_status_round_trip)
{
	struct codec_status s = {.battery_percent = 57, .hours_left = 130, .snr_db = -3,
				 .rssi_dbuv = 41, .channel = 7, .last_rwt_utc = -1,
				 .health_flags = 0x15, .locked = 1}, s2;

	n = codec_encode_status(&s, buf);
	zassert_equal(n, WX_GATT_MAX_STATUS);
	zassert_ok(codec_decode_status(buf, n, &s2));
	zassert_equal(s2.battery_percent, 57);
	zassert_equal(s2.hours_left, 130);
	zassert_equal(s2.snr_db, -3);
	zassert_equal(s2.rssi_dbuv, 41);
	zassert_equal(s2.channel, 7);
	zassert_equal(s2.last_rwt_utc, -1);
	zassert_equal(s2.health_flags, 0x15);
	zassert_equal(s2.locked, 1);
}

ZTEST(codec, test_event_table_round_trip)
{
	struct codec_et_write w = {.op = CODEC_ET_ENTRY, .index = 57}, w2;
	struct codec_et_read r = {.version = 3, .count = 58, .index = 57}, r2;
	struct codec_et_write ops[] = {
		{.op = CODEC_ET_SELECT, .index = 9},
		{.op = CODEC_ET_BEGIN, .version = 513, .count = 58},
		{.op = CODEC_ET_COMMIT},
		{.op = CODEC_ET_ABORT},
	};

	strcpy(w.entry.code, "NMN");
	strcpy(w.entry.name, "Network Message Notification");
	w.entry.cls = EVENT_CLASS_ADVISORY;
	n = codec_encode_et_write(&w, buf);
	zassert_ok(codec_decode_et_write(buf, n, &w2));
	zassert_mem_equal(&w2, &w, sizeof(w));

	for (size_t i = 0; i < ARRAY_SIZE(ops); i++) {
		n = codec_encode_et_write(&ops[i], buf);
		zassert_ok(codec_decode_et_write(buf, n, &w2), "op %u", ops[i].op);
		zassert_mem_equal(&w2, &ops[i], sizeof(w2));
	}

	r.entry = w.entry;
	memset(r.entry.name, 'N', EVENT_NAME_MAX);
	r.entry.name[EVENT_NAME_MAX] = '\0';
	n = codec_encode_et_read(&r, buf);
	zassert_equal(n, WX_GATT_MAX_EVENT_TABLE, "longest name is the max length");
	zassert_ok(codec_decode_et_read(buf, n, &r2));
	zassert_mem_equal(&r2, &r, sizeof(r));
}

ZTEST(codec, test_alert_log_round_trip)
{
	struct codec_log_entry e = {.count = 16, .index = 15, .received_utc = -1, .outcome = 3,
				    .flags = 1}, e2;
	uint8_t idx;

	memset(e.raw, 'Z', SAME_HEADER_MAX_LEN);
	n = codec_encode_log_entry(&e, buf);
	zassert_equal(n, WX_GATT_MAX_ALERT_LOG, "a 252-character header is the max length");
	zassert_ok(codec_decode_log_entry(buf, n, &e2));
	zassert_mem_equal(&e2, &e, sizeof(e));

	n = codec_encode_log_select(7, buf);
	zassert_ok(codec_decode_log_select(buf, n, &idx));
	zassert_equal(idx, 7);
}

ZTEST(codec, test_control_round_trip)
{
	struct codec_control_indication ind = {.command = CODEC_CMD_FACTORY_RESET,
					       .result = CODEC_CTRL_AWAITING_CONFIRMATION}, ind2;
	enum codec_command cmd;

	for (int c = CODEC_CMD_TEST_ALERT; c <= CODEC_CMD_FACTORY_RESET; c++) {
		n = codec_encode_command((enum codec_command)c, buf);
		zassert_ok(codec_decode_command(buf, n, &cmd));
		zassert_equal(cmd, c);
	}
	n = codec_encode_control_indication(&ind, buf);
	zassert_equal(n, WX_GATT_MAX_CONTROL);
	zassert_ok(codec_decode_control_indication(buf, n, &ind2));
	zassert_mem_equal(&ind2, &ind, sizeof(ind));
}

/* ---- Rejections ---- */

ZTEST(codec, test_truncated_values_rejected)
{
	struct codec_counties c = {.count = 2, .codes = {{0, 48, 453}, {1, 48, 29}}};
	struct codec_time t = {.utc = 5};
	struct codec_presets p = {.count = 1, .p = {{CODEC_BAND_WB, 162400}}};
	struct codec_counties co;
	struct codec_time to;
	struct codec_presets po;
	uint8_t v[600];
	size_t len;

	len = codec_encode_counties(&c, v);
	assert_truncations_rejected((void *)codec_decode_counties, v, len, &co);
	strcpy(t.tz, "UTC0");
	len = codec_encode_time(&t, v);
	assert_truncations_rejected((void *)codec_decode_time, v, len, &to);
	len = codec_encode_presets(&p, v);
	assert_truncations_rejected((void *)codec_decode_presets, v, len, &po);
}

ZTEST(codec, test_trailing_bytes_rejected)
{
	struct codec_mode m = {.mode = 0, .channel = 3}, mo;
	struct codec_counties c = {.count = 1, .codes = {{0, 48, 453}}}, co;

	n = codec_encode_mode(&m, buf);
	buf[n] = 0;
	zassert_equal(codec_decode_mode(buf, n + 1, &mo), CODEC_ERR_LENGTH);
	n = codec_encode_counties(&c, buf);
	zassert_equal(codec_decode_counties(buf, n + 1, &co), CODEC_ERR_LENGTH);
	zassert_equal(codec_decode_counties(buf, n + 6, &co), CODEC_ERR_LENGTH,
		      "a seventh code beyond the count");
}

ZTEST(codec, test_oversized_counts_rejected)
{
	struct codec_counties co;
	struct codec_presets po;
	struct codec_et_write wo;
	uint8_t v[200] = {CODEC_SCHEMA, 17};

	memset(&v[2], '0', 17 * 6);
	zassert_equal(codec_decode_counties(v, 2 + 17 * 6, &co), CODEC_ERR_VALUE, "17 counties");
	v[1] = 9;
	zassert_equal(codec_decode_presets(v, 2 + 9 * 5, &po), CODEC_ERR_VALUE, "9 presets");
	v[1] = CODEC_ET_BEGIN;
	v[2] = 1;
	v[3] = 0;
	v[4] = EVENT_TABLE_MAX + 1;
	zassert_equal(codec_decode_et_write(v, 5, &wo), CODEC_ERR_VALUE, "129 events");
}

ZTEST(codec, test_wrong_schema_rejected)
{
	struct codec_mode m = {.mode = 1, .channel = 2}, mo = {9, 9};

	n = codec_encode_mode(&m, buf);
	buf[0] = 2;
	zassert_equal(codec_decode_mode(buf, n, &mo), CODEC_ERR_SCHEMA);
	buf[0] = 0;
	zassert_equal(codec_decode_mode(buf, n, &mo), CODEC_ERR_SCHEMA);
	zassert_equal(mo.mode, 9, "output untouched");
	zassert_equal(codec_decode_mode(buf, 0, &mo), CODEC_ERR_LENGTH, "empty write");
}

ZTEST(codec, test_county_codes_must_be_six_digits)
{
	static const char *const bad[] = {"04845A", "04 453", "A48453", "-48453", "04845\x7f"};
	struct codec_counties out = {.count = 99};
	uint8_t v[8] = {CODEC_SCHEMA, 1};

	for (size_t i = 0; i < ARRAY_SIZE(bad); i++) {
		memcpy(&v[2], bad[i], 6);
		zassert_equal(codec_decode_counties(v, 8, &out), CODEC_ERR_VALUE, "%s", bad[i]);
	}
	zassert_equal(out.count, 99, "output untouched");
	memcpy(&v[2], "948453", 6);
	zassert_ok(codec_decode_counties(v, 8, &out), "subdivision 9 is valid");
	zassert_equal(out.codes[0].subdivision, 9);
}

ZTEST(codec, test_field_ranges)
{
	struct codec_mode mo;
	struct codec_filter fo;
	struct codec_presets po;
	struct codec_time to;
	struct codec_et_write wo;
	enum codec_command co;
	uint8_t v[64];

	v[0] = CODEC_SCHEMA;
	v[1] = 2;
	v[2] = 0;
	zassert_equal(codec_decode_mode(v, 3, &mo), CODEC_ERR_VALUE, "mode 2");
	v[1] = 0;
	v[2] = 8;
	zassert_equal(codec_decode_mode(v, 3, &mo), CODEC_ERR_VALUE, "channel 8");

	memset(v, 0, sizeof(v));
	v[0] = CODEC_SCHEMA;
	v[1] = FILTER_PRESET_COUNT;
	zassert_equal(codec_decode_filter(v, 18, &fo), CODEC_ERR_VALUE, "unknown preset");

	/* One WB preset off the 25 kHz raster, one FM preset out of band. */
	{
		struct codec_presets bad = {.count = 1, .p = {{CODEC_BAND_WB, 162410}}};

		n = codec_encode_presets(&bad, v);
		zassert_equal(codec_decode_presets(v, n, &po), CODEC_ERR_VALUE);
		bad.p[0].band = CODEC_BAND_FM;
		bad.p[0].khz = 108100;
		n = codec_encode_presets(&bad, v);
		zassert_equal(codec_decode_presets(v, n, &po), CODEC_ERR_VALUE);
	}

	/* Time: negative UTC, control characters and an empty TZ string. */
	{
		struct codec_time t = {.utc = -5};

		strcpy(t.tz, "UTC0");
		n = codec_encode_time(&t, v);
		zassert_equal(codec_decode_time(v, n, &to), CODEC_ERR_VALUE);
		t.utc = 5;
		strcpy(t.tz, "UTC\\n");
		n = codec_encode_time(&t, v);
		zassert_ok(codec_decode_time(v, n, &to), "a backslash is printable");
		v[11] = 0x0A;
		zassert_equal(codec_decode_time(v, n, &to), CODEC_ERR_VALUE, "newline");
		v[9] = 0;
		zassert_equal(codec_decode_time(v, 10, &to), CODEC_ERR_VALUE, "empty TZ");
	}

	/* Event table entries: lower-case code, unknown class, empty name, unknown op. */
	{
		struct codec_et_write w = {.op = CODEC_ET_ENTRY, .index = 0};

		strcpy(w.entry.code, "tor");
		strcpy(w.entry.name, "x");
		n = codec_encode_et_write(&w, v);
		zassert_equal(codec_decode_et_write(v, n, &wo), CODEC_ERR_VALUE);
		strcpy(w.entry.code, "TOR");
		w.entry.cls = EVENT_CLASS_COUNT;
		n = codec_encode_et_write(&w, v);
		zassert_equal(codec_decode_et_write(v, n, &wo), CODEC_ERR_VALUE);
		w.entry.cls = 0;
		w.entry.name[0] = '\0';
		n = codec_encode_et_write(&w, v);
		zassert_equal(codec_decode_et_write(v, n, &wo), CODEC_ERR_VALUE);
		v[1] = 9;
		zassert_equal(codec_decode_et_write(v, 2, &wo), CODEC_ERR_VALUE);
	}

	v[0] = CODEC_SCHEMA;
	v[1] = 0;
	zassert_equal(codec_decode_command(v, 2, &co), CODEC_ERR_VALUE, "command 0");
	v[1] = 4;
	zassert_equal(codec_decode_command(v, 2, &co), CODEC_ERR_VALUE, "command 4");
}

ZTEST_SUITE(codec, NULL, NULL, NULL, NULL, NULL);
