/*
 * audio_in fake: WAV file reader behind hal/audio_in.h.
 */

#include <errno.h>
#include <stdbool.h>
#include <string.h>

#include "fakes/audio_in_fake.h"
#include "fakes/host_file/host_file.h"
#include "hal/audio_in.h"

#define READ_BUF_SIZE 4096

static struct {
	int fd;
	bool open;
	bool started;
	uint32_t rate;
	uint32_t total;     /* samples in the data chunk */
	uint32_t remaining; /* samples not yet returned */
	uint8_t buf[READ_BUF_SIZE];
	uint32_t buf_len;
	uint32_t buf_pos;
} s = {.fd = -1};

static uint32_t le32(const uint8_t *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
	       ((uint32_t)p[3] << 24);
}

static uint16_t le16(const uint8_t *p)
{
	return (uint16_t)(p[0] | (p[1] << 8));
}

/* Read exactly len bytes straight from the file; false on short read. */
static bool read_exact(void *dst, uint32_t len)
{
	uint8_t *p = dst;

	while (len > 0) {
		long n = wx_host_file_read(s.fd, p, len);

		if (n <= 0) {
			return false;
		}
		p += n;
		len -= (uint32_t)n;
	}
	return true;
}

static bool skip_bytes(uint32_t len)
{
	uint8_t scratch[256];

	while (len > 0) {
		uint32_t chunk = len < sizeof(scratch) ? len : sizeof(scratch);

		if (!read_exact(scratch, chunk)) {
			return false;
		}
		len -= chunk;
	}
	return true;
}

/* Parse the RIFF header up to the start of the data chunk. */
static int parse_header(void)
{
	uint8_t hdr[12];
	uint8_t chunk[8];
	bool have_fmt = false;

	if (!read_exact(hdr, sizeof(hdr)) || memcmp(hdr, "RIFF", 4) != 0 ||
	    memcmp(hdr + 8, "WAVE", 4) != 0) {
		return -EINVAL;
	}

	for (;;) {
		uint32_t size;

		if (!read_exact(chunk, sizeof(chunk))) {
			return -EINVAL; /* no data chunk */
		}
		size = le32(chunk + 4);

		if (memcmp(chunk, "fmt ", 4) == 0) {
			uint8_t fmt[16];

			if (size < sizeof(fmt) || !read_exact(fmt, sizeof(fmt)) ||
			    !skip_bytes(size - sizeof(fmt) + (size & 1U))) {
				return -EINVAL;
			}
			if (le16(fmt) != 1U || le16(fmt + 2) != 1U || le16(fmt + 12) != 2U ||
			    le16(fmt + 14) != 16U) {
				return -ENOTSUP;
			}
			s.rate = le32(fmt + 4);
			have_fmt = true;
		} else if (memcmp(chunk, "data", 4) == 0) {
			if (!have_fmt) {
				return -EINVAL;
			}
			/* Streaming writers may leave the size as 0xFFFFFFFF; reads stop at EOF. */
			s.total = size / 2U;
			s.remaining = s.total;
			return 0;
		} else if (!skip_bytes(size + (size & 1U))) {
			return -EINVAL;
		}
	}
}

int audio_in_fake_open(const char *path)
{
	int err;

	audio_in_fake_close();
	s.fd = wx_host_file_open_read(path);
	if (s.fd < 0) {
		return -ENOENT;
	}
	err = parse_header();
	if (err != 0) {
		audio_in_fake_close();
		return err;
	}
	s.open = true;
	return 0;
}

void audio_in_fake_close(void)
{
	if (s.fd >= 0) {
		(void)wx_host_file_close(s.fd);
	}
	s.fd = -1;
	s.open = false;
	s.started = false;
	s.rate = 0;
	s.total = 0;
	s.remaining = 0;
	s.buf_len = 0;
	s.buf_pos = 0;
}

uint32_t audio_in_fake_sample_rate(void)
{
	return s.rate;
}

uint32_t audio_in_fake_total_samples(void)
{
	return s.total;
}

int hal_audio_in_start(void)
{
	if (!s.open) {
		return -ENODEV;
	}
	s.started = true;
	return 0;
}

int hal_audio_in_stop(void)
{
	s.started = false;
	return 0;
}

int hal_audio_in_read(int16_t *out, size_t max, int32_t timeout_ms)
{
	size_t n = 0;

	(void)timeout_ms; /* file data is always ready */

	if (!s.started || out == NULL || max == 0U) {
		return -EINVAL;
	}

	while (n < max && s.remaining > 0U) {
		if (s.buf_len - s.buf_pos < 2U) {
			/* Keep a stray odd byte, then refill behind it. */
			uint32_t keep = s.buf_len - s.buf_pos;
			long got;

			if (keep != 0U) {
				s.buf[0] = s.buf[s.buf_pos];
			}
			got = wx_host_file_read(s.fd, s.buf + keep, READ_BUF_SIZE - keep);
			if (got < 0) {
				return -EIO;
			}
			s.buf_pos = 0;
			s.buf_len = keep + (uint32_t)got;
			if (got == 0) {
				s.remaining = 0; /* truncated file: stop at EOF */
				break;
			}
			continue;
		}
		out[n++] = (int16_t)le16(&s.buf[s.buf_pos]);
		s.buf_pos += 2U;
		s.remaining--;
	}

	if (n > (size_t)INT32_MAX) {
		return -EINVAL;
	}
	return (int)n;
}

uint32_t hal_audio_in_dropped(void)
{
	return 0;
}
