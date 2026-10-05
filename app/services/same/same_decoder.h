/*
 * Streaming SAME decoder: int16 audio in, validated headers and
 * end-of-message events out through callbacks.
 *
 * Plain C99, no Zephyr includes, no heap. Audio must be at 16 MHz / 1536 =
 * 10,416.67 Hz, exactly 20 samples per bit. Feed any number of samples at
 * a time; results do not depend on how the audio is split into blocks.
 *
 * Stages (docs/firmware-spec.md, SAME decoding):
 *  1. Tone detection: sliding 20-sample correlators at mark (2083.33 Hz)
 *     and space (1562.5 Hz); a bit is the sign of the energy difference.
 *  2. Bit sync: a timing loop nudged at each mark-space transition; locks on
 *     the 0xAB preamble while carrier quality is high.
 *  3. Framing: 8-bit bytes, least significant bit first. Only the low 7
 *     bits are kept (47 CFR 11.31 sends 7-bit ASCII with an 8th null bit
 *     that may be 0 or 1). A header copy ends at its closing '-', when the
 *     carrier drops, or at SAME_HEADER_MAX_LEN characters.
 *  4. Voting: up to 3 copies, byte-wise majority; with 2 copies they must
 *     match. A group is voted after its third copy, or once
 *     SAME_COPY_TIMEOUT_MS pass after its latest copy with no new copy
 *     arriving. One copy alone is never emitted.
 *  5. Parsing: services/same/same_header.h; malformed headers are dropped.
 *
 * End of message: one copy of NNNN (at least 3 of the 4 characters) fires
 * on_eom; further EOM copies within SAME_COPY_TIMEOUT_MS of the last are
 * the same event. A pending header group is voted first, so callbacks
 * always arrive in broadcast order.
 *
 * Callbacks run inside same_feed() or same_flush(), in the caller's thread.
 */

#ifndef SERVICES_SAME_DECODER_H_
#define SERVICES_SAME_DECODER_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "services/same/same_header.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SAME_SAMPLES_PER_BIT 20

/** Three times the sample rate, an integer: 3 x 16 MHz / 1536 = 31,250. */
#define SAME_SAMPLE_RATE_X3 31250U

/** Milliseconds to samples, rounded. */
#define SAME_MS_TO_SAMPLES(ms)                                                                     \
	((uint32_t)(((uint64_t)(ms) * SAME_SAMPLE_RATE_X3 + 1500U) / 3000U))

/**
 * How long a header group waits after its latest copy ends for another copy
 * to start. Initial value; tune with recordings.
 */
#define SAME_COPY_TIMEOUT_MS 5000U

typedef void (*same_header_cb)(const struct same_header *header, void *user);
typedef void (*same_eom_cb)(void *user);

/** Counters for the health supervisor and for tuning. All wrap. */
struct same_stats {
	uint32_t samples;        /* fed so far; stalls if audio stops */
	uint32_t preamble_locks;
	uint32_t header_copies;  /* copies framed as ZCZC */
	uint32_t eom_copies;     /* copies framed as NNNN */
	uint32_t headers;        /* on_header calls */
	uint32_t eoms;           /* on_eom calls */
	uint32_t single_copy;    /* groups dropped because only one copy arrived */
	uint32_t vote_failures;  /* groups whose copies disagreed */
	uint32_t parse_failures; /* voted text that was not a valid header */
	uint32_t frame_aborts;   /* locks that framed neither ZCZC nor NNNN */
};

/* Number of values the correlators keep per sample. */
#define SAME_CORR_TERMS 6

/** Decoder state. Allocate statically; all fields are private. */
struct same_decoder {
	same_header_cb on_header;
	same_eom_cb on_eom;
	void *user;
	uint32_t now; /* sample clock */

	/* Tone correlators: exact integer running sums over the last 20 samples. */
	int32_t ring[SAME_CORR_TERMS][SAME_SAMPLES_PER_BIT];
	int32_t sum[SAME_CORR_TERMS];
	uint8_t idx;

	/* Bit timing. */
	float prev_diff;
	float bit_phase;      /* samples until the next bit decision */
	float period;         /* samples per bit, tracked */
	float since_decision; /* samples since the last bit decision */
	float cross_at;       /* strongest zero crossing since the last decision */
	float cross_strength;
	float quality;        /* smoothed carrier quality, 0..1 */
	uint8_t prev_bit;

	/* Framing. */
	uint8_t state;
	uint32_t shift;
	uint8_t bit_count;
	uint8_t byte;
	uint8_t preamble_bytes;
	bool header_copy;
	bool plus_seen;
	uint8_t dashes_after_plus;
	uint16_t msg_len;
	uint8_t msg[SAME_HEADER_MAX_LEN];

	/* Voting. */
	uint8_t copies[3][SAME_HEADER_MAX_LEN];
	uint16_t copy_len[3];
	uint8_t n_copies;
	uint32_t group_deadline;
	uint8_t voted[SAME_HEADER_MAX_LEN];
	bool eom_active;
	uint32_t eom_deadline;

	struct same_header header;
	struct same_stats stats;
};

/** Reset the decoder and register callbacks (either may be NULL). */
void same_init(struct same_decoder *d, same_header_cb on_header, same_eom_cb on_eom, void *user);

/** Decode n samples. */
void same_feed(struct same_decoder *d, const int16_t *samples, size_t n);

/**
 * The audio stream has stopped (end of file, or the tuner left the weather
 * channel): finish any header copy in progress, vote the pending group now
 * instead of waiting for the timeout, and forget the current EOM.
 */
void same_flush(struct same_decoder *d);

/** Counters since same_init(). */
const struct same_stats *same_get_stats(const struct same_decoder *d);

#ifdef __cplusplus
}
#endif

#endif /* SERVICES_SAME_DECODER_H_ */
