/*
 * SAME issue and purge times. Plain C99, integer arithmetic only.
 *
 * Headers carry a Julian day, hour and minute in UTC but no year. The year
 * is the one that puts the issue time closest to now, so a day 365 or 366
 * header received on January 1 belongs to the previous year. Expiry is
 * expressed in uptime milliseconds so timers and duplicate entries share one
 * time base: issue time plus purge time when UTC is known, otherwise
 * receive time plus purge time.
 */

#ifndef SERVICES_MATCH_SAME_TIME_H_
#define SERVICES_MATCH_SAME_TIME_H_

#include <stdint.h>

#include "services/same/same_header.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Days since 1970-01-01 for a proleptic Gregorian date. */
int64_t same_days_from_civil(int64_t y, unsigned int m, unsigned int d);

/** Calendar year containing a UTC second count. */
int64_t same_year_of(int64_t utc_s);

/** Issue time as UTC seconds, the year inferred from now_utc; -1 if now_utc < 0. */
int64_t same_issue_utc(const struct same_header *h, int64_t now_utc);

/**
 * Issue time as UTC seconds when the clock is unset and only a lower bound
 * is known (the last time the radio knew UTC, or the firmware's epoch):
 * the earliest valid year whose issue time is no more than a day before
 * floor_utc. Time only moves forward while the radio is off.
 */
int64_t same_issue_utc_after(const struct same_header *h, int64_t floor_utc);

/** Purge period +TTTT in seconds. */
int64_t same_purge_s(const struct same_header *h);

/**
 * When the alert expires, in uptime ms. May be <= now_ms: the header was
 * already stale on arrival (only possible when UTC is known).
 *
 * @param now_utc UTC seconds, or -1 if the clock is unset
 */
int64_t same_expiry_ms(const struct same_header *h, int64_t now_utc, int64_t now_ms);

#ifdef __cplusplus
}
#endif

#endif /* SERVICES_MATCH_SAME_TIME_H_ */
