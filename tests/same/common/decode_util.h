/*
 * Test helper: run a WAV through the audio_in fake into the SAME decoder
 * and record every callback.
 */

#ifndef TESTS_SAME_DECODE_UTIL_H_
#define TESTS_SAME_DECODE_UTIL_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "services/same/same_decoder.h"

#define DECODE_MAX_HEADERS 64
#define DECODE_MAX_EVENTS  128

struct decode_result {
	uint32_t n_headers;
	char headers[DECODE_MAX_HEADERS][SAME_HEADER_MAX_LEN + 1];
	struct same_header parsed[DECODE_MAX_HEADERS];
	uint32_t n_eoms;
	char events[DECODE_MAX_EVENTS + 1]; /* 'H' or 'E' per callback, in order */
	uint32_t n_events;
	uint32_t samples;
	struct same_stats stats;
};

/* Returns the next block size; ctx is passed through. */
typedef size_t (*block_size_fn)(void *ctx);

/** A fixed block size; ctx points at a size_t. */
size_t block_fixed(void *ctx);

/** Random block sizes 1 to 512; ctx points at a uint32_t xorshift state. */
size_t block_random(void *ctx);

/**
 * Decode a WAV file. With flush, same_flush() runs at end of file, as when
 * the audio stream stops. Returns 0 or a negative errno from the fake.
 */
int decode_wav(const char *path, block_size_fn block, void *ctx, bool flush,
	       struct decode_result *r);

/** The decoder used by the last decode_wav(), for feeding more audio. */
struct same_decoder *decode_decoder(void);

/** Feed ms of silence to the last decoder, recording into r. */
void decode_silence(uint32_t ms, struct decode_result *r);

#endif /* TESTS_SAME_DECODE_UTIL_H_ */
