/*
 * SAME issue and purge times. Plain C99.
 */

#include "services/match/same_time.h"

#define SECONDS_PER_DAY 86400

/* Howard Hinnant's days_from_civil, valid far beyond any SAME header. */
int64_t same_days_from_civil(int64_t y, unsigned int m, unsigned int d)
{
	int64_t era;
	int64_t yoe, doy, doe;

	y -= m <= 2U ? 1 : 0;
	era = (y >= 0 ? y : y - 399) / 400;
	yoe = y - era * 400;
	doy = (153 * (int64_t)(m > 2U ? m - 3U : m + 9U) + 2) / 5 + (int64_t)d - 1;
	doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
	return era * 146097 + doe - 719468;
}

int64_t same_year_of(int64_t utc_s)
{
	int64_t z = (utc_s >= 0 ? utc_s : utc_s - (SECONDS_PER_DAY - 1)) / SECONDS_PER_DAY + 719468;
	int64_t era = (z >= 0 ? z : z - 146096) / 146097;
	int64_t doe = z - era * 146097;
	int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
	int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
	int64_t mp = (5 * doy + 2) / 153;

	return yoe + era * 400 + (mp >= 10 ? 1 : 0);
}

static int days_in_year(int64_t y)
{
	return ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0) ? 366 : 365;
}

int64_t same_issue_utc(const struct same_header *h, int64_t now_utc)
{
	int64_t year, best = -1, best_dist = 0;

	if (now_utc < 0) {
		return -1;
	}
	year = same_year_of(now_utc);
	for (int64_t y = year - 1; y <= year + 1; y++) {
		int64_t t, dist;

		if (h->issue_day < 1U || h->issue_day > (unsigned int)days_in_year(y)) {
			continue; /* day 366 only exists in leap years */
		}
		t = (same_days_from_civil(y, 1, 1) + h->issue_day - 1) * SECONDS_PER_DAY +
		    h->issue_hour * 3600 + h->issue_minute * 60;
		dist = t > now_utc ? t - now_utc : now_utc - t;
		if (best < 0 || dist < best_dist) {
			best = t;
			best_dist = dist;
		}
	}
	return best;
}

int64_t same_issue_utc_after(const struct same_header *h, int64_t floor_utc)
{
	int64_t year = same_year_of(floor_utc);

	for (int64_t y = year - 1; y <= year + 4; y++) {
		int64_t t;

		if (h->issue_day < 1U || h->issue_day > (unsigned int)days_in_year(y)) {
			continue;
		}
		t = (same_days_from_civil(y, 1, 1) + h->issue_day - 1) * SECONDS_PER_DAY +
		    h->issue_hour * 3600 + h->issue_minute * 60;
		if (t >= floor_utc - SECONDS_PER_DAY) {
			return t;
		}
	}
	return -1;
}

bool same_issue_in_future(const struct same_header *h, int64_t now_utc)
{
	int64_t issue = same_issue_utc(h, now_utc);

	return issue >= 0 && issue - now_utc > SAME_FUTURE_TOLERANCE_S;
}

int64_t same_purge_s(const struct same_header *h)
{
	return (int64_t)h->purge_hours * 3600 + (int64_t)h->purge_minutes * 60;
}

int64_t same_expiry_ms(const struct same_header *h, int64_t now_utc, int64_t now_ms)
{
	int64_t issue = same_issue_utc(h, now_utc);

	/* Clock unset, or an issue time too far ahead to trust: from receipt. */
	if (issue < 0 || issue - now_utc > SAME_FUTURE_TOLERANCE_S) {
		return now_ms + same_purge_s(h) * 1000;
	}
	return now_ms + (issue + same_purge_s(h) - now_utc) * 1000;
}
