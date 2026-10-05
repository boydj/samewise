/*
 * SAME decoder against generated vectors (tools/vectors, group "decoder").
 * One test per milestone 1 checklist item, plus a sweep that checks every
 * vector against its sidecar.
 */

#include <string.h>
#include <zephyr/ztest.h>

#include "decode_util.h"
#include "vectors.h"

static struct decode_result r, r2;
static size_t block_512 = 512;

static const struct test_vector *vec(const char *name)
{
	const struct test_vector *v = test_vector_find(name);

	zassert_not_null(v, "no vector %s", name);
	return v;
}

/* Decode with 512-sample blocks and flush at end of file. */
static const struct test_vector *decode(const char *name)
{
	const struct test_vector *v = vec(name);

	zassert_ok(decode_wav(v->wav, block_fixed, &block_512, true, &r), "%s", name);
	zassert_equal(r.samples, v->samples, "%s: whole file fed", name);
	return v;
}

/* Exactly the expected headers, in order, and the expected EOM count. */
static void assert_matches_sidecar(const struct test_vector *v, const struct decode_result *res)
{
	zassert_equal(res->n_headers, v->n_headers, "%s: %u headers, want %u", v->name,
		      res->n_headers, v->n_headers);
	for (uint32_t i = 0; i < v->n_headers; i++) {
		zassert_str_equal(res->headers[i], v->headers[i], "%s: header %u", v->name, i);
	}
	zassert_equal(res->n_eoms, v->eom_count, "%s: %u EOMs, want %u", v->name, res->n_eoms,
		      v->eom_count);
}

ZTEST(same_decoder, test_every_vector_matches_its_sidecar)
{
	for (size_t i = 0; i < TEST_VECTOR_COUNT; i++) {
		decode(test_vectors[i].name);
		assert_matches_sidecar(&test_vectors[i], &r);
	}
}

ZTEST(same_decoder, test_clean_header_decodes_exactly_once)
{
	const struct test_vector *v = decode("clean");

	zassert_equal(r.n_headers, 1, "callback fires once");
	zassert_str_equal(r.headers[0], v->headers[0]);
	zassert_equal(r.parsed[0].copies, 3);
	zassert_str_equal(r.parsed[0].event, "TOR");
	zassert_equal(r.n_eoms, 0);
	zassert_equal(r.stats.header_copies, 3);
	zassert_equal(r.stats.vote_failures + r.stats.parse_failures + r.stats.single_copy, 0);
}

ZTEST(same_decoder, test_one_corrupted_byte_in_one_copy_is_voted_out)
{
	const struct test_vector *v = decode("corrupt_one_copy");

	zassert_equal(r.n_headers, 1);
	zassert_str_equal(r.headers[0], v->headers[0]);
	zassert_equal(r.parsed[0].copies, 3);

	/* Every copy damaged, at different bytes: still recoverable. */
	v = decode("corrupt_each_copy");
	zassert_equal(r.n_headers, 1);
	zassert_str_equal(r.headers[0], v->headers[0]);
}

ZTEST(same_decoder, test_only_two_copies_still_decode)
{
	static const char *const names[] = {"two_copies_drop1", "two_copies_drop3"};

	for (size_t i = 0; i < ARRAY_SIZE(names); i++) {
		const struct test_vector *v = decode(names[i]);

		zassert_equal(r.n_headers, 1, "%s", names[i]);
		zassert_str_equal(r.headers[0], v->headers[0]);
		zassert_equal(r.parsed[0].copies, 2, "%s", names[i]);
	}
}

ZTEST(same_decoder, test_two_copies_emitted_by_timeout_without_flush)
{
	const struct test_vector *v = vec("two_copies_drop3");

	zassert_ok(decode_wav(v->wav, block_fixed, &block_512, false, &r));
	zassert_equal(r.n_headers, 0, "file ends before the 5 s timeout");
	decode_silence(SAME_COPY_TIMEOUT_MS, &r);
	zassert_equal(r.n_headers, 1, "emitted once the timeout passes");
	zassert_str_equal(r.headers[0], v->headers[0]);
}

ZTEST(same_decoder, test_two_copies_that_disagree_are_rejected)
{
	decode("two_copies_disagree");
	zassert_equal(r.n_headers, 0);
	zassert_equal(r.stats.vote_failures, 1);
}

ZTEST(same_decoder, test_one_copy_is_not_emitted)
{
	decode("one_copy");
	zassert_equal(r.n_headers, 0);
	zassert_equal(r.stats.header_copies, 1, "the copy itself was received");
	zassert_equal(r.stats.single_copy, 1);
}

ZTEST(same_decoder, test_31_location_codes)
{
	const struct same_header *h;

	decode("loc31");
	zassert_equal(r.n_headers, 1);
	h = &r.parsed[0];
	zassert_equal(h->location_count, 31);
	zassert_equal(h->locations[0].subdivision, 1);
	zassert_equal(h->locations[0].state, 48);
	zassert_equal(h->locations[0].county, 453);
	zassert_equal(h->locations[1].state, 48);
	zassert_equal(h->locations[1].county, 0, "whole state");
	zassert_equal(h->locations[2].state, 0);
	zassert_equal(h->locations[30].county, 48001 % 1000 + 2 * 27);
	zassert_equal(strlen(h->raw), SAME_HEADER_MAX_LEN, "31 locations is the longest header");
}

ZTEST(same_decoder, test_frequency_offsets)
{
	static const char *const names[] = {"freq_-2", "freq_-1", "freq_+1", "freq_+2"};

	for (size_t i = 0; i < ARRAY_SIZE(names); i++) {
		const struct test_vector *v = decode(names[i]);

		zassert_equal(r.n_headers, 1, "%s", names[i]);
		zassert_str_equal(r.headers[0], v->headers[0], "%s", names[i]);
		zassert_equal(r.parsed[0].copies, 3, "%s: every copy", names[i]);
	}
}

ZTEST(same_decoder, test_timing_offsets)
{
	static const char *const names[] = {"timing_-2", "timing_-1", "timing_+1", "timing_+2",
					    "offsets_combined"};

	for (size_t i = 0; i < ARRAY_SIZE(names); i++) {
		const struct test_vector *v = decode(names[i]);

		zassert_equal(r.n_headers, 1, "%s", names[i]);
		zassert_str_equal(r.headers[0], v->headers[0], "%s", names[i]);
		zassert_equal(r.parsed[0].copies, 3, "%s: every copy", names[i]);
	}
}

ZTEST(same_decoder, test_header_then_eom_fire_in_order)
{
	decode("header_eom");
	zassert_equal(r.n_headers, 1);
	zassert_equal(r.n_eoms, 1, "three EOM copies are one event");
	zassert_equal(r.stats.eom_copies, 3);
	zassert_mem_equal(r.events, "HE", 2);
	zassert_equal(r.n_events, 2);
}

ZTEST(same_decoder, test_eom_alone)
{
	decode("eom_only");
	zassert_equal(r.n_headers, 0);
	zassert_equal(r.n_eoms, 1);
}

ZTEST(same_decoder, test_random_block_sizes_give_identical_results)
{
	static const char *const names[] = {"header_eom", "back_to_back_lost_copy",
					    "corrupt_each_copy", "offsets_combined", "malformed"};

	for (size_t i = 0; i < ARRAY_SIZE(names); i++) {
		const struct test_vector *v = vec(names[i]);

		zassert_ok(decode_wav(v->wav, block_fixed, &block_512, true, &r));
		for (uint32_t seed = 1; seed <= 10; seed++) {
			uint32_t state = seed * 2654435761U;

			zassert_ok(decode_wav(v->wav, block_random, &state, true, &r2));
			zassert_equal(r2.n_headers, r.n_headers, "%s seed %u", names[i], seed);
			zassert_equal(r2.n_eoms, r.n_eoms, "%s seed %u", names[i], seed);
			zassert_mem_equal(r2.headers, r.headers, sizeof(r.headers), "%s seed %u",
					  names[i], seed);
			zassert_mem_equal(r2.events, r.events, sizeof(r.events));
			zassert_mem_equal(&r2.stats, &r.stats, sizeof(r.stats),
					  "%s seed %u: internal counters differ", names[i], seed);
		}
	}
}

ZTEST(same_decoder, test_malformed_headers_are_rejected)
{
	decode("malformed");
	zassert_equal(r.n_headers, 0);
	/* MALFORMED in tools/vectors/build_vectors.py: 10 headers. */
	zassert_equal(r.stats.header_copies, 10 * 3, "every copy was framed");
	zassert_equal(r.stats.parse_failures + r.stats.vote_failures, 10, "each group rejected");
}

ZTEST(same_decoder, test_two_alerts_back_to_back)
{
	const struct test_vector *v = decode("back_to_back");

	zassert_equal(r.n_headers, 2);
	zassert_str_equal(r.headers[0], v->headers[0]);
	zassert_str_equal(r.headers[1], v->headers[1]);

	/* First alert's middle copy lost: its group spans into the second. */
	v = decode("back_to_back_lost_copy");
	zassert_equal(r.n_headers, 2);
	zassert_str_equal(r.headers[0], v->headers[0]);
	zassert_str_equal(r.headers[1], v->headers[1]);
}

ZTEST(same_decoder, test_eighth_bit_is_ignored)
{
	const struct test_vector *v = decode("eighth_bit");

	zassert_equal(r.n_headers, 1);
	zassert_str_equal(r.headers[0], v->headers[0]);
}

ZTEST(same_decoder, test_level_independent)
{
	static const char *const names[] = {"quiet", "loud_clipping", "clean_noise_lead"};

	for (size_t i = 0; i < ARRAY_SIZE(names); i++) {
		const struct test_vector *v = decode(names[i]);

		zassert_equal(r.n_headers, 1, "%s", names[i]);
		zassert_str_equal(r.headers[0], v->headers[0], "%s", names[i]);
	}
}

ZTEST(same_decoder, test_stats_count_samples)
{
	const struct test_vector *v = decode("clean");

	zassert_equal(r.stats.samples, v->samples);
	zassert_true(r.stats.preamble_locks >= 3);
}

ZTEST_SUITE(same_decoder, NULL, NULL, NULL, NULL, NULL);
