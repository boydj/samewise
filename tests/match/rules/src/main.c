/*
 * Matcher, filter, SAME time and duplicate suppression.
 */

#include <string.h>
#include <zephyr/ztest.h>

#include "services/match/dup.h"
#include "services/match/filter.h"
#include "services/match/match.h"
#include "services/match/same_time.h"
#include "services/same/same_header.h"

static struct same_header h;

static const struct same_header *hdr(const char *text)
{
	zassert_ok(same_parse_header(text, strlen(text), &h), "%s", text);
	return &h;
}

static struct same_location loc(const char *pssccc)
{
	struct same_location l = {
		.subdivision = (uint8_t)(pssccc[0] - '0'),
		.state = (uint8_t)((pssccc[1] - '0') * 10 + (pssccc[2] - '0')),
		.county = (uint16_t)((pssccc[3] - '0') * 100 + (pssccc[4] - '0') * 10 +
				     (pssccc[5] - '0')),
	};
	return l;
}

static bool m(const char *alert, const char *configured)
{
	struct same_location a = loc(alert), c = loc(configured);

	return match_location(&a, &c);
}

/* 2025-01-01T00:00:00Z and friends, from same_days_from_civil(). */
static int64_t utc(int64_t y, unsigned int mo, unsigned int d, int hh, int mm)
{
	return same_days_from_civil(y, mo, d) * 86400 + hh * 3600 + mm * 60;
}

/* ---- Matching ---- */

ZTEST(match_rules, test_exact_county_and_subdivisions)
{
	zassert_true(m("048453", "048453"), "exact");
	zassert_true(m("048453", "348453"), "alert subdivision 0 = whole county");
	zassert_true(m("348453", "048453"), "configured subdivision 0 = whole county");
	zassert_true(m("348453", "348453"), "same subdivision");
	zassert_false(m("348453", "748453"), "different non-zero subdivisions");
	zassert_false(m("048453", "048491"), "different county");
	zassert_false(m("048453", "040453"), "different state");
}

ZTEST(match_rules, test_whole_state_and_whole_country)
{
	zassert_true(m("048000", "048453"), "alert county 000 = whole state");
	zassert_true(m("048000", "748453"), "even a configured subdivision");
	zassert_false(m("040000", "048453"), "other state");
	zassert_true(m("000000", "048453"), "state 00 = all of the U.S.");
	zassert_true(m("000123", "312345"), "state 00 with any county");
	zassert_true(m("048453", "048000"), "configured county 000 = whole state");
	zassert_false(m("040109", "048000"));
}

ZTEST(match_rules, test_header_mask_and_empty_list)
{
	const struct same_location home[] = {loc("048453"), loc("348029")};
	const struct same_header *x = hdr("ZCZC-WXR-TOR-048491-048453-748029-048029+0030-2781915-KEWX/NWS-");

	zassert_equal(match_header(x, home, 2), 0x0AU, "locations 1 and 3 match");
	zassert_equal(match_header(x, home, 0), 0x0FU, "an empty list accepts everything");
	zassert_equal(match_header(hdr("ZCZC-WXR-TOR-040001+0030-2781915-KEWX/NWS-"), home, 2), 0);
}

/* ---- Filter ---- */

static struct event_table t;

static void make_table(void)
{
	event_table_clear(&t, 1);
	zassert_ok(event_table_add(&t, "TOR", "Tornado Warning", EVENT_CLASS_WARNING));
	zassert_ok(event_table_add(&t, "TOA", "Tornado Watch", EVENT_CLASS_WATCH));
	zassert_ok(event_table_add(&t, "BLU", "Blue Alert", EVENT_CLASS_ADVISORY));
	zassert_ok(event_table_add(&t, "SPS", "Special Weather Statement", EVENT_CLASS_STATEMENT));
	zassert_ok(event_table_add(&t, "RWT", "Required Weekly Test", EVENT_CLASS_TEST));
	zassert_ok(event_table_add(&t, "RMT", "Required Monthly Test", EVENT_CLASS_TEST));
}

ZTEST(match_rules, test_presets)
{
	static const struct {
		enum filter_preset p;
		const char *alerts; /* codes that alert, in table order */
	} cases[] = {
		{FILTER_WARNINGS, "TOR"},
		{FILTER_WARNINGS_WATCHES, "TOR TOA"},
		{FILTER_ALL, "TOR TOA BLU SPS"},
	};

	make_table();
	for (size_t c = 0; c < ARRAY_SIZE(cases); c++) {
		for (int i = 0; i < t.count; i++) {
			const char *code = t.entries[i].code;
			bool want = strstr(cases[c].alerts, code) != NULL;
			enum filter_verdict v = filter_decide(&t, code, cases[c].p, NULL);

			zassert_equal(v, want ? FILTER_VERDICT_ALERT : FILTER_VERDICT_LOG,
				      "preset %d, %s", cases[c].p, code);
		}
	}
}

ZTEST(match_rules, test_custom_bitmap_and_tests_never_alert)
{
	uint8_t custom[FILTER_CUSTOM_BYTES] = {0};

	make_table();
	custom[0] = (1U << 2) | (1U << 4) | (1U << 5); /* BLU, RWT, RMT */
	zassert_equal(filter_decide(&t, "BLU", FILTER_CUSTOM, custom), FILTER_VERDICT_ALERT);
	zassert_equal(filter_decide(&t, "TOR", FILTER_CUSTOM, custom), FILTER_VERDICT_LOG);
	zassert_equal(filter_decide(&t, "RWT", FILTER_CUSTOM, custom), FILTER_VERDICT_LOG,
		      "RWT never alerts, even when selected");
	zassert_equal(filter_decide(&t, "RMT", FILTER_ALL, NULL), FILTER_VERDICT_LOG);
}

ZTEST(match_rules, test_unknown_codes_are_log_only)
{
	make_table();
	for (int p = 0; p < FILTER_PRESET_COUNT; p++) {
		zassert_equal(filter_decide(&t, "XYZ", (enum filter_preset)p, NULL),
			      FILTER_VERDICT_UNKNOWN);
	}
}

/* ---- Time ---- */

ZTEST(match_rules, test_calendar_helpers)
{
	zassert_equal(same_days_from_civil(1970, 1, 1), 0);
	zassert_equal(same_days_from_civil(2026, 10, 5), 20731);
	zassert_equal(same_year_of(utc(2024, 12, 31, 23, 59)), 2024);
	zassert_equal(same_year_of(utc(2025, 1, 1, 0, 0)), 2025);
}

ZTEST(match_rules, test_issue_time_same_year)
{
	/* Day 278 of 2026 is October 5. */
	hdr("ZCZC-WXR-TOR-048453+0030-2781915-KEWX/NWS-");
	zassert_equal(same_issue_utc(&h, utc(2026, 10, 5, 19, 17)), utc(2026, 10, 5, 19, 15));
	zassert_equal(same_issue_utc(&h, -1), -1, "clock unset");
}

ZTEST(match_rules, test_day_366_received_on_january_1_is_previous_year)
{
	hdr("ZCZC-WXR-TOR-048453+0030-3662350-KEWX/NWS-");
	zassert_equal(same_issue_utc(&h, utc(2025, 1, 1, 0, 5)), utc(2024, 12, 31, 23, 50),
		      "2024 is a leap year: day 366 is Dec 31");
}

ZTEST(match_rules, test_day_365_received_on_january_1_is_previous_year)
{
	hdr("ZCZC-WXR-TOR-048453+0030-3652350-KEWX/NWS-");
	zassert_equal(same_issue_utc(&h, utc(2026, 1, 1, 0, 5)), utc(2025, 12, 31, 23, 50));
}

ZTEST(match_rules, test_day_1_received_on_december_31_is_next_year)
{
	hdr("ZCZC-WXR-TOR-048453+0030-0010005-KEWX/NWS-");
	zassert_equal(same_issue_utc(&h, utc(2026, 12, 31, 23, 58)), utc(2027, 1, 1, 0, 5),
		      "a fast transmitter clock across midnight");
}

ZTEST(match_rules, test_expiry)
{
	int64_t now = utc(2026, 10, 5, 19, 20);

	hdr("ZCZC-WXR-TOR-048453+0130-2781915-KEWX/NWS-");
	zassert_equal(same_purge_s(&h), 5400);
	/* Issued 19:15, purge 1:30 -> 20:45, which is 85 minutes after 19:20. */
	zassert_equal(same_expiry_ms(&h, now, 1000), 1000 + 85 * 60 * 1000);
	zassert_equal(same_expiry_ms(&h, -1, 1000), 1000 + 5400 * 1000, "unset: receive + purge");
	zassert_true(same_expiry_ms(&h, utc(2026, 10, 5, 21, 0), 1000) <= 1000, "stale on arrival");
}

/* ---- Duplicates ---- */

static struct dup_store d;

ZTEST(match_rules, test_rebroadcast_inside_window_is_duplicate)
{
	dup_init(&d);
	hdr("ZCZC-WXR-TOR-048453-048491+0030-2781915-KEWX/NWS-");
	zassert_false(dup_seen(&d, &h, 0));
	dup_add(&d, &h, 30 * 60 * 1000, 0);
	zassert_true(dup_seen(&d, &h, 10 * 60 * 1000));
	/* Same alert with locations in another order is still the same alert. */
	zassert_true(dup_seen(&d, hdr("ZCZC-WXR-TOR-048491-048453+0030-2781915-KEWX/NWS-"),
			      10 * 60 * 1000));
}

ZTEST(match_rules, test_any_key_field_makes_it_new)
{
	static const char *const others[] = {
		"ZCZC-CIV-TOR-048453+0030-2781915-KEWX/NWS-", /* originator */
		"ZCZC-WXR-SVR-048453+0030-2781915-KEWX/NWS-", /* event */
		"ZCZC-WXR-TOR-048491+0030-2781915-KEWX/NWS-", /* location */
		"ZCZC-WXR-TOR-048453+0030-2781916-KEWX/NWS-", /* issue time */
		"ZCZC-WXR-TOR-048453+0030-2781915-KFWD/NWS-", /* station */
	};

	dup_init(&d);
	dup_add(&d, hdr("ZCZC-WXR-TOR-048453+0030-2781915-KEWX/NWS-"), 1000000, 0);
	for (size_t i = 0; i < ARRAY_SIZE(others); i++) {
		zassert_false(dup_seen(&d, hdr(others[i]), 1), "%s", others[i]);
	}
	/* Purge time is not part of the key: an extended purge is the same alert. */
	zassert_true(dup_seen(&d, hdr("ZCZC-WXR-TOR-048453+0100-2781915-KEWX/NWS-"), 1));
}

ZTEST(match_rules, test_after_expiry_it_is_new_again)
{
	dup_init(&d);
	hdr("ZCZC-WXR-TOR-048453+0030-2781915-KEWX/NWS-");
	dup_add(&d, &h, 1800000, 0);
	zassert_true(dup_seen(&d, &h, 1799999));
	zassert_false(dup_seen(&d, &h, 1800000), "expired exactly at issue + purge");
}

ZTEST(match_rules, test_duplicates_with_clock_unset)
{
	int64_t expiry;

	dup_init(&d);
	hdr("ZCZC-WXR-TOR-048453+0030-2781915-KEWX/NWS-");
	expiry = same_expiry_ms(&h, -1, 5000);
	zassert_equal(expiry, 5000 + 1800000, "receive time + purge");
	dup_add(&d, &h, expiry, 5000);
	zassert_true(dup_seen(&d, &h, 5000 + 1799999));
	zassert_false(dup_seen(&d, &h, 5000 + 1800000));
}

ZTEST(match_rules, test_full_store_evicts_soonest_expiry)
{
	char text[64];

	dup_init(&d);
	for (int i = 0; i < DUP_STORE_MAX + 1; i++) {
		snprintf(text, sizeof(text), "ZCZC-WXR-TOR-0480%02d+0030-2781915-KEWX/NWS-", i);
		dup_add(&d, hdr(text), 1000000 + i, 0);
	}
	zassert_equal(d.evictions, 1);
	zassert_false(dup_seen(&d, hdr("ZCZC-WXR-TOR-048000+0030-2781915-KEWX/NWS-"), 1),
		      "the soonest-expiring entry went");
	zassert_true(dup_seen(&d, hdr("ZCZC-WXR-TOR-048032+0030-2781915-KEWX/NWS-"), 1));
}

ZTEST(match_rules, test_already_expired_is_not_stored)
{
	dup_init(&d);
	dup_add(&d, hdr("ZCZC-WXR-TOR-048453+0030-2781915-KEWX/NWS-"), 100, 100);
	zassert_false(dup_seen(&d, &h, 100));
}

ZTEST_SUITE(match_rules, NULL, NULL, NULL, NULL, NULL);
