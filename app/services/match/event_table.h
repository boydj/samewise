/*
 * Event table: SAME event codes with a display name and a class.
 *
 * Configuration is data (spec: Scope and goals): the phone can replace the
 * table, so it is a versioned value, not code. Entries keep their index,
 * which the custom filter bitmap refers to. Plain C99, no heap.
 */

#ifndef SERVICES_MATCH_EVENT_TABLE_H_
#define SERVICES_MATCH_EVENT_TABLE_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Most entries a table holds (the NWS list has under 80). */
#define EVENT_TABLE_MAX 128
/** Longest display name, excluding the NUL. */
#define EVENT_NAME_MAX 31

enum event_class {
	EVENT_CLASS_WARNING,
	EVENT_CLASS_WATCH,
	EVENT_CLASS_ADVISORY,
	EVENT_CLASS_STATEMENT,
	EVENT_CLASS_TEST, /* never alerts; RWT feeds the health check */
	EVENT_CLASS_COUNT,
};

struct event_entry {
	char code[4]; /* EEE, NUL-terminated */
	char name[EVENT_NAME_MAX + 1];
	uint8_t cls; /* enum event_class */
};

struct event_table {
	uint16_t version;
	uint16_t count;
	struct event_entry entries[EVENT_TABLE_MAX];
};

enum event_table_result {
	EVENT_TABLE_OK = 0,
	EVENT_TABLE_ERR_FULL = -1,
	EVENT_TABLE_ERR_CODE = -2,      /* not 3 upper-case letters */
	EVENT_TABLE_ERR_DUPLICATE = -3,
	EVENT_TABLE_ERR_CLASS = -4,
	EVENT_TABLE_ERR_NAME = -5,      /* empty or too long */
};

/** Empty table with the given version. */
void event_table_clear(struct event_table *t, uint16_t version);

/** Replace t with the built-in default table (event_table_default.c). */
void event_table_load_default(struct event_table *t);

/** Append an entry; its index is the previous count. */
int event_table_add(struct event_table *t, const char *code, const char *name,
		    enum event_class cls);

/** Index of a 3-character code (need not be NUL-terminated), or -1. */
int event_table_index(const struct event_table *t, const char *code);

/** Entry for a code, or NULL if the table doesn't know it. */
const struct event_entry *event_table_find(const struct event_table *t, const char *code);

/** "warning", "watch", "advisory", "statement", "test" or "unknown". */
const char *event_class_name(int cls);

#ifdef __cplusplus
}
#endif

#endif /* SERVICES_MATCH_EVENT_TABLE_H_ */
