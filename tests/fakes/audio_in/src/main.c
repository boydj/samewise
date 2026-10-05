/*
 * audio_in fake: WAV reader, any block size, error handling.
 *
 * Writes its own WAV files (with awkward chunk layouts) through the host
 * file shim, so it does not depend on tools/samegen.
 */

#include <stdio.h>
#include <string.h>
#include <zephyr/ztest.h>

#include "fakes/audio_in_fake.h"
#include "fakes/host_file/host_file.h"
#include "hal/audio_in.h"

#define N_SAMPLES 5000

static int16_t ref[N_SAMPLES];
static int16_t got[2 * N_SAMPLES]; /* room for one full block past the end */
static char path[256];

struct wav_opts {
	uint16_t format;   /* 1 = PCM */
	uint16_t channels;
	uint32_t rate;
	uint16_t bits;
	bool list_before_fmt;   /* odd-sized LIST chunk before fmt */
	bool junk_before_data;  /* odd-sized chunk between fmt and data */
	bool fmt_extra;         /* 18-byte fmt chunk (cbSize) */
	bool omit_data;
	uint32_t data_size_override; /* 0 = actual size */
	uint32_t n;
};

static const struct wav_opts std_opts = {
	.format = 1, .channels = 1, .rate = 10417, .bits = 16, .n = N_SAMPLES};

static void put16(uint8_t *p, uint16_t v)
{
	p[0] = (uint8_t)v;
	p[1] = (uint8_t)(v >> 8);
}

static void put32(uint8_t *p, uint32_t v)
{
	put16(p, (uint16_t)v);
	put16(p + 2, (uint16_t)(v >> 16));
}

static void wr(int fd, const void *buf, unsigned long len)
{
	zassert_equal(wx_host_file_write(fd, buf, len), (long)len);
}

static void chunk(int fd, const char *id, uint32_t size)
{
	uint8_t h[8];

	memcpy(h, id, 4);
	put32(h + 4, size);
	wr(fd, h, 8);
}

static const char *write_wav(const char *name, const struct wav_opts *o)
{
	uint8_t fmt[18] = {0};
	uint8_t riff[12];
	int fd;

	snprintf(path, sizeof(path), "%s/%s.wav", TEST_TMP_DIR, name);
	fd = wx_host_file_open_write(path);
	zassert_true(fd >= 0, "cannot create %s", path);

	memcpy(riff, "RIFF", 4);
	put32(riff + 4, 0); /* readers must not rely on the RIFF size */
	memcpy(riff + 8, "WAVE", 4);
	wr(fd, riff, sizeof(riff));

	if (o->list_before_fmt) {
		chunk(fd, "LIST", 5);
		wr(fd, "abcde\0", 6); /* 5 bytes + pad */
	}

	put16(fmt, o->format);
	put16(fmt + 2, o->channels);
	put32(fmt + 4, o->rate);
	put32(fmt + 8, o->rate * o->channels * (o->bits / 8U));
	put16(fmt + 12, (uint16_t)(o->channels * (o->bits / 8U)));
	put16(fmt + 14, o->bits);
	chunk(fd, "fmt ", o->fmt_extra ? 18 : 16);
	wr(fd, fmt, o->fmt_extra ? 18 : 16);

	if (o->junk_before_data) {
		chunk(fd, "junk", 3);
		wr(fd, "xyz\0", 4);
	}

	if (!o->omit_data) {
		uint8_t s[2];

		chunk(fd, "data", o->data_size_override ? o->data_size_override : o->n * 2U);
		for (uint32_t i = 0; i < o->n; i++) {
			put16(s, (uint16_t)ref[i]);
			wr(fd, s, 2);
		}
	}
	zassert_ok(wx_host_file_close(fd));
	return path;
}

/* Deterministic block sizes: xorshift. */
static uint32_t rng_state;

static uint32_t rng(void)
{
	rng_state ^= rng_state << 13;
	rng_state ^= rng_state >> 17;
	rng_state ^= rng_state << 5;
	return rng_state;
}

/* Read everything with block sizes from next_block(); returns sample count. */
static size_t read_all(size_t (*next_block)(void))
{
	size_t total = 0;

	zassert_ok(hal_audio_in_start());
	for (;;) {
		size_t want = next_block();
		int n;

		zassert_true(total + want <= ARRAY_SIZE(got));
		n = hal_audio_in_read(&got[total], want, 0);
		zassert_true(n >= 0, "read failed: %d", n);
		zassert_true((size_t)n <= want);
		if (n == 0) {
			break;
		}
		total += (size_t)n;
	}
	zassert_equal(hal_audio_in_read(got, 1, 0), 0, "stays at end of stream");
	return total;
}

static size_t fixed_block;
static size_t block_fixed(void)
{
	return fixed_block;
}
static size_t block_random(void)
{
	return 1U + rng() % 512U;
}

static void before(void *f)
{
	ARG_UNUSED(f);
	for (int i = 0; i < N_SAMPLES; i++) {
		/* Covers the full int16 range including both extremes. */
		ref[i] = (int16_t)(i * 7919 - 32768);
	}
	ref[1] = INT16_MIN;
	ref[2] = INT16_MAX;
	audio_in_fake_close();
}

ZTEST_SUITE(audio_in_fake, NULL, NULL, before, NULL, NULL);

ZTEST(audio_in_fake, test_header_fields)
{
	zassert_ok(audio_in_fake_open(write_wav("std", &std_opts)));
	zassert_equal(audio_in_fake_sample_rate(), 10417);
	zassert_equal(audio_in_fake_total_samples(), N_SAMPLES);
}

ZTEST(audio_in_fake, test_any_fixed_block_size_gives_same_samples)
{
	static const size_t sizes[] = {1, 2, 3, 7, 20, 160, 511, 512, 4999, 5000};

	write_wav("std", &std_opts);
	for (size_t i = 0; i < ARRAY_SIZE(sizes); i++) {
		zassert_ok(audio_in_fake_open(path));
		fixed_block = sizes[i];
		memset(got, 0, sizeof(got));
		zassert_equal(read_all(block_fixed), N_SAMPLES, "block %zu", sizes[i]);
		zassert_mem_equal(got, ref, sizeof(ref), "block %zu", sizes[i]);
	}
}

ZTEST(audio_in_fake, test_random_block_sizes_give_same_samples)
{
	write_wav("std", &std_opts);
	for (uint32_t seed = 1; seed <= 20; seed++) {
		rng_state = seed;
		zassert_ok(audio_in_fake_open(path));
		memset(got, 0, sizeof(got));
		zassert_equal(read_all(block_random), N_SAMPLES);
		zassert_mem_equal(got, ref, sizeof(ref), "seed %u", seed);
	}
}

ZTEST(audio_in_fake, test_skips_unknown_and_odd_chunks)
{
	struct wav_opts o = std_opts;

	o.list_before_fmt = true;
	o.junk_before_data = true;
	o.fmt_extra = true;
	zassert_ok(audio_in_fake_open(write_wav("chunks", &o)));
	fixed_block = 160;
	zassert_equal(read_all(block_fixed), N_SAMPLES);
	zassert_mem_equal(got, ref, sizeof(ref));
}

ZTEST(audio_in_fake, test_truncated_data_stops_at_eof)
{
	struct wav_opts o = std_opts;

	o.data_size_override = 0xFFFFFFFFU; /* streaming writer */
	zassert_ok(audio_in_fake_open(write_wav("stream", &o)));
	fixed_block = 333;
	zassert_equal(read_all(block_fixed), N_SAMPLES);
	zassert_mem_equal(got, ref, sizeof(ref));
}

ZTEST(audio_in_fake, test_rejects_bad_files)
{
	struct wav_opts o;
	int fd;

	zassert_equal(audio_in_fake_open(TEST_TMP_DIR "/does-not-exist.wav"), -ENOENT);

	snprintf(path, sizeof(path), "%s/notwav.wav", TEST_TMP_DIR);
	fd = wx_host_file_open_write(path);
	zassert_true(fd >= 0);
	wr(fd, "RIFX....WAVEfmt ", 16);
	zassert_ok(wx_host_file_close(fd));
	zassert_equal(audio_in_fake_open(path), -EINVAL);

	o = std_opts;
	o.channels = 2;
	zassert_equal(audio_in_fake_open(write_wav("stereo", &o)), -ENOTSUP);

	o = std_opts;
	o.bits = 8;
	zassert_equal(audio_in_fake_open(write_wav("8bit", &o)), -ENOTSUP);

	o = std_opts;
	o.format = 3; /* IEEE float */
	zassert_equal(audio_in_fake_open(write_wav("float", &o)), -ENOTSUP);

	o = std_opts;
	o.omit_data = true;
	zassert_equal(audio_in_fake_open(write_wav("nodata", &o)), -EINVAL);
}

ZTEST(audio_in_fake, test_read_requires_open_and_start)
{
	int16_t x;

	zassert_equal(hal_audio_in_start(), -ENODEV);
	zassert_equal(hal_audio_in_read(&x, 1, 0), -EINVAL);
	zassert_ok(audio_in_fake_open(write_wav("std", &std_opts)));
	zassert_equal(hal_audio_in_read(&x, 1, 0), -EINVAL, "not started");
	zassert_ok(hal_audio_in_start());
	zassert_equal(hal_audio_in_read(&x, 0, 0), -EINVAL, "zero-length read");
	zassert_equal(hal_audio_in_read(&x, 1, 0), 1);
	zassert_equal(x, ref[0]);
	zassert_ok(hal_audio_in_stop());
	zassert_equal(hal_audio_in_read(&x, 1, 0), -EINVAL, "stopped");
	zassert_equal(hal_audio_in_dropped(), 0);
}
