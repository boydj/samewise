/*
 * Audio input interface: tuner audio for the SAME decoder.
 *
 * The board samples the tuner's analog output with the SAADC (EasyDMA,
 * timer-triggered, 4x gain, differential). The native_sim fake streams WAV
 * files (fakes/audio_in_fake.h).
 *
 * Audio is mono, signed 16-bit, at 16 MHz / 1536 = 10,416.67 Hz: exactly
 * 20 samples per SAME bit at 520.83 baud.
 */

#ifndef HAL_AUDIO_IN_H_
#define HAL_AUDIO_IN_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Sample rate numerator and denominator: 16,000,000 / 1,536 Hz. */
#define HAL_AUDIO_IN_RATE_NUM 16000000U
#define HAL_AUDIO_IN_RATE_DEN 1536U

/** Sample rate in millihertz (10,416,667 mHz). */
#define HAL_AUDIO_IN_RATE_MHZ                                                                      \
	((uint32_t)((HAL_AUDIO_IN_RATE_NUM * 1000ULL + HAL_AUDIO_IN_RATE_DEN / 2) /                 \
		    HAL_AUDIO_IN_RATE_DEN))

/** Samples per SAME bit at the native rate. */
#define HAL_AUDIO_IN_SAMPLES_PER_BIT 20U

/**
 * Block size the board driver produces (one DMA buffer): 8 SAME bits,
 * 15.4 ms. hal_audio_in_read() accepts any max; this is only the natural
 * granularity.
 */
#define HAL_AUDIO_IN_BLOCK_SAMPLES 160U

/** Start sampling. Samples arriving before the first read are buffered. */
int hal_audio_in_start(void);

/** Stop sampling and discard buffered samples. */
int hal_audio_in_stop(void);

/**
 * Read up to max samples, blocking up to timeout_ms for the first one.
 *
 * @param buf        destination
 * @param max        capacity of buf in samples (any value >= 1)
 * @param timeout_ms 0 to poll, negative to wait forever
 * @return number of samples (> 0); 0 at end of stream (fakes only: the WAV
 *         file is exhausted); -EAGAIN on timeout; -EIO on a driver fault;
 *         -EINVAL if not started.
 */
int hal_audio_in_read(int16_t *buf, size_t max, int32_t timeout_ms);

/**
 * Samples lost because the consumer fell behind, since start. The health
 * supervisor treats a growing count as a stalled decoder.
 */
uint32_t hal_audio_in_dropped(void);

#ifdef __cplusplus
}
#endif

#endif /* HAL_AUDIO_IN_H_ */
