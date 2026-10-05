/*
 * Alert log: a RAM queue in front of a ring of storage records.
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "app/alert_log.h"
#include "hal/storage.h"

#define KEY_HEAD "log/head"

static struct {
	struct alert_log_entry queue[ALERT_LOG_QUEUE];
	uint8_t q_start;
	uint8_t q_len;
	uint32_t head;   /* total entries ever stored; next slot is head % SIZE */
	uint32_t appended;
	uint32_t dropped;
} s;

static void slot_key(uint32_t slot, char *key, size_t len)
{
	(void)snprintf(key, len, "log/%u", (unsigned int)slot);
}

void alert_log_init(void)
{
	uint32_t head = 0;

	memset(&s, 0, sizeof(s));
	if (hal_storage_read(KEY_HEAD, &head, sizeof(head)) == (int)sizeof(head)) {
		s.head = head;
	}
}

void alert_log_append(const struct same_header *h, int64_t received_utc,
		      enum alert_log_outcome outcome, uint8_t flags)
{
	struct alert_log_entry *e;

	if (s.q_len == ALERT_LOG_QUEUE) {
		s.q_start = (uint8_t)((s.q_start + 1U) % ALERT_LOG_QUEUE);
		s.q_len--;
		s.dropped++;
	}
	e = &s.queue[(s.q_start + s.q_len) % ALERT_LOG_QUEUE];
	memcpy(e->raw, h->raw, sizeof(e->raw));
	e->received_utc = received_utc;
	e->outcome = (uint8_t)outcome;
	e->flags = flags;
	s.q_len++;
	s.appended++;
}

int alert_log_flush(void)
{
	int written = 0;

	while (s.q_len > 0U) {
		char key[HAL_STORAGE_KEY_MAX + 1];
		uint32_t next = s.head + 1U;

		slot_key(s.head % ALERT_LOG_SIZE, key, sizeof(key));
		if (hal_storage_write(key, &s.queue[s.q_start], sizeof(s.queue[0])) != 0 ||
		    hal_storage_write(KEY_HEAD, &next, sizeof(next)) != 0) {
			break; /* keep it queued; retried on the next flush */
		}
		s.head = next;
		s.q_start = (uint8_t)((s.q_start + 1U) % ALERT_LOG_QUEUE);
		s.q_len--;
		written++;
	}
	return written;
}

uint32_t alert_log_count(void)
{
	return (s.head < ALERT_LOG_SIZE ? s.head : ALERT_LOG_SIZE) + s.q_len;
}

uint32_t alert_log_appended(void)
{
	return s.appended;
}

uint32_t alert_log_dropped(void)
{
	return s.dropped;
}

int alert_log_read(uint32_t i, struct alert_log_entry *out)
{
	char key[HAL_STORAGE_KEY_MAX + 1];
	uint32_t stored = s.head < ALERT_LOG_SIZE ? s.head : ALERT_LOG_SIZE;
	int len;

	if (i >= stored) {
		return -ENOENT;
	}
	slot_key((s.head - 1U - i) % ALERT_LOG_SIZE, key, sizeof(key));
	len = hal_storage_read(key, out, sizeof(*out));
	return len == (int)sizeof(*out) ? 0 : -EIO;
}
