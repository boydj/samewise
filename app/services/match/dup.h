/*
 * Duplicate suppression (spec: SAME decoding step 8). Plain C99, no heap.
 *
 * Key: originator, event, locations (sorted), issue time and station. An
 * entry lasts until the alert's expiry (same_time.h). When the store is full
 * the entry closest to expiry is evicted and counted.
 */

#ifndef SERVICES_MATCH_DUP_H_
#define SERVICES_MATCH_DUP_H_

#include <stdbool.h>
#include <stdint.h>

#include "services/same/same_header.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DUP_STORE_MAX 32

struct dup_key {
	char originator[4];
	char event[4];
	uint8_t location_count;
	struct same_location locations[SAME_MAX_LOCATIONS]; /* sorted */
	uint16_t issue_day;
	uint8_t issue_hour;
	uint8_t issue_minute;
	char station[9];
};

struct dup_entry {
	bool used;
	int64_t expires_ms;
	struct dup_key key;
};

struct dup_store {
	struct dup_entry entries[DUP_STORE_MAX];
	uint32_t evictions;
};

void dup_init(struct dup_store *d);

/** True if h matches an entry that has not expired by now_ms. */
bool dup_seen(struct dup_store *d, const struct same_header *h, int64_t now_ms);

/** Remember h until expires_ms (ignored if already expired). */
void dup_add(struct dup_store *d, const struct same_header *h, int64_t expires_ms, int64_t now_ms);

#ifdef __cplusplus
}
#endif

#endif /* SERVICES_MATCH_DUP_H_ */
