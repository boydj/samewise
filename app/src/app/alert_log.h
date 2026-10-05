/*
 * Alert log: the last ALERT_LOG_SIZE headers with their outcome, persisted
 * through hal/storage.h.
 *
 * The alert path never waits on flash: alert_log_append() only queues the
 * entry in RAM. alert_log_flush() writes queued entries to storage and runs
 * at lower priority (a work item on the board, a runner step in tests).
 */

#ifndef APP_ALERT_LOG_H_
#define APP_ALERT_LOG_H_

#include <stdint.h>

#include "services/same/same_header.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Entries kept (spec: Alert log characteristic). */
#define ALERT_LOG_SIZE 16
/** Entries that can wait in RAM for a flush. */
#define ALERT_LOG_QUEUE 8

enum alert_log_outcome {
	ALERT_LOG_ALERTED,
	ALERT_LOG_FILTERED, /* known event the filter doesn't alert on, tests included */
	ALERT_LOG_UNKNOWN,  /* event code missing from the table */
	ALERT_LOG_EXPIRED,  /* issue + purge already past on arrival */
};

/** Entry flags. */
#define ALERT_LOG_FLAG_FUTURE_ISSUE 0x01U /* issue time distrusted; expiry from receipt */

struct alert_log_entry {
	char raw[SAME_HEADER_MAX_LEN + 1];
	int64_t received_utc; /* -1 if the clock was unset */
	uint8_t outcome;      /* enum alert_log_outcome */
	uint8_t flags;        /* ALERT_LOG_FLAG_* */
};

/** Read the log's position from storage (it survives resets). */
void alert_log_init(void);

/** Queue an entry; never blocks. Drops the oldest queued entry if full. */
void alert_log_append(const struct same_header *h, int64_t received_utc,
		      enum alert_log_outcome outcome, uint8_t flags);

/** Write queued entries to storage. Returns the number written. */
int alert_log_flush(void);

/** Entries in storage (at most ALERT_LOG_SIZE), plus those still queued. */
uint32_t alert_log_count(void);

/** Entries ever appended since alert_log_init(). */
uint32_t alert_log_appended(void);

/** Entries dropped because the queue was full. */
uint32_t alert_log_dropped(void);

/** Read stored entry i, 0 = newest. Returns 0 or a negative errno. */
int alert_log_read(uint32_t i, struct alert_log_entry *out);

#ifdef __cplusplus
}
#endif

#endif /* APP_ALERT_LOG_H_ */
