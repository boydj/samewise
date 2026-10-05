/*
 * SAME header parser. Plain C99.
 */

#include <stdbool.h>
#include <string.h>

#include "services/same/same_header.h"

struct cursor {
	const char *p;
	size_t left;
};

static bool take_char(struct cursor *c, char want)
{
	if (c->left == 0U || *c->p != want) {
		return false;
	}
	c->p++;
	c->left--;
	return true;
}

/* n upper-case letters into dst (NUL-terminated). */
static bool take_letters(struct cursor *c, size_t n, char *dst)
{
	if (c->left < n) {
		return false;
	}
	for (size_t i = 0; i < n; i++) {
		if (c->p[i] < 'A' || c->p[i] > 'Z') {
			return false;
		}
		dst[i] = c->p[i];
	}
	dst[n] = '\0';
	c->p += n;
	c->left -= n;
	return true;
}

/* n decimal digits as a number (n <= 7). */
static bool take_digits(struct cursor *c, size_t n, uint32_t *value)
{
	uint32_t v = 0;

	if (c->left < n) {
		return false;
	}
	for (size_t i = 0; i < n; i++) {
		if (c->p[i] < '0' || c->p[i] > '9') {
			return false;
		}
		v = v * 10U + (uint32_t)(c->p[i] - '0');
	}
	*value = v;
	c->p += n;
	c->left -= n;
	return true;
}

int same_parse_header(const char *text, size_t len, struct same_header *out)
{
	struct cursor c = {text, len};
	uint32_t v;
	char sep;

	if (text == NULL || out == NULL || len == 0U || len > SAME_HEADER_MAX_LEN) {
		return SAME_PARSE_ERR_LENGTH;
	}
	if (len < 5U || memcmp(text, "ZCZC-", 5) != 0) {
		return SAME_PARSE_ERR_PREFIX;
	}
	c.p += 5;
	c.left -= 5U;

	if (!take_letters(&c, 3, out->originator) || !take_char(&c, '-')) {
		return SAME_PARSE_ERR_ORIGINATOR;
	}
	if (!take_letters(&c, 3, out->event) || !take_char(&c, '-')) {
		return SAME_PARSE_ERR_EVENT;
	}

	out->location_count = 0;
	do {
		struct same_location *loc;

		if (out->location_count == SAME_MAX_LOCATIONS) {
			return SAME_PARSE_ERR_LOCATION_COUNT;
		}
		if (!take_digits(&c, 6, &v) || c.left == 0U) {
			return SAME_PARSE_ERR_LOCATION;
		}
		sep = *c.p;
		if (sep != '-' && sep != '+') {
			return SAME_PARSE_ERR_LOCATION;
		}
		c.p++;
		c.left--;
		loc = &out->locations[out->location_count++];
		loc->subdivision = (uint8_t)(v / 100000U);
		loc->state = (uint8_t)(v / 1000U % 100U);
		loc->county = (uint16_t)(v % 1000U);
	} while (sep == '-');

	if (!take_digits(&c, 4, &v) || v % 100U >= 60U || !take_char(&c, '-')) {
		return SAME_PARSE_ERR_PURGE;
	}
	out->purge_hours = (uint8_t)(v / 100U);
	out->purge_minutes = (uint8_t)(v % 100U);

	if (!take_digits(&c, 7, &v) || !take_char(&c, '-')) {
		return SAME_PARSE_ERR_ISSUE;
	}
	out->issue_day = (uint16_t)(v / 10000U);
	out->issue_hour = (uint8_t)(v / 100U % 100U);
	out->issue_minute = (uint8_t)(v % 100U);
	if (out->issue_day < 1U || out->issue_day > 366U || out->issue_hour > 23U ||
	    out->issue_minute > 59U) {
		return SAME_PARSE_ERR_ISSUE;
	}

	if (c.left < 8U) {
		return SAME_PARSE_ERR_STATION;
	}
	for (size_t i = 0; i < 8U; i++) {
		char ch = c.p[i];

		/* Printable ASCII other than '-'; chars may be signed. */
		if ((unsigned char)ch < 0x20U || (unsigned char)ch > 0x7EU || ch == '-') {
			return SAME_PARSE_ERR_STATION;
		}
		out->station[i] = ch;
	}
	out->station[8] = '\0';
	c.p += 8;
	c.left -= 8U;
	if (!take_char(&c, '-')) {
		return SAME_PARSE_ERR_STATION;
	}
	if (c.left != 0U) {
		return SAME_PARSE_ERR_LENGTH;
	}

	memcpy(out->raw, text, len);
	out->raw[len] = '\0';
	out->copies = 0;
	return SAME_PARSE_OK;
}

const char *same_parse_strerror(int result)
{
	switch (result) {
	case SAME_PARSE_OK:
		return "ok";
	case SAME_PARSE_ERR_LENGTH:
		return "bad length";
	case SAME_PARSE_ERR_PREFIX:
		return "no ZCZC prefix";
	case SAME_PARSE_ERR_ORIGINATOR:
		return "bad originator";
	case SAME_PARSE_ERR_EVENT:
		return "bad event code";
	case SAME_PARSE_ERR_LOCATION:
		return "bad location code";
	case SAME_PARSE_ERR_LOCATION_COUNT:
		return "too many location codes";
	case SAME_PARSE_ERR_PURGE:
		return "bad purge time";
	case SAME_PARSE_ERR_ISSUE:
		return "bad issue time";
	case SAME_PARSE_ERR_STATION:
		return "bad station ID";
	default:
		return "unknown error";
	}
}
