/*
 * POSIX TZ strings (IEEE 1003.1 section 8.3, with the RFC 8536 extension of
 * rule times from -167 to 167 hours): std offset [dst [offset] ,rule,rule].
 *
 * Plain C99, no Zephyr includes, no heap, no floating point. A DST name
 * without rules is rejected rather than guessed.
 */

#ifndef SERVICES_BLE_TZ_H_
#define SERVICES_BLE_TZ_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TZ_NAME_MIN 3
#define TZ_NAME_MAX 15

enum tz_rule_kind {
	TZ_RULE_JULIAN,    /* Jn: 1-365, 29 February never counted */
	TZ_RULE_ZERO_DAY,  /* n: 0-365, 29 February counted */
	TZ_RULE_MONTH,     /* Mm.w.d */
};

struct tz_rule {
	uint8_t kind;    /* enum tz_rule_kind */
	uint8_t month;   /* 1-12 */
	uint8_t week;    /* 1-5, 5 = last */
	uint8_t wday;    /* 0 = Sunday */
	uint16_t day;    /* Jn or n */
	int32_t time_s;  /* local wall time of the change, default 02:00 */
};

struct tz_info {
	char std_name[TZ_NAME_MAX + 1];
	char dst_name[TZ_NAME_MAX + 1];
	int32_t std_offset_s; /* local = UTC + offset (EST5 gives -18000) */
	int32_t dst_offset_s;
	bool has_dst;
	struct tz_rule start; /* in standard time */
	struct tz_rule end;   /* in daylight time */
};

/** Parse s; 0 on success, -1 on any malformed string (out untouched). */
int tz_parse(const char *s, struct tz_info *out);

/** Local wall-clock seconds for a UTC time; dst (may be NULL) says which offset applied. */
int64_t tz_local(const struct tz_info *tz, int64_t utc, bool *dst);

#ifdef __cplusplus
}
#endif

#endif /* SERVICES_BLE_TZ_H_ */
