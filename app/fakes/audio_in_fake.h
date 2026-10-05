/*
 * audio_in fake: streams a WAV file through hal/audio_in.h.
 *
 * Accepts RIFF/WAVE, PCM, 16-bit, mono, any sample rate (check it with
 * audio_in_fake_sample_rate()). Unknown chunks are skipped. Reads return
 * whatever block size the caller asks for, and 0 once the file is exhausted.
 */

#ifndef FAKES_AUDIO_IN_FAKE_H_
#define FAKES_AUDIO_IN_FAKE_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Open a WAV file as the audio source, replacing any previous one.
 *
 * @return 0; -ENOENT if it can't be opened; -EINVAL if it is not a valid
 *         WAV file; -ENOTSUP if it is not 16-bit mono PCM.
 */
int audio_in_fake_open(const char *path);

/** Close the source; reads then fail with -EINVAL until the next open. */
void audio_in_fake_close(void);

/** Sample rate from the WAV header, in Hz (0 if nothing is open). */
uint32_t audio_in_fake_sample_rate(void);

/** Samples in the data chunk (0 if nothing is open). */
uint32_t audio_in_fake_total_samples(void);

#ifdef __cplusplus
}
#endif

#endif /* FAKES_AUDIO_IN_FAKE_H_ */
