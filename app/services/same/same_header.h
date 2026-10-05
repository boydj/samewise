/*
 * SAME header parser: ZCZC-ORG-EEE-PSSCCC-PSSCCC+TTTT-JJJHHMM-LLLLLLLL-
 *
 * Plain C99, no Zephyr includes. Validation is strict, and anything
 * malformed is rejected:
 *   - "ZCZC-" prefix and a closing '-' with nothing after it
 *   - ORG: EAS, CIV, WXR or PEP, the only originators in 47 CFR 11.31(d)(1)
 *   - EEE: 3 upper-case letters (the event table, not the parser, knows codes)
 *   - 1 to 31 locations PSSCCC, 6 digits each, '-' between, '+' after the last
 *   - TTTT: 4 digits, minutes 00-59 (any hours; real encoders send
 *     nonstandard increments, and a warning must not be dropped over that)
 *   - JJJHHMM: 7 digits, Julian day 001-366, hour 00-23, minute 00-59 (UTC)
 *   - LLLLLLLL: 8 printable ASCII characters other than '-' and '+', which
 *     11.31(b) reserves as separators
 */

#ifndef SERVICES_SAME_HEADER_H_
#define SERVICES_SAME_HEADER_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Most location codes in one header (47 CFR 11.31). */
#define SAME_MAX_LOCATIONS 31

/**
 * Longest valid header in characters: "ZCZC-ORG-EEE-" (13) + 31 locations
 * of 7 characters (217) + "TTTT-" (5) + "JJJHHMM-" (8) + "LLLLLLLL-" (9).
 */
#define SAME_HEADER_MAX_LEN 252

/** One location code PSSCCC. */
struct same_location {
	uint8_t subdivision; /* P: 0 = whole county, 1-9 = part of it */
	uint8_t state;       /* SS: FIPS state code, 00 = all states */
	uint16_t county;     /* CCC: FIPS county code, 000 = whole state */
};

struct same_header {
	char originator[4]; /* ORG, NUL-terminated: EAS, CIV, WXR or PEP */
	char event[4];      /* EEE, NUL-terminated: TOR, SVR, RWT ... */
	uint8_t location_count;
	struct same_location locations[SAME_MAX_LOCATIONS];
	uint8_t purge_hours;   /* +TTTT: valid period after issue */
	uint8_t purge_minutes;
	uint16_t issue_day;    /* JJJ: Julian day of year, 1-366, UTC */
	uint8_t issue_hour;    /* HH, UTC */
	uint8_t issue_minute;  /* MM */
	char station[9];       /* LLLLLLLL, NUL-terminated */
	char raw[SAME_HEADER_MAX_LEN + 1]; /* the full header text, NUL-terminated */
	uint8_t copies;        /* copies the decoder voted over (2 or 3); 0 from the parser */
};

/** Parser results; 0 is success. */
enum same_parse_result {
	SAME_PARSE_OK = 0,
	SAME_PARSE_ERR_LENGTH = -1,     /* empty, too long, or text after the closing '-' */
	SAME_PARSE_ERR_PREFIX = -2,     /* does not start "ZCZC-" */
	SAME_PARSE_ERR_ORIGINATOR = -3,
	SAME_PARSE_ERR_EVENT = -4,
	SAME_PARSE_ERR_LOCATION = -5,   /* missing, or not 6 digits */
	SAME_PARSE_ERR_LOCATION_COUNT = -6, /* more than 31 */
	SAME_PARSE_ERR_PURGE = -7,
	SAME_PARSE_ERR_ISSUE = -8,
	SAME_PARSE_ERR_STATION = -9,
};

/**
 * Parse and validate a header.
 *
 * @param text header characters (need not be NUL-terminated)
 * @param len  number of characters, including the closing '-'
 * @param out  filled on success; contents undefined on failure
 * @return SAME_PARSE_OK or a negative enum same_parse_result
 */
int same_parse_header(const char *text, size_t len, struct same_header *out);

/** Short description of a parser result, for logs. */
const char *same_parse_strerror(int result);

#ifdef __cplusplus
}
#endif

#endif /* SERVICES_SAME_HEADER_H_ */
