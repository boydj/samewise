/*
 * POSIX TZ parser and UTC-to-local conversion. See tz.h.
 */

#include "services/ble/tz.h"

#include <stddef.h>
#include <string.h>

#include "services/match/same_time.h"

#define HOUR_S 3600
#define DAY_S  86400

static bool is_alpha(char c)
{
	return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

static bool is_digit(char c)
{
	return c >= '0' && c <= '9';
}

/* Reads 1 to max_digits digits; false if none. */
static bool number(const char **p, int max_digits, int32_t *v)
{
	int n = 0;

	*v = 0;
	while (n < max_digits && is_digit(**p)) {
		*v = *v * 10 + (**p - '0');
		(*p)++;
		n++;
	}
	return n > 0 && !is_digit(**p);
}

static bool name(const char **p, char *out)
{
	size_t n = 0;
	bool quoted = **p == '<';

	if (quoted) {
		(*p)++;
	}
	while (quoted ? (is_alpha(**p) || is_digit(**p) || **p == '+' || **p == '-')
		      : is_alpha(**p)) {
		if (n == TZ_NAME_MAX) {
			return false;
		}
		out[n++] = *(*p)++;
	}
	out[n] = '\0';
	if (quoted) {
		if (**p != '>') {
			return false;
		}
		(*p)++;
	}
	return n >= TZ_NAME_MIN;
}

/* [+|-]hh[:mm[:ss]] with hh up to max_h. */
static bool hms(const char **p, int32_t max_h, int32_t *out)
{
	int32_t sign = 1, h, m = 0, s = 0;

	if (**p == '+' || **p == '-') {
		sign = **p == '-' ? -1 : 1;
		(*p)++;
	}
	if (!number(p, 3, &h) || h > max_h) {
		return false;
	}
	if (**p == ':') {
		(*p)++;
		if (!number(p, 2, &m) || m > 59) {
			return false;
		}
		if (**p == ':') {
			(*p)++;
			if (!number(p, 2, &s) || s > 59) {
				return false;
			}
		}
	}
	*out = sign * (h * HOUR_S + m * 60 + s);
	return true;
}

static bool rule(const char **p, struct tz_rule *r)
{
	int32_t a, b, c;

	memset(r, 0, sizeof(*r));
	if (**p == 'M') {
		(*p)++;
		if (!number(p, 2, &a) || **p != '.') {
			return false;
		}
		(*p)++;
		if (!number(p, 1, &b) || **p != '.') {
			return false;
		}
		(*p)++;
		if (!number(p, 1, &c)) {
			return false;
		}
		if (a < 1 || a > 12 || b < 1 || b > 5 || c > 6) {
			return false;
		}
		r->kind = TZ_RULE_MONTH;
		r->month = (uint8_t)a;
		r->week = (uint8_t)b;
		r->wday = (uint8_t)c;
	} else if (**p == 'J') {
		(*p)++;
		if (!number(p, 3, &a) || a < 1 || a > 365) {
			return false;
		}
		r->kind = TZ_RULE_JULIAN;
		r->day = (uint16_t)a;
	} else {
		if (!number(p, 3, &a) || a > 365) {
			return false;
		}
		r->kind = TZ_RULE_ZERO_DAY;
		r->day = (uint16_t)a;
	}
	r->time_s = 2 * HOUR_S;
	if (**p == '/') {
		(*p)++;
		return hms(p, 167, &r->time_s);
	}
	return true;
}

int tz_parse(const char *s, struct tz_info *out)
{
	struct tz_info t;
	int32_t off;

	if (s == NULL) {
		return -1;
	}
	memset(&t, 0, sizeof(t));
	if (!name(&s, t.std_name) || !hms(&s, 24, &off)) {
		return -1;
	}
	t.std_offset_s = -off;
	if (*s != '\0') {
		if (!name(&s, t.dst_name)) {
			return -1;
		}
		t.has_dst = true;
		t.dst_offset_s = t.std_offset_s + HOUR_S;
		if (*s != ',') {
			if (!hms(&s, 24, &off)) {
				return -1;
			}
			t.dst_offset_s = -off;
		}
		if (*s++ != ',' || !rule(&s, &t.start) || *s++ != ',' || !rule(&s, &t.end)) {
			return -1;
		}
		if (*s != '\0') {
			return -1;
		}
	}
	*out = t;
	return 0;
}

static bool leap(int64_t y)
{
	return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
}

static unsigned int month_days(int64_t y, unsigned int m)
{
	static const uint8_t days[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

	return m == 2 && leap(y) ? 29U : days[m - 1];
}

/* Seconds from the epoch to the local midnight starting the rule's day in year y. */
static int64_t rule_day(const struct tz_rule *r, int64_t y)
{
	int64_t jan1 = same_days_from_civil(y, 1, 1);

	switch (r->kind) {
	case TZ_RULE_JULIAN:
		return (jan1 + r->day - 1 + (leap(y) && r->day >= 60 ? 1 : 0)) * DAY_S;
	case TZ_RULE_ZERO_DAY:
		return (jan1 + r->day) * DAY_S;
	default: {
		int64_t first = same_days_from_civil(y, r->month, 1);
		/* 1970-01-01 was a Thursday (4). */
		int64_t wday1 = ((first % 7) + 7 + 4) % 7;
		int64_t d = (r->wday - wday1 + 7) % 7 + 7 * (r->week - 1);

		if (d >= month_days(y, r->month)) {
			d -= 7;
		}
		return (first + d) * DAY_S;
	}
	}
}

int64_t tz_local(const struct tz_info *tz, int64_t utc, bool *dst)
{
	bool in_dst = false;

	if (tz->has_dst) {
		int64_t y = same_year_of(utc + tz->std_offset_s);
		int64_t start = rule_day(&tz->start, y) + tz->start.time_s - tz->std_offset_s;
		int64_t end = rule_day(&tz->end, y) + tz->end.time_s - tz->dst_offset_s;

		in_dst = start < end ? (utc >= start && utc < end) : !(utc >= end && utc < start);
	}
	if (dst != NULL) {
		*dst = in_dst;
	}
	return utc + (in_dst ? tz->dst_offset_s : tz->std_offset_s);
}
