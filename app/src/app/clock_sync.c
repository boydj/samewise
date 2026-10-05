/*
 * Clock correction from weekly tests.
 */

#include <stdbool.h>

#include "app/clock_sync.h"
#include "hal/clock.h"
#include "hal/storage.h"
#include "services/match/same_time.h"

#define KEY_LAST  "clock/last"
#define DAY_MS    (24U * 3600U * 1000U)

static struct {
	int64_t floor_utc;
	bool dirty;
	uint32_t rejected;
	struct hal_clock_timer daily;
} s;

static void daily(struct hal_clock_timer *t, void *user)
{
	(void)t;
	(void)user;
	if (hal_clock_utc_s() >= 0) {
		s.dirty = true;
	}
}

void clock_sync_init(int64_t firmware_epoch_utc)
{
	int64_t stored = -1;

	s.floor_utc = firmware_epoch_utc;
	s.dirty = false;
	if (hal_storage_read(KEY_LAST, &stored, sizeof(stored)) == (int)sizeof(stored) &&
	    stored > s.floor_utc) {
		s.floor_utc = stored;
	}
	s.rejected = 0;
	hal_clock_timer_init(&s.daily, daily, NULL);
	(void)hal_clock_timer_start(&s.daily, DAY_MS, DAY_MS);
}

enum clock_sync_result clock_sync_on_rwt(const struct same_header *h)
{
	int64_t now = hal_clock_utc_s();
	int64_t issue, err;

	if (now < 0) {
		issue = same_issue_utc_after(h, s.floor_utc);
		if (issue < 0 || hal_clock_set_utc(issue) != 0) {
			return CLOCK_SYNC_NO_YEAR;
		}
		s.dirty = true;
		return CLOCK_SYNC_SET;
	}
	issue = same_issue_utc(h, now);
	err = now - issue;
	if (err <= CLOCK_SYNC_MAX_ERROR_S && err >= -CLOCK_SYNC_MAX_ERROR_S) {
		return CLOCK_SYNC_KEPT;
	}
	if (err > CLOCK_SYNC_MAX_JUMP_S || err < -CLOCK_SYNC_MAX_JUMP_S) {
		/* Wrong year (day 366 in a non-leap year): believe the clock. */
		s.rejected++;
		return CLOCK_SYNC_REJECTED;
	}
	if (hal_clock_set_utc(issue) != 0) {
		return CLOCK_SYNC_NO_YEAR;
	}
	s.dirty = true;
	return CLOCK_SYNC_CORRECTED;
}

uint32_t clock_sync_rejected(void)
{
	return s.rejected;
}

int clock_sync_flush(void)
{
	int64_t now = hal_clock_utc_s();
	int err;

	if (!s.dirty || now < 0) {
		return 0;
	}
	err = hal_storage_write(KEY_LAST, &now, sizeof(now));
	if (err == 0) {
		s.dirty = false;
		s.floor_utc = now;
	}
	return err;
}
