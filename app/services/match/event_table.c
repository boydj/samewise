/*
 * Event table operations. Plain C99.
 */

#include <string.h>

#include "services/match/event_table.h"

void event_table_clear(struct event_table *t, uint16_t version)
{
	memset(t, 0, sizeof(*t));
	t->version = version;
}

static bool valid_code(const char *code)
{
	for (int i = 0; i < 3; i++) {
		if (code[i] < 'A' || code[i] > 'Z') {
			return false;
		}
	}
	return true;
}

int event_table_add(struct event_table *t, const char *code, const char *name,
		    enum event_class cls)
{
	struct event_entry *e;
	size_t name_len;

	if (code == NULL || strlen(code) != 3U || !valid_code(code)) {
		return EVENT_TABLE_ERR_CODE;
	}
	if ((int)cls < 0 || cls >= EVENT_CLASS_COUNT) {
		return EVENT_TABLE_ERR_CLASS;
	}
	name_len = name == NULL ? 0U : strlen(name);
	if (name_len == 0U || name_len > EVENT_NAME_MAX) {
		return EVENT_TABLE_ERR_NAME;
	}
	if (event_table_index(t, code) >= 0) {
		return EVENT_TABLE_ERR_DUPLICATE;
	}
	if (t->count >= EVENT_TABLE_MAX) {
		return EVENT_TABLE_ERR_FULL;
	}
	e = &t->entries[t->count++];
	memcpy(e->code, code, 4);
	memcpy(e->name, name, name_len + 1U);
	e->cls = (uint8_t)cls;
	return EVENT_TABLE_OK;
}

int event_table_index(const struct event_table *t, const char *code)
{
	if (code == NULL) {
		return -1;
	}
	for (uint16_t i = 0; i < t->count; i++) {
		if (memcmp(t->entries[i].code, code, 3) == 0) {
			return (int)i;
		}
	}
	return -1;
}

const struct event_entry *event_table_find(const struct event_table *t, const char *code)
{
	int i = event_table_index(t, code);

	return i < 0 ? NULL : &t->entries[i];
}

const char *event_class_name(int cls)
{
	static const char *const names[EVENT_CLASS_COUNT] = {
		"warning", "watch", "advisory", "statement", "test"};

	return (cls >= 0 && cls < EVENT_CLASS_COUNT) ? names[cls] : "unknown";
}
