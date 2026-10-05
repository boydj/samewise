/*
 * Location matching (spec: SAME decoding step 6). Plain C99.
 *
 * An alert location A matches a configured code C when:
 *   - A's state is 00 (all of the U.S.); or
 *   - the states are equal and either county is 000 (a whole state); or
 *   - the states and counties are equal and either subdivision is 0
 *     (the whole county), or both subdivisions are equal.
 * An empty configured list accepts every location (an unconfigured radio,
 * or travel mode with no travel counties).
 */

#ifndef SERVICES_MATCH_MATCH_H_
#define SERVICES_MATCH_MATCH_H_

#include <stdbool.h>
#include <stdint.h>

#include "services/same/same_header.h"

#ifdef __cplusplus
extern "C" {
#endif

bool match_location(const struct same_location *alert, const struct same_location *configured);

/**
 * Which of the header's locations match the configured list.
 *
 * @return bit i set when h->locations[i] matches; 0 when none does.
 */
uint32_t match_header(const struct same_header *h, const struct same_location *configured,
		      uint8_t count);

#ifdef __cplusplus
}
#endif

#endif /* SERVICES_MATCH_MATCH_H_ */
