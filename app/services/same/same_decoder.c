/*
 * Streaming SAME decoder. Plain C99, single-precision float only.
 */

#include <string.h>

#include "services/same/same_decoder.h"

/* Correlator references over one bit, Q14: mark is 4 cycles, space 3. */
static const int16_t mark_cos[SAME_SAMPLES_PER_BIT] = {
	16384, 5063, -13255, -13255, 5063, 16384, 5063, -13255, -13255, 5063,
	16384, 5063, -13255, -13255, 5063, 16384, 5063, -13255, -13255, 5063};
static const int16_t mark_sin[SAME_SAMPLES_PER_BIT] = {
	0, 15582, 9630, -9630, -15582, 0, 15582, 9630, -9630, -15582,
	0, 15582, 9630, -9630, -15582, 0, 15582, 9630, -9630, -15582};
static const int16_t space_cos[SAME_SAMPLES_PER_BIT] = {
	16384, 9630, -5063, -15582, -13255, 0, 13255, 15582, 5063, -9630,
	-16384, -9630, 5063, 15582, 13255, 0, -13255, -15582, -5063, 9630};
static const int16_t space_sin[SAME_SAMPLES_PER_BIT] = {
	0, 13255, 15582, 5063, -9630, -16384, -9630, 5063, 15582, 13255,
	0, -13255, -15582, -5063, 9630, 16384, 9630, -5063, -15582, -13255};

enum { MARK_C, MARK_S, SPACE_C, SPACE_S, ENERGY, DC };

/*
 * Product scaling keeps every 20-term sum inside int32 and exact:
 * |x * ref| >> 4 < 2^25, x^2 >> 6 <= 2^24.
 */
#define REF_SHIFT    4
#define ENERGY_SHIFT 6

/*
 * Carrier quality = tone energy / window energy, 1.0 for a clean bit-aligned
 * tone and 2/20 for white noise. With the shifts above, a tone of amplitude A
 * gives |M|^2 = (A * 1024 * 10)^2 and window energy 10 * A^2.
 */
#define QUALITY_NORM  (1024.0f * 1024.0f * 10.0f)
#define MIN_ENERGY    80.0f /* below an RMS of 2 LSB the window is silence */
#define QUALITY_SMOOTH 0.125f
#define QUALITY_LOCK  0.35f /* needed to lock on the preamble */
#define QUALITY_LOST  0.2f  /* below this the copy has ended */

/* Timing loop gains, per transition, and the tracking range (+/- 3%). */
#define TIMING_ALPHA 0.25f
#define TIMING_BETA  0.01f
#define PERIOD_NOM   ((float)SAME_SAMPLES_PER_BIT)
#define PERIOD_MIN   (PERIOD_NOM * 0.97f)
#define PERIOD_MAX   (PERIOD_NOM * 1.03f)

#define PREAMBLE_BYTE      0xABU
#define PREAMBLE_PAIR      0xABABU
#define MAX_PREAMBLE_BYTES 24U /* 16 sent, with slack for bit errors */
#define PREFIX_LEN         4U

enum { HUNT, PREAMBLE, MESSAGE };

static uint32_t copy_timeout(void)
{
	return SAME_MS_TO_SAMPLES(SAME_COPY_TIMEOUT_MS);
}

static bool deadline_passed(uint32_t now, uint32_t deadline)
{
	return (int32_t)(now - deadline) >= 0;
}

static unsigned int popcount8(uint8_t v)
{
	unsigned int n = 0;

	while (v != 0U) {
		n += v & 1U;
		v >>= 1;
	}
	return n;
}

/* Number of positions where the first 4 characters match a 4-char prefix. */
static unsigned int prefix_matches(const uint8_t *msg, const char *prefix)
{
	unsigned int n = 0;

	for (size_t i = 0; i < PREFIX_LEN; i++) {
		n += (msg[i] == (uint8_t)prefix[i]) ? 1U : 0U;
	}
	return n;
}

/* Tracks a header's structural end: the third '-' after the '+'. */
static bool header_ends_with(bool *plus_seen, uint8_t *dashes, uint8_t c)
{
	if (!*plus_seen) {
		*plus_seen = (c == '+');
		return false;
	}
	if (c == '-') {
		(*dashes)++;
	}
	return *dashes == 3U;
}

/* Byte-wise majority over the first n_copies copies, into d->voted. */
static int vote(struct same_decoder *d, uint8_t n_copies, size_t *out_len)
{
	bool plus_seen = false;
	uint8_t dashes = 0;
	uint16_t longest = 0;

	for (uint8_t c = 0; c < n_copies; c++) {
		if (d->copy_len[c] > longest) {
			longest = d->copy_len[c];
		}
	}

	for (uint16_t i = 0; i < longest; i++) {
		uint8_t vals[3];
		uint8_t n = 0;
		int winner = -1;

		for (uint8_t c = 0; c < n_copies; c++) {
			if (i < d->copy_len[c]) {
				vals[n++] = d->copies[c][i];
			}
		}
		for (uint8_t a = 0; a < n && winner < 0; a++) {
			for (uint8_t b = (uint8_t)(a + 1U); b < n; b++) {
				if (vals[a] == vals[b]) {
					winner = vals[a];
					break;
				}
			}
		}
		if (winner < 0) {
			return -1; /* no two copies agree here */
		}
		d->voted[i] = (uint8_t)winner;
		if (header_ends_with(&plus_seen, &dashes, (uint8_t)winner)) {
			*out_len = (size_t)i + 1U;
			return 0;
		}
	}
	return -1; /* the copies agree, but on something without an end */
}

static void finalize_group(struct same_decoder *d)
{
	size_t len = 0;
	uint8_t copies = d->n_copies;

	d->n_copies = 0;
	if (copies == 0U) {
		return;
	}
	if (copies < 2U) {
		d->stats.single_copy++;
		return;
	}
	if (vote(d, copies, &len) != 0) {
		d->stats.vote_failures++;
		return;
	}
	if (same_parse_header((const char *)d->voted, len, &d->header) != SAME_PARSE_OK) {
		d->stats.parse_failures++;
		return;
	}
	d->header.copies = copies;
	d->stats.headers++;
	if (d->on_header != NULL) {
		d->on_header(&d->header, d->user);
	}
}

static void add_header_copy(struct same_decoder *d)
{
	uint8_t slot = d->n_copies;

	d->stats.header_copies++;
	memcpy(d->copies[slot], d->msg, d->msg_len);
	d->copy_len[slot] = d->msg_len;
	d->n_copies++;
	if (d->n_copies == 3U) {
		finalize_group(d);
	} else {
		d->group_deadline = d->now + copy_timeout();
	}
}

static void eom_copy(struct same_decoder *d)
{
	d->stats.eom_copies++;
	finalize_group(d); /* a header before this EOM is reported first */
	if (!d->eom_active) {
		d->stats.eoms++;
		if (d->on_eom != NULL) {
			d->on_eom(d->user);
		}
	}
	d->eom_active = true;
	d->eom_deadline = d->now + copy_timeout();
}

static void end_frame(struct same_decoder *d)
{
	d->state = HUNT;
	d->shift = 0;
	d->msg_len = 0;
}

static void framing(struct same_decoder *d, uint8_t bit)
{
	if (d->state == HUNT) {
		d->shift = (d->shift >> 1) | ((uint32_t)bit << 31);
		if ((d->shift >> 16) == PREAMBLE_PAIR && d->quality >= QUALITY_LOCK) {
			d->state = PREAMBLE;
			d->bit_count = 0;
			d->preamble_bytes = 2;
			d->stats.preamble_locks++;
		}
		return;
	}

	if (d->quality < QUALITY_LOST) {
		/* Carrier gone: a header copy ends here, anything shorter is lost. */
		if (d->state == MESSAGE && d->header_copy) {
			add_header_copy(d);
		} else if (d->state == MESSAGE || d->state == PREAMBLE) {
			d->stats.frame_aborts++;
		}
		end_frame(d);
		return;
	}

	d->byte = (uint8_t)((d->byte >> 1) | (bit << 7)); /* least significant bit first */
	if (++d->bit_count < 8U) {
		return;
	}
	d->bit_count = 0;

	if (d->state == PREAMBLE) {
		if (popcount8((uint8_t)(d->byte ^ PREAMBLE_BYTE)) <= 1U) {
			if (++d->preamble_bytes > MAX_PREAMBLE_BYTES) {
				d->stats.frame_aborts++;
				end_frame(d);
			}
			return;
		}
		d->state = MESSAGE;
		d->msg_len = 0;
		d->header_copy = false;
		d->plus_seen = false;
		d->dashes_after_plus = 0;
	}

	/* MESSAGE: keep 7 bits; the 8th is a null bit that may be 0 or 1. */
	d->msg[d->msg_len++] = d->byte & 0x7FU;

	if (d->msg_len == PREFIX_LEN) {
		if (prefix_matches(d->msg, "NNNN") >= 3U) {
			eom_copy(d);
			end_frame(d);
		} else if (prefix_matches(d->msg, "ZCZC") >= 3U) {
			d->header_copy = true;
		} else {
			d->stats.frame_aborts++;
			end_frame(d);
		}
		return;
	}

	if (d->header_copy &&
	    (header_ends_with(&d->plus_seen, &d->dashes_after_plus, d->msg[d->msg_len - 1U]) ||
	     d->msg_len == SAME_HEADER_MAX_LEN)) {
		add_header_copy(d);
		end_frame(d);
	}
}

static void bit_decision(struct same_decoder *d, float diff, float tone_energy)
{
	/* The ideal decision instant is now + residual, with residual in [-0.5, 0.5). */
	float residual = d->bit_phase;
	float window = (float)d->sum[ENERGY] * (float)(1U << ENERGY_SHIFT) -
		       (float)d->sum[DC] * (float)d->sum[DC] / PERIOD_NOM;
	float q = 0.0f;
	float err = 0.0f;
	uint8_t bit = diff > 0.0f ? 1U : 0U;

	if (window > MIN_ENERGY) {
		q = tone_energy / (window * QUALITY_NORM);
		q = q > 1.0f ? 1.0f : q;
	}
	d->quality += (q - d->quality) * QUALITY_SMOOTH;

	/*
	 * A mark-space transition puts a zero crossing of the difference half a
	 * bit before the instant the window covers the new bit exactly.
	 */
	if (bit != d->prev_bit && d->cross_strength > 0.0f) {
		float rel = d->cross_at - d->since_decision; /* <= 0: in the past */

		err = rel + d->period * 0.5f - residual;
	}
	d->bit_phase = residual + d->period + TIMING_ALPHA * err;
	d->period += TIMING_BETA * err;
	if (d->state == HUNT && d->quality < QUALITY_LOCK) {
		d->period = PERIOD_NOM; /* no carrier: don't let noise steer the clock */
	}
	d->period = d->period < PERIOD_MIN ? PERIOD_MIN : d->period;
	d->period = d->period > PERIOD_MAX ? PERIOD_MAX : d->period;

	d->prev_bit = bit;
	d->since_decision = 0.0f;
	d->cross_strength = 0.0f;

	framing(d, bit);
}

static void process_sample(struct same_decoder *d, int16_t sample)
{
	int32_t x = sample;
	uint8_t i = d->idx;
	int32_t terms[SAME_CORR_TERMS];
	float mark, space, diff;

	terms[MARK_C] = (x * mark_cos[i]) >> REF_SHIFT;
	terms[MARK_S] = (x * mark_sin[i]) >> REF_SHIFT;
	terms[SPACE_C] = (x * space_cos[i]) >> REF_SHIFT;
	terms[SPACE_S] = (x * space_sin[i]) >> REF_SHIFT;
	terms[ENERGY] = (x * x) >> ENERGY_SHIFT;
	terms[DC] = x;
	for (int k = 0; k < SAME_CORR_TERMS; k++) {
		d->sum[k] += terms[k] - d->ring[k][i];
		d->ring[k][i] = terms[k];
	}
	d->idx = (uint8_t)((i + 1U) % SAME_SAMPLES_PER_BIT);
	d->now++;
	d->stats.samples++;

	mark = (float)d->sum[MARK_C] * (float)d->sum[MARK_C] +
	       (float)d->sum[MARK_S] * (float)d->sum[MARK_S];
	space = (float)d->sum[SPACE_C] * (float)d->sum[SPACE_C] +
		(float)d->sum[SPACE_S] * (float)d->sum[SPACE_S];
	diff = mark - space;

	d->since_decision += 1.0f;
	if ((diff > 0.0f) != (d->prev_diff > 0.0f)) {
		float step = d->prev_diff - diff;
		float strength = step < 0.0f ? -step : step;

		if (strength > d->cross_strength) {
			/* Linear interpolation between the previous sample and this one. */
			d->cross_strength = strength;
			d->cross_at = d->since_decision - 1.0f + d->prev_diff / step;
		}
	}
	d->prev_diff = diff;

	d->bit_phase -= 1.0f;
	if (d->bit_phase < 0.5f) {
		bit_decision(d, diff, mark + space);
	}

	/* Never while a copy is arriving: a 31-location copy alone lasts 4.1 s. */
	if (d->n_copies > 0U && d->state == HUNT && deadline_passed(d->now, d->group_deadline)) {
		finalize_group(d);
	}
	if (d->eom_active && deadline_passed(d->now, d->eom_deadline)) {
		d->eom_active = false;
	}
}

void same_init(struct same_decoder *d, same_header_cb on_header, same_eom_cb on_eom, void *user)
{
	memset(d, 0, sizeof(*d));
	d->on_header = on_header;
	d->on_eom = on_eom;
	d->user = user;
	d->period = PERIOD_NOM;
	d->bit_phase = PERIOD_NOM;
	d->state = HUNT;
}

void same_feed(struct same_decoder *d, const int16_t *samples, size_t n)
{
	for (size_t i = 0; i < n; i++) {
		process_sample(d, samples[i]);
	}
}

void same_flush(struct same_decoder *d)
{
	if (d->state == MESSAGE && d->header_copy) {
		add_header_copy(d);
	}
	end_frame(d);
	finalize_group(d);
	d->eom_active = false;
}

const struct same_stats *same_get_stats(const struct same_decoder *d)
{
	return &d->stats;
}
