/*
 * SAME header parser: fields, 31 locations, strict rejection of malformed
 * headers.
 */

#include <string.h>
#include <zephyr/ztest.h>

#include "services/same/same_header.h"

static struct same_header h;

static int parse(const char *s)
{
	memset(&h, 0xA5, sizeof(h));
	return same_parse_header(s, strlen(s), &h);
}

#define TOR "ZCZC-WXR-TOR-048453+0030-2781915-KEWX/NWS-"

ZTEST(same_parser, test_fields)
{
	zassert_equal(parse(TOR), SAME_PARSE_OK);
	zassert_str_equal(h.originator, "WXR");
	zassert_str_equal(h.event, "TOR");
	zassert_equal(h.location_count, 1);
	zassert_equal(h.locations[0].subdivision, 0);
	zassert_equal(h.locations[0].state, 48);
	zassert_equal(h.locations[0].county, 453);
	zassert_equal(h.purge_hours, 0);
	zassert_equal(h.purge_minutes, 30);
	zassert_equal(h.issue_day, 278);
	zassert_equal(h.issue_hour, 19);
	zassert_equal(h.issue_minute, 15);
	zassert_str_equal(h.station, "KEWX/NWS");
	zassert_str_equal(h.raw, TOR);
	zassert_equal(h.copies, 0);
}

ZTEST(same_parser, test_subdivision_whole_state_and_station_spaces)
{
	zassert_equal(parse("ZCZC-EAS-RWT-948000-000000+0600-3662359-WXK27   -"), SAME_PARSE_OK);
	zassert_str_equal(h.originator, "EAS");
	zassert_equal(h.location_count, 2);
	zassert_equal(h.locations[0].subdivision, 9);
	zassert_equal(h.locations[0].state, 48);
	zassert_equal(h.locations[0].county, 0);
	zassert_equal(h.locations[1].state, 0);
	zassert_equal(h.purge_hours, 6);
	zassert_equal(h.issue_day, 366);
	zassert_equal(h.issue_hour, 23);
	zassert_equal(h.issue_minute, 59);
	zassert_str_equal(h.station, "WXK27   ");
}

ZTEST(same_parser, test_31_locations)
{
	char buf[SAME_HEADER_MAX_LEN + 1];
	size_t n;

	strcpy(buf, "ZCZC-CIV-CAE-");
	for (int i = 0; i < SAME_MAX_LOCATIONS; i++) {
		n = strlen(buf);
		/* P = i % 10, SS = i + 1, CCC = 3 * i */
		buf[n] = (char)('0' + i % 10);
		buf[n + 1] = (char)('0' + (i + 1) / 10);
		buf[n + 2] = (char)('0' + (i + 1) % 10);
		buf[n + 3] = (char)('0' + (3 * i) / 100);
		buf[n + 4] = (char)('0' + (3 * i) / 10 % 10);
		buf[n + 5] = (char)('0' + (3 * i) % 10);
		buf[n + 6] = i == SAME_MAX_LOCATIONS - 1 ? '+' : '-';
		buf[n + 7] = '\0';
	}
	strcat(buf, "0100-2790105-KFWD/NWS-");
	zassert_equal(strlen(buf), SAME_HEADER_MAX_LEN, "longest header");

	zassert_equal(parse(buf), SAME_PARSE_OK);
	zassert_equal(h.location_count, 31);
	for (int i = 0; i < SAME_MAX_LOCATIONS; i++) {
		zassert_equal(h.locations[i].subdivision, i % 10, "loc %d", i);
		zassert_equal(h.locations[i].state, i + 1, "loc %d", i);
		zassert_equal(h.locations[i].county, 3 * i, "loc %d", i);
	}
	zassert_str_equal(h.raw, buf);
}

ZTEST(same_parser, test_32_locations_rejected)
{
	char buf[300] = "ZCZC-WXR-TOR-";

	for (int i = 0; i < 32; i++) {
		strcat(buf, i == 31 ? "048453+" : "048453-");
	}
	strcat(buf, "0030-2781915-KEWX/NWS-");
	zassert_equal(parse(buf), SAME_PARSE_ERR_LENGTH);
	/* Same count check without the length limit hitting first. */
	zassert_equal(same_parse_header(buf, SAME_HEADER_MAX_LEN, &h), SAME_PARSE_ERR_LOCATION_COUNT);
}

struct bad {
	const char *text;
	int err;
};

static const struct bad bad_headers[] = {
	{"", SAME_PARSE_ERR_LENGTH},
	{"NNNN", SAME_PARSE_ERR_PREFIX},
	{"ZCZD-WXR-TOR-048453+0030-2781915-KEWX/NWS-", SAME_PARSE_ERR_PREFIX},
	{"ZCZC-wxr-TOR-048453+0030-2781915-KEWX/NWS-", SAME_PARSE_ERR_ORIGINATOR},
	{"ZCZC-W1R-TOR-048453+0030-2781915-KEWX/NWS-", SAME_PARSE_ERR_ORIGINATOR},
	{"ZCZC-WX-TOR-048453+0030-2781915-KEWX/NWS-", SAME_PARSE_ERR_ORIGINATOR},
	{"ZCZC-WXRR-TOR-048453+0030-2781915-KEWX/NWS-", SAME_PARSE_ERR_ORIGINATOR},
	/* 47 CFR 11.31(d)(1): the only originator codes are EAS, CIV, WXR and PEP. */
	{"ZCZC-XYZ-TOR-048453+0030-2781915-KEWX/NWS-", SAME_PARSE_ERR_ORIGINATOR},
	{"ZCZC-EAN-EAN-048453+0030-2781915-KEWX/NWS-", SAME_PARSE_ERR_ORIGINATOR},
	{"ZCZC-WXR-T0R-048453+0030-2781915-KEWX/NWS-", SAME_PARSE_ERR_EVENT},
	{"ZCZC-WXR-TO-048453+0030-2781915-KEWX/NWS-", SAME_PARSE_ERR_EVENT},
	{"ZCZC-WXR-TORR-048453+0030-2781915-KEWX/NWS-", SAME_PARSE_ERR_EVENT},
	{"ZCZC-WXR-TOR-04845+0030-2781915-KEWX/NWS-", SAME_PARSE_ERR_LOCATION},
	{"ZCZC-WXR-TOR-0484531+0030-2781915-KEWX/NWS-", SAME_PARSE_ERR_LOCATION},
	{"ZCZC-WXR-TOR-04845A+0030-2781915-KEWX/NWS-", SAME_PARSE_ERR_LOCATION},
	{"ZCZC-WXR-TOR-048453-04845+0030-2781915-KEWX/NWS-", SAME_PARSE_ERR_LOCATION},
	{"ZCZC-WXR-TOR-048453--0030-2781915-KEWX/NWS-", SAME_PARSE_ERR_LOCATION},
	{"ZCZC-WXR-TOR-+0030-2781915-KEWX/NWS-", SAME_PARSE_ERR_LOCATION},
	{"ZCZC-WXR-TOR-048453+00A0-2781915-KEWX/NWS-", SAME_PARSE_ERR_PURGE},
	{"ZCZC-WXR-TOR-048453+0060-2781915-KEWX/NWS-", SAME_PARSE_ERR_PURGE},
	{"ZCZC-WXR-TOR-048453+030-2781915-KEWX/NWS-", SAME_PARSE_ERR_PURGE},
	{"ZCZC-WXR-TOR-048453+00300-2781915-KEWX/NWS-", SAME_PARSE_ERR_PURGE},
	{"ZCZC-WXR-TOR-048453+0030-278191-KEWX/NWS-", SAME_PARSE_ERR_ISSUE},
	{"ZCZC-WXR-TOR-048453+0030-27819155-KEWX/NWS-", SAME_PARSE_ERR_ISSUE},
	{"ZCZC-WXR-TOR-048453+0030-27819X5-KEWX/NWS-", SAME_PARSE_ERR_ISSUE},
	{"ZCZC-WXR-TOR-048453+0030-0001915-KEWX/NWS-", SAME_PARSE_ERR_ISSUE},
	{"ZCZC-WXR-TOR-048453+0030-3671915-KEWX/NWS-", SAME_PARSE_ERR_ISSUE},
	{"ZCZC-WXR-TOR-048453+0030-2782415-KEWX/NWS-", SAME_PARSE_ERR_ISSUE},
	{"ZCZC-WXR-TOR-048453+0030-2781960-KEWX/NWS-", SAME_PARSE_ERR_ISSUE},
	{"ZCZC-WXR-TOR-048453+0030-2781915-KEWX/NW-", SAME_PARSE_ERR_STATION},
	{"ZCZC-WXR-TOR-048453+0030-2781915-KEWX-NWS-", SAME_PARSE_ERR_STATION},
	{"ZCZC-WXR-TOR-048453+0030-2781915-KEWX/NWS", SAME_PARSE_ERR_STATION},
	/* 11.31(b): '+' and '-' may not be used for any other purpose. */
	{"ZCZC-WXR-TOR-048453+0030-2781915-KEWX+NWS-", SAME_PARSE_ERR_STATION},
	{"ZCZC-WXR-TOR-048453+0030-2781915-KEWX\x01NWS-", SAME_PARSE_ERR_STATION},
	{"ZCZC-WXR-TOR-048453+0030-2781915-KEWX\xC6NWS-", SAME_PARSE_ERR_STATION},
	{"ZCZC-WXR-TOR-048453+0030-2781915-KEWX/NWS-X", SAME_PARSE_ERR_LENGTH},
	{"ZCZC-WXR-TOR-048453+0030-2781915-KEWX/NWS--", SAME_PARSE_ERR_LENGTH},
};

ZTEST(same_parser, test_malformed_rejected)
{
	for (size_t i = 0; i < ARRAY_SIZE(bad_headers); i++) {
		int err = parse(bad_headers[i].text);

		zassert_equal(err, bad_headers[i].err, "\"%s\": got %d (%s), want %d",
			      bad_headers[i].text, err, same_parse_strerror(err), bad_headers[i].err);
	}
}

ZTEST(same_parser, test_every_originator_code)
{
	static const char *const orgs[] = {"EAS", "CIV", "WXR", "PEP"};
	char buf[] = "ZCZC-XXX-RWT-048000+0015-2791700-KEWX/NWS-";

	for (size_t i = 0; i < ARRAY_SIZE(orgs); i++) {
		memcpy(buf + 5, orgs[i], 3);
		zassert_equal(parse(buf), SAME_PARSE_OK, "%s", orgs[i]);
		zassert_str_equal(h.originator, orgs[i]);
	}
}

ZTEST(same_parser, test_embedded_nul_rejected)
{
	static const char text[] = "ZCZC-WXR-TOR-048453+0030-2781915-KEWX\0NWS-";

	zassert_equal(same_parse_header(text, sizeof(text) - 1, &h), SAME_PARSE_ERR_STATION);
}

ZTEST(same_parser, test_purge_hours_not_restricted_to_fcc_steps)
{
	/* Digits and minutes < 60 only: nonstandard increments still alert. */
	zassert_equal(parse("ZCZC-WXR-TOR-048453+0020-2781915-KEWX/NWS-"), SAME_PARSE_OK);
	zassert_equal(parse("ZCZC-WXR-TOR-048453+9959-2781915-KEWX/NWS-"), SAME_PARSE_OK);
	zassert_equal(h.purge_hours, 99);
	zassert_equal(h.purge_minutes, 59);
}

ZTEST(same_parser, test_unterminated_input_is_bounded_by_len)
{
	/* The parser must not read past len, even mid-field. */
	static const char text[] = TOR;

	for (size_t len = 0; len < sizeof(text) - 1; len++) {
		zassert_not_equal(same_parse_header(text, len, &h), SAME_PARSE_OK, "len %zu", len);
	}
	zassert_equal(same_parse_header(text, sizeof(text) - 1, &h), SAME_PARSE_OK);
}

ZTEST(same_parser, test_strerror)
{
	zassert_str_equal(same_parse_strerror(SAME_PARSE_OK), "ok");
	zassert_not_null(same_parse_strerror(SAME_PARSE_ERR_STATION));
	zassert_not_null(same_parse_strerror(-100));
}

ZTEST_SUITE(same_parser, NULL, NULL, NULL, NULL, NULL);
