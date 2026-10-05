/*
 * Decode rate versus SNR, 30 dB down to 0 dB in 3 dB steps, written to
 * docs/decoder-snr.md. Gate: 100% at 20 dB and above; no wrong headers at
 * any SNR.
 */

#include <stdio.h>
#include <string.h>
#include <zephyr/ztest.h>

#include "decode_util.h"
#include "fakes/host_file/host_file.h"
#include "vectors.h"

#define GATE_SNR_DB 20.0f

static struct decode_result r;

struct point {
	float snr_db;
	uint32_t expected;
	uint32_t decoded;
	uint32_t wrong;
	struct same_stats stats;
};

static struct point points[TEST_VECTOR_COUNT];
static char report[4096];

static bool listed(const char *h, const char *const *list, uint32_t n)
{
	for (uint32_t i = 0; i < n; i++) {
		if (strcmp(h, list[i]) == 0) {
			return true;
		}
	}
	return false;
}

static void write_report(size_t n_points)
{
	const char *path = WX_DOCS_DIR "/decoder-snr.md";
	int len = 0;
	int fd;

	len += snprintf(report + len, sizeof(report) - len,
		"# SAME decoder: decode rate versus SNR\n\n"
		"Written by `tests/same/snr` (ztest on `native_sim`); do not edit. Regenerate with\n"
		"`west twister -T tests/same/snr -p native_sim`.\n\n"
		"- Vectors: `tools/vectors/build_vectors.py --group snr`: %u distinct random valid\n"
		"  headers (1 to 6 locations), each sent 3 times 1 s apart, 2 s between alerts.\n"
		"- SNR: AFSK power over white Gaussian noise power across the full band (0 to\n"
		"  5,208 Hz at 10,416.67 Hz sampling). 0 dB here is about 10 dB Eb/N0.\n"
		"- Decoded: expected headers reported exactly. Wrong: valid headers reported that\n"
		"  were never sent (a false alert risk); must be 0 everywhere.\n"
		"- Gate: 100%% at %.0f dB and above.\n\n"
		"| SNR (dB) | Decoded | Rate | Wrong | Copies framed | Vote failures | Parse failures |\n"
		"| ---: | ---: | ---: | ---: | ---: | ---: | ---: |\n",
		points[0].expected, (double)GATE_SNR_DB);
	for (size_t i = 0; i < n_points; i++) {
		const struct point *p = &points[i];

		len += snprintf(report + len, sizeof(report) - len,
				"| %.0f | %u/%u | %.0f%% | %u | %u/%u | %u | %u |\n",
				(double)p->snr_db, p->decoded, p->expected,
				100.0 * p->decoded / p->expected, p->wrong, p->stats.header_copies,
				3U * p->expected, p->stats.vote_failures, p->stats.parse_failures);
	}
	zassert_true(len > 0 && (size_t)len < sizeof(report));

	fd = wx_host_file_open_write(path);
	zassert_true(fd >= 0, "cannot write %s", path);
	zassert_equal(wx_host_file_write(fd, report, (unsigned long)len), len);
	zassert_ok(wx_host_file_close(fd));
	TC_PRINT("%s", report);
}

ZTEST(same_snr, test_decode_rate_versus_snr)
{
	size_t block = 512;

	for (size_t i = 0; i < TEST_VECTOR_COUNT; i++) {
		const struct test_vector *v = &test_vectors[i];
		struct point *p = &points[i];

		zassert_true(v->has_snr);
		zassert_ok(decode_wav(v->wav, block_fixed, &block, true, &r), "%s", v->name);
		p->snr_db = v->snr_db;
		p->expected = v->n_headers;
		p->stats = r.stats;
		for (uint32_t k = 0; k < v->n_headers; k++) {
			bool found = false;

			for (uint32_t j = 0; j < r.n_headers && j < DECODE_MAX_HEADERS; j++) {
				found = found || strcmp(r.headers[j], v->headers[k]) == 0;
			}
			p->decoded += found ? 1U : 0U;
		}
		for (uint32_t j = 0; j < r.n_headers && j < DECODE_MAX_HEADERS; j++) {
			p->wrong += listed(r.headers[j], v->headers, v->n_headers) ? 0U : 1U;
		}
	}

	write_report(TEST_VECTOR_COUNT);

	for (size_t i = 0; i < TEST_VECTOR_COUNT; i++) {
		const struct point *p = &points[i];

		zassert_equal(p->wrong, 0, "%.0f dB: wrong header", (double)p->snr_db);
		if (p->snr_db >= GATE_SNR_DB) {
			zassert_equal(p->decoded, p->expected, "%.0f dB: %u/%u decoded",
				      (double)p->snr_db, p->decoded, p->expected);
		}
	}
}

ZTEST_SUITE(same_snr, NULL, NULL, NULL, NULL, NULL);
