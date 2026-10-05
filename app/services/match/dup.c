/*
 * Duplicate suppression. Plain C99.
 */

#include <string.h>

#include "services/match/dup.h"

static int loc_cmp(const struct same_location *a, const struct same_location *b)
{
	if (a->state != b->state) {
		return a->state < b->state ? -1 : 1;
	}
	if (a->county != b->county) {
		return a->county < b->county ? -1 : 1;
	}
	if (a->subdivision != b->subdivision) {
		return a->subdivision < b->subdivision ? -1 : 1;
	}
	return 0;
}

static void make_key(const struct same_header *h, struct dup_key *k)
{
	memset(k, 0, sizeof(*k));
	memcpy(k->originator, h->originator, 4);
	memcpy(k->event, h->event, 4);
	memcpy(k->station, h->station, 9);
	k->issue_day = h->issue_day;
	k->issue_hour = h->issue_hour;
	k->issue_minute = h->issue_minute;
	k->location_count = h->location_count;
	memcpy(k->locations, h->locations, h->location_count * sizeof(h->locations[0]));
	/* Insertion sort: at most 31 entries. */
	for (uint8_t i = 1; i < k->location_count; i++) {
		struct same_location v = k->locations[i];
		uint8_t j = i;

		while (j > 0U && loc_cmp(&k->locations[j - 1U], &v) > 0) {
			k->locations[j] = k->locations[j - 1U];
			j--;
		}
		k->locations[j] = v;
	}
}

void dup_init(struct dup_store *d)
{
	memset(d, 0, sizeof(*d));
}

bool dup_seen(struct dup_store *d, const struct same_header *h, int64_t now_ms)
{
	struct dup_key k;

	make_key(h, &k);
	for (int i = 0; i < DUP_STORE_MAX; i++) {
		struct dup_entry *e = &d->entries[i];

		if (e->used && e->expires_ms <= now_ms) {
			e->used = false; /* expired: forget it */
		}
		if (e->used && memcmp(&e->key, &k, sizeof(k)) == 0) {
			return true;
		}
	}
	return false;
}

void dup_add(struct dup_store *d, const struct same_header *h, int64_t expires_ms, int64_t now_ms)
{
	struct dup_entry *slot = NULL;

	if (expires_ms <= now_ms) {
		return;
	}
	for (int i = 0; i < DUP_STORE_MAX; i++) {
		struct dup_entry *e = &d->entries[i];

		if (!e->used || e->expires_ms <= now_ms) {
			slot = e;
			break;
		}
		if (slot == NULL || e->expires_ms < slot->expires_ms) {
			slot = e;
		}
	}
	if (slot->used && slot->expires_ms > now_ms) {
		d->evictions++;
	}
	slot->used = true;
	slot->expires_ms = expires_ms;
	make_key(h, &slot->key);
}
