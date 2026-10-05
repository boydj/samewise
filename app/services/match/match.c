/*
 * Location matching. Plain C99.
 */

#include "services/match/match.h"

bool match_location(const struct same_location *alert, const struct same_location *configured)
{
	if (alert->state == 0U) {
		return true;
	}
	if (alert->state != configured->state) {
		return false;
	}
	if (alert->county == 0U || configured->county == 0U) {
		return true;
	}
	if (alert->county != configured->county) {
		return false;
	}
	return alert->subdivision == 0U || configured->subdivision == 0U ||
	       alert->subdivision == configured->subdivision;
}

uint32_t match_header(const struct same_header *h, const struct same_location *configured,
		      uint8_t count)
{
	uint32_t mask = 0;

	for (uint8_t i = 0; i < h->location_count && i < SAME_MAX_LOCATIONS; i++) {
		bool hit = count == 0U;

		for (uint8_t j = 0; j < count && !hit; j++) {
			hit = match_location(&h->locations[i], &configured[j]);
		}
		if (hit) {
			mask |= 1UL << i;
		}
	}
	return mask;
}
