/*
 * Prints docs/gatt.json from services/ble/gatt_table.h. Host tool: built and
 * run by tools/gatt/gatt_json.py, never part of the firmware.
 */

#include <stdio.h>

#include "services/ble/gatt_table.h"
#include "services/match/event_table.h"

static void props(unsigned int p)
{
	const char *sep = "";

	printf("[");
	if (p & WX_GATT_READ) {
		printf("%s\"read\"", sep);
		sep = ", ";
	}
	if (p & WX_GATT_WRITE) {
		printf("%s\"write\"", sep);
		sep = ", ";
	}
	if (p & WX_GATT_NOTIFY) {
		printf("%s\"notify\"", sep);
		sep = ", ";
	}
	if (p & WX_GATT_INDICATE) {
		printf("%s\"indicate\"", sep);
	}
	printf("]");
}

/* Names are printable ASCII (the codec checks); escape what JSON needs. */
static void json_string(const char *s)
{
	putchar('"');
	for (; *s != '\0'; s++) {
		if (*s == '"' || *s == '\\') {
			putchar('\\');
		}
		putchar(*s);
	}
	putchar('"');
}

/* The firmware's built-in table, so the mock peripheral starts with the same one. */
static void default_event_table(void)
{
	static struct event_table t;

	event_table_load_default(&t);
	printf("  \"default_event_table\": {\"version\": %u, \"entries\": [\n", t.version);
	for (unsigned int i = 0; i < t.count; i++) {
		printf("    {\"code\": \"%s\", \"class\": %u, \"name\": ", t.entries[i].code,
		       t.entries[i].cls);
		json_string(t.entries[i].name);
		printf("}%s\n", i + 1 < t.count ? "," : "");
	}
	printf("  ]},\n");
}

int main(void)
{
	int first = 1;

	printf("{\n  \"generated_from\": \"app/services/ble/gatt_table.h\",\n");
	printf("  \"security\": \"LE Secure Connections, bonded, encrypted and authenticated\",\n");
	printf("  \"service\": {\"name\": \"wx_settings\", \"uuid\": \"" WX_GATT_UUID_FORMAT "\"},\n",
	       WX_GATT_SERVICE_OFFSET);
	printf("  \"characteristics\": [\n");
#define WX_GATT_JSON(id, name, offset, p, max_len, schema, desc)                                   \
	printf("%s    {\"name\": \"%s\", \"uuid\": \"" WX_GATT_UUID_FORMAT "\", \"properties\": ",    \
	       first ? "" : ",\n", name, offset);                                                  \
	props(p);                                                                                  \
	printf(", \"max_length\": %d, \"schema\": %d, \"layout\": \"%s\"}", max_len, schema, desc); \
	first = 0;
	WX_GATT_CHARACTERISTICS(WX_GATT_JSON)
#undef WX_GATT_JSON
	printf("\n  ],\n");
	default_event_table();
	printf("  \"errors\": [\n");
	first = 1;
#define WX_GATT_ERR_JSON(id, code, meaning)                                                       \
	printf("%s    {\"name\": \"" #id "\", \"code\": %d, \"meaning\": \"%s\"}", first ? "" : ",\n", code, meaning); \
	first = 0;
	WX_GATT_ERRORS(WX_GATT_ERR_JSON)
#undef WX_GATT_ERR_JSON
	printf("\n  ]\n}\n");
	return 0;
}
