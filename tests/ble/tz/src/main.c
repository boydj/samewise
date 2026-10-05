/*
 * POSIX TZ strings: parsing, and local time either side of each 2026
 * transition. Expected values from the IANA database (America/New_York,
 * Australia/Sydney).
 */

#include <zephyr/ztest.h>

#include "services/ble/tz.h"

#define H 3600

static struct tz_info tz;

static void assert_local(int64_t utc, int32_t offset_s, bool dst)
{
	bool is_dst = !dst;

	zassert_equal(tz_local(&tz, utc, &is_dst), utc + offset_s, "utc %lld",
		      (long long)utc);
	zassert_equal(is_dst, dst, "utc %lld dst", (long long)utc);
}

ZTEST(tz, test_us_eastern_2026)
{
	zassert_ok(tz_parse("EST5EDT,M3.2.0,M11.1.0", &tz));
	zassert_str_equal(tz.std_name, "EST");
	zassert_str_equal(tz.dst_name, "EDT");
	zassert_equal(tz.std_offset_s, -5 * H);
	zassert_equal(tz.dst_offset_s, -4 * H, "DST defaults to an hour ahead");

	/* 8 March 2026, 02:00 EST -> 03:00 EDT */
	assert_local(1772953199, -5 * H, false);
	assert_local(1772953200, -4 * H, true);
	/* 1 November 2026, 02:00 EDT -> 01:00 EST */
	assert_local(1793512799, -4 * H, true);
	assert_local(1793512800, -5 * H, false);
	/* Mid-winter and mid-summer */
	assert_local(1767225600, -5 * H, false); /* 2026-01-01 00:00 UTC */
	assert_local(1783036800, -4 * H, true);  /* 2026-07-03 00:00 UTC */
}

ZTEST(tz, test_utc)
{
	zassert_ok(tz_parse("UTC0", &tz));
	zassert_false(tz.has_dst);
	assert_local(1772953200, 0, false);
	assert_local(0, 0, false);
}

ZTEST(tz, test_sydney_2026)
{
	zassert_ok(tz_parse("AEST-10AEDT,M10.1.0,M4.1.0/3", &tz));
	zassert_equal(tz.std_offset_s, 10 * H);
	zassert_equal(tz.dst_offset_s, 11 * H);

	/* 5 April 2026, 03:00 AEDT -> 02:00 AEST */
	assert_local(1775318399, 11 * H, true);
	assert_local(1775318400, 10 * H, false);
	/* 4 October 2026, 02:00 AEST -> 03:00 AEDT */
	assert_local(1791043199, 10 * H, false);
	assert_local(1791043200, 11 * H, true);
	/* Southern summer spans the new year */
	assert_local(1767225600, 11 * H, true);
	assert_local(1783036800, 10 * H, false);
}

ZTEST(tz, test_other_forms)
{
	/* Quoted names, explicit DST offset, Julian days, minutes in offsets. */
	zassert_ok(tz_parse("<+0530>-5:30", &tz));
	zassert_str_equal(tz.std_name, "+0530");
	zassert_equal(tz.std_offset_s, 5 * H + 30 * 60);

	zassert_ok(tz_parse("NST3:30NDT,M3.2.0,M11.1.0", &tz));
	zassert_equal(tz.std_offset_s, -(3 * H + 30 * 60));

	/* J60 is 1 March even in a leap year; DST from 1 March to 1 November. */
	zassert_ok(tz_parse("XST8XDT7,J60/0,J305/0", &tz));
	zassert_equal(tz.dst_offset_s, -7 * H);
	assert_local(1772323200 + 8 * H - 1, -8 * H, false); /* 2026-03-01 00:00 local - 1 s */
	assert_local(1772323200 + 8 * H, -7 * H, true);

	/* Zero-based day 59 is 29 February in 2028 */
	zassert_ok(tz_parse("XST8XDT,59/0,304/0", &tz));
	assert_local(1835395200 + 8 * H, -7 * H, true); /* 2028-02-29 00:00 local */
	assert_local(1835395200 + 8 * H - 1, -8 * H, false);

	/* Fifth week means the last such weekday; negative and >24 h rule times. */
	zassert_ok(tz_parse("XST0XDT,M3.5.0/-1,M10.5.0/26", &tz));
	/* Last Sunday of March 2026 is the 29th: 23:00 on the 28th local. */
	assert_local(1774738800 - 1, 0, false);
	assert_local(1774738800, H, true);
}

ZTEST(tz, test_malformed_rejected)
{
	static const char *const bad[] = {
		"",
		"EST",                       /* no offset */
		"ES5",                       /* name too short */
		"EST5EDT",                   /* DST without rules */
		"EST5EDT,M3.2.0",            /* one rule */
		"EST5EDT,M3.2.0,M11.1.0,",   /* trailing comma */
		"EST5EDT,M13.2.0,M11.1.0",   /* month 13 */
		"EST5EDT,M3.6.0,M11.1.0",    /* week 6 */
		"EST5EDT,M3.2.7,M11.1.0",    /* weekday 7 */
		"EST5EDT,M3.0.0,M11.1.0",    /* week 0 */
		"EST5EDT,J0,J100",           /* Julian day 0 */
		"EST5EDT,J366,J100",         /* Julian day 366 */
		"EST5EDT,366,100",           /* zero-based day 366 */
		"EST25",                     /* offset over 24 h */
		"EST5:60",                   /* minutes 60 */
		"EST5:",                     /* missing minutes */
		"EST5EDT,M3.2.0/168,M11.1.0",/* rule time over 167 h */
		"<EST5",                     /* unterminated quote */
		"<E>5",                      /* quoted name too short */
		"EST5 ",                     /* trailing space */
		"EST5EDT,M3.2.0,M11.1.0x",   /* trailing junk */
		"ABCDEFGHIJKLMNOP5",         /* name too long */
	};

	for (size_t i = 0; i < ARRAY_SIZE(bad); i++) {
		tz.std_offset_s = 1234;
		zassert_not_ok(tz_parse(bad[i], &tz), "accepted \"%s\"", bad[i]);
		zassert_equal(tz.std_offset_s, 1234, "touched output for \"%s\"", bad[i]);
	}
	zassert_not_ok(tz_parse(NULL, &tz));
}

ZTEST_SUITE(tz, NULL, NULL, NULL, NULL, NULL);
