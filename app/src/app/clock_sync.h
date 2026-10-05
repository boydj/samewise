/*
 * Clock correction from weekly tests (spec: Alert behaviour).
 *
 * Only RWT headers adjust the clock, and only when it is unset or more than
 * CLOCK_SYNC_MAX_ERROR_S from the RWT's issue time; other headers never
 * touch it (a re-broadcast alert can carry an issue time hours old). A set
 * clock is never moved by more than CLOCK_SYNC_MAX_JUMP_S: an inferred year
 * that far off (day 366 in a non-leap year) would make every later alert
 * look expired.
 *
 * Headers carry no year. With the clock unset, the year comes from the last
 * UTC the radio stored (or the firmware's epoch): the first year whose issue
 * time is not before it. The stored time is refreshed on every set and
 * daily, written by clock_sync_flush() at low priority, never on the alert
 * path.
 */

#ifndef APP_CLOCK_SYNC_H_
#define APP_CLOCK_SYNC_H_

#include <stdint.h>

#include "services/same/same_header.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CLOCK_SYNC_MAX_ERROR_S 300

enum clock_sync_result {
	CLOCK_SYNC_KEPT,      /* within the tolerance: left alone */
	CLOCK_SYNC_SET,       /* was unset */
	CLOCK_SYNC_CORRECTED, /* was off by more than the tolerance */
	CLOCK_SYNC_NO_YEAR,   /* unset and no usable year floor */
	CLOCK_SYNC_REJECTED,  /* would move a set clock more than CLOCK_SYNC_MAX_JUMP_S */
};

/** Largest correction an RWT may make to a set clock (larger = wrong year). */
#define CLOCK_SYNC_MAX_JUMP_S (24 * 3600)

/** Load the stored floor; firmware_epoch_utc is used if none is stored. */
void clock_sync_init(int64_t firmware_epoch_utc);

/** Apply an RWT header. Call only for RWT. */
enum clock_sync_result clock_sync_on_rwt(const struct same_header *h);

/** RWTs rejected because they would have jumped the clock, since init. */
uint32_t clock_sync_rejected(void);

/** Persist the current UTC as the year floor if it changed. Low priority. */
int clock_sync_flush(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_CLOCK_SYNC_H_ */
