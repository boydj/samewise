/*
 * Event filter (spec: SAME decoding step 7). Plain C99.
 *
 * The class comes from the event table. Tests (RWT, RMT, NPT, DMO) never
 * alert under any preset; they are logged, and RWT feeds the health check.
 * Codes the table doesn't know are logged, never alerted.
 */

#ifndef SERVICES_MATCH_FILTER_H_
#define SERVICES_MATCH_FILTER_H_

#include <stdint.h>

#include "services/match/event_table.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Bytes in a custom filter bitmap: one bit per event table index. */
#define FILTER_CUSTOM_BYTES (EVENT_TABLE_MAX / 8)

enum filter_preset {
	FILTER_WARNINGS,         /* warnings only */
	FILTER_WARNINGS_WATCHES, /* default */
	FILTER_ALL,              /* every class except tests */
	FILTER_CUSTOM,           /* bitmap over the event table */
	FILTER_PRESET_COUNT,
};

enum filter_verdict {
	FILTER_VERDICT_ALERT,
	FILTER_VERDICT_LOG,     /* known code the filter doesn't alert on */
	FILTER_VERDICT_UNKNOWN, /* code missing from the table: log only */
};

/**
 * Decide what to do with an event code.
 *
 * @param event  3-character code (need not be NUL-terminated)
 * @param custom bitmap for FILTER_CUSTOM (bit i = table index i), else unused
 */
enum filter_verdict filter_decide(const struct event_table *t, const char *event,
				  enum filter_preset preset,
				  const uint8_t custom[FILTER_CUSTOM_BYTES]);

#ifdef __cplusplus
}
#endif

#endif /* SERVICES_MATCH_FILTER_H_ */
