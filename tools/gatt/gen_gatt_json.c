/*
 * Prints docs/gatt.json from services/ble/gatt_table.h. Host tool: built and
 * run by tools/gatt/gatt_json.py, never part of the firmware.
 */

#include <stdio.h>

#include "services/ble/gatt_table.h"

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
	printf("\n  ]\n}\n");
	return 0;
}
