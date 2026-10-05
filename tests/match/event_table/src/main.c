/*
 * Event table: building, lookup, validation and capacity.
 */

#include <zephyr/ztest.h>

#include "services/match/event_table.h"

static struct event_table t;

static void before(void *f)
{
	ARG_UNUSED(f);
	event_table_clear(&t, 7);
}

ZTEST_SUITE(event_table, NULL, NULL, before, NULL, NULL);

ZTEST(event_table, test_add_and_find)
{
	const struct event_entry *e;

	zassert_ok(event_table_add(&t, "TOR", "Tornado Warning", EVENT_CLASS_WARNING));
	zassert_ok(event_table_add(&t, "SVA", "Severe Thunderstorm Watch", EVENT_CLASS_WATCH));
	zassert_ok(event_table_add(&t, "RWT", "Required Weekly Test", EVENT_CLASS_TEST));
	zassert_equal(t.version, 7);
	zassert_equal(t.count, 3);

	e = event_table_find(&t, "SVA");
	zassert_not_null(e);
	zassert_str_equal(e->name, "Severe Thunderstorm Watch");
	zassert_equal(e->cls, EVENT_CLASS_WATCH);
	zassert_equal(event_table_index(&t, "RWT"), 2, "index is insertion order");
}

ZTEST(event_table, test_lookup_needs_only_three_chars)
{
	zassert_ok(event_table_add(&t, "TOR", "Tornado Warning", EVENT_CLASS_WARNING));
	/* A code straight out of a header, not NUL-terminated. */
	zassert_equal(event_table_index(&t, "TOR-048453"), 0);
}

ZTEST(event_table, test_unknown_code)
{
	zassert_ok(event_table_add(&t, "TOR", "Tornado Warning", EVENT_CLASS_WARNING));
	zassert_is_null(event_table_find(&t, "XYZ"));
	zassert_equal(event_table_index(&t, "XYZ"), -1);
	zassert_equal(event_table_index(&t, NULL), -1);
}

ZTEST(event_table, test_rejects_bad_entries)
{
	char long_name[EVENT_NAME_MAX + 2];

	memset(long_name, 'x', sizeof(long_name) - 1);
	long_name[sizeof(long_name) - 1] = '\0';

	zassert_equal(event_table_add(&t, "TO", "x", EVENT_CLASS_WARNING), EVENT_TABLE_ERR_CODE);
	zassert_equal(event_table_add(&t, "TORR", "x", EVENT_CLASS_WARNING), EVENT_TABLE_ERR_CODE);
	zassert_equal(event_table_add(&t, "tor", "x", EVENT_CLASS_WARNING), EVENT_TABLE_ERR_CODE);
	zassert_equal(event_table_add(&t, "T0R", "x", EVENT_CLASS_WARNING), EVENT_TABLE_ERR_CODE);
	zassert_equal(event_table_add(&t, "TOR", "", EVENT_CLASS_WARNING), EVENT_TABLE_ERR_NAME);
	zassert_equal(event_table_add(&t, "TOR", long_name, EVENT_CLASS_WARNING),
		      EVENT_TABLE_ERR_NAME);
	zassert_equal(event_table_add(&t, "TOR", "x", EVENT_CLASS_COUNT), EVENT_TABLE_ERR_CLASS);
	zassert_ok(event_table_add(&t, "TOR", "Tornado Warning", EVENT_CLASS_WARNING));
	zassert_equal(event_table_add(&t, "TOR", "Again", EVENT_CLASS_WATCH),
		      EVENT_TABLE_ERR_DUPLICATE);
	zassert_equal(t.count, 1);
}

ZTEST(event_table, test_capacity)
{
	char code[4] = "AAA";

	for (int i = 0; i < EVENT_TABLE_MAX; i++) {
		code[1] = (char)('A' + i / 26);
		code[2] = (char)('A' + i % 26);
		zassert_ok(event_table_add(&t, code, "x", EVENT_CLASS_ADVISORY), "%s", code);
	}
	zassert_equal(event_table_add(&t, "ZZZ", "x", EVENT_CLASS_ADVISORY), EVENT_TABLE_ERR_FULL);
}

ZTEST(event_table, test_class_names)
{
	zassert_str_equal(event_class_name(EVENT_CLASS_WARNING), "warning");
	zassert_str_equal(event_class_name(EVENT_CLASS_TEST), "test");
	zassert_str_equal(event_class_name(99), "unknown");
}

/* ---- Built-in default table ---- */

/* NWS "NWR NWS Event Codes", https://www.weather.gov/nwr/eventcodes (printed Oct 4, 2026). */
static const char *const nws_codes[] = {
	/* Weather-related */
	"BZW", "CFA", "CFW", "DSW", "EWW", "FFA", "FFW", "FFS", "FLA", "FLW", "FLS",
	"HWA", "HWW", "HUA", "HUW", "HLS", "SVA", "SVR", "SVS", "SQW", "SMW", "SPS",
	"SSA", "SSW", "TOA", "TOR", "TRA", "TRW", "TSA", "TSW", "WSA", "WSW",
	/* Non-weather-related */
	"AVA", "AVW", "BLU", "CAE", "CDW", "CEM", "EQW", "EVI", "FRW", "HMW", "LEW",
	"LAE", "TOE", "NUW", "RHW", "SPW", "VOW",
	/* Administrative */
	"ADR", "DMO", "RMT", "RWT",
};

/* 47 CFR 11.31 Table 2 codes the NWS page doesn't list. */
static const char *const cfr_only_codes[] = {"EAN", "NIC", "NPT", "MEP", "NMN"};

static void expect_class(const char *code, enum event_class cls)
{
	const struct event_entry *e = event_table_find(&t, code);

	zassert_not_null(e, "%s missing", code);
	zassert_equal(e->cls, cls, "%s is %s, want %s", code, event_class_name(e->cls),
		      event_class_name(cls));
}

ZTEST(event_table, test_default_has_every_nws_and_cfr_code)
{
	event_table_load_default(&t);
	zassert_equal(ARRAY_SIZE(nws_codes), 53);
	for (size_t i = 0; i < ARRAY_SIZE(nws_codes); i++) {
		zassert_not_null(event_table_find(&t, nws_codes[i]), "%s", nws_codes[i]);
	}
	for (size_t i = 0; i < ARRAY_SIZE(cfr_only_codes); i++) {
		zassert_not_null(event_table_find(&t, cfr_only_codes[i]), "%s", cfr_only_codes[i]);
	}
	zassert_equal(t.count, 58, "nothing else");
	zassert_equal(t.version, 1);
}

ZTEST(event_table, test_default_classes)
{
	static const char *const watches[] = {"AVA", "CFA", "FFA", "FLA", "HWA", "HUA",
					      "SVA", "SSA", "TOA", "TRA", "TSA", "WSA"};
	static const char *const statements[] = {"FFS", "FLS", "HLS", "SVS", "SPS"};
	static const char *const tests[] = {"RWT", "RMT", "NPT", "DMO"};
	/* Urgent codes whose names don't say Warning alert as warnings (user decision). */
	static const char *const urgent[] = {"TOR", "SVR", "EVI", "EAN", "CAE", "CEM", "LAE"};
	static const char *const advisories[] = {"BLU", "TOE", "ADR", "NIC", "NMN", "MEP"};

	event_table_load_default(&t);
	for (size_t i = 0; i < ARRAY_SIZE(watches); i++) {
		expect_class(watches[i], EVENT_CLASS_WATCH);
	}
	for (size_t i = 0; i < ARRAY_SIZE(statements); i++) {
		expect_class(statements[i], EVENT_CLASS_STATEMENT);
	}
	for (size_t i = 0; i < ARRAY_SIZE(tests); i++) {
		expect_class(tests[i], EVENT_CLASS_TEST);
	}
	for (size_t i = 0; i < ARRAY_SIZE(urgent); i++) {
		expect_class(urgent[i], EVENT_CLASS_WARNING);
	}
	for (size_t i = 0; i < ARRAY_SIZE(advisories); i++) {
		expect_class(advisories[i], EVENT_CLASS_ADVISORY);
	}
	/* Every code ending in W is a warning (FCC naming convention). */
	for (int i = 0; i < t.count; i++) {
		if (t.entries[i].code[2] == 'W') {
			expect_class(t.entries[i].code, EVENT_CLASS_WARNING);
		}
	}
}

ZTEST(event_table, test_default_names)
{
	event_table_load_default(&t);
	zassert_str_equal(event_table_find(&t, "TOR")->name, "Tornado Warning");
	zassert_str_equal(event_table_find(&t, "TOE")->name, "911 Telephone Outage Emergency");
	zassert_str_equal(event_table_find(&t, "EAN")->name, "Emergency Action Notification");
}
