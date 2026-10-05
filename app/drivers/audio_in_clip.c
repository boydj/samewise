/*
 * hal/audio_in.h from the SAME clip in flash (tools/vectors/build_clip.py):
 * each start rewinds it; reads return what is left, then 0 at the end.
 * Never blocks, so the decoder can run through the clip as fast as the CPU
 * allows (the Renode benchmark).
 */

#include <errno.h>
#include <stdbool.h>

#include "hal/audio_in.h"
#include "wx_clip.h"

static uint32_t pos;
static bool running;

int hal_audio_in_start(void)
{
	pos = 0;
	running = true;
	return 0;
}

int hal_audio_in_stop(void)
{
	running = false;
	return 0;
}

int hal_audio_in_read(int16_t *buf, size_t max, int32_t timeout_ms)
{
	size_t n = 0;

	(void)timeout_ms;
	if (!running || buf == NULL) {
		return -EINVAL;
	}
	while (n < max && pos < wx_clip_count) {
		buf[n++] = (int16_t)(wx_clip_samples[pos++] * (1 << WX_CLIP_SHIFT));
	}
	return (int)n;
}

uint32_t hal_audio_in_dropped(void)
{
	return 0;
}
