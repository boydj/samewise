#include <errno.h>
#include <string.h>
#include <zephyr/sys/util.h>

#include "decode_util.h"
#include "fakes/audio_in_fake.h"
#include "hal/audio_in.h"

static struct same_decoder decoder;
static int16_t block_buf[4096];

static void on_header(const struct same_header *h, void *user)
{
	struct decode_result *r = user;

	if (r->n_headers < DECODE_MAX_HEADERS) {
		strcpy(r->headers[r->n_headers], h->raw);
		r->parsed[r->n_headers] = *h;
	}
	r->n_headers++;
	if (r->n_events < DECODE_MAX_EVENTS) {
		r->events[r->n_events++] = 'H';
	}
}

static void on_eom(void *user)
{
	struct decode_result *r = user;

	r->n_eoms++;
	if (r->n_events < DECODE_MAX_EVENTS) {
		r->events[r->n_events++] = 'E';
	}
}

size_t block_fixed(void *ctx)
{
	return *(size_t *)ctx;
}

size_t block_random(void *ctx)
{
	uint32_t *s = ctx;

	*s ^= *s << 13;
	*s ^= *s >> 17;
	*s ^= *s << 5;
	return 1U + *s % 512U;
}

int decode_wav(const char *path, block_size_fn block, void *ctx, bool flush,
	       struct decode_result *r)
{
	int err;

	memset(r, 0, sizeof(*r));
	same_init(&decoder, on_header, on_eom, r);

	err = audio_in_fake_open(path);
	if (err != 0) {
		return err;
	}
	if (audio_in_fake_sample_rate() != 10417U) {
		return -EINVAL; /* the decoder needs 16 MHz / 1536 */
	}
	err = hal_audio_in_start();
	if (err != 0) {
		return err;
	}
	for (;;) {
		size_t want = block(ctx);
		int n = hal_audio_in_read(block_buf, want < ARRAY_SIZE(block_buf) ? want :
					  ARRAY_SIZE(block_buf), 0);

		if (n < 0) {
			return n;
		}
		if (n == 0) {
			break;
		}
		same_feed(&decoder, block_buf, (size_t)n);
		r->samples += (uint32_t)n;
	}
	(void)hal_audio_in_stop();
	audio_in_fake_close();
	if (flush) {
		same_flush(&decoder);
	}
	r->stats = *same_get_stats(&decoder);
	return 0;
}

struct same_decoder *decode_decoder(void)
{
	return &decoder;
}

void decode_silence(uint32_t ms, struct decode_result *r)
{
	uint32_t n = SAME_MS_TO_SAMPLES(ms);

	memset(block_buf, 0, sizeof(block_buf));
	decoder.user = r;
	while (n > 0U) {
		uint32_t chunk = n < ARRAY_SIZE(block_buf) ? n : ARRAY_SIZE(block_buf);

		same_feed(&decoder, block_buf, chunk);
		n -= chunk;
	}
	r->stats = *same_get_stats(&decoder);
}
