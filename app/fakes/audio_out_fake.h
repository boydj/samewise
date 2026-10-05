/*
 * audio_out fake: current amplifier and routing state, plus a change count.
 */

#ifndef FAKES_AUDIO_OUT_FAKE_H_
#define FAKES_AUDIO_OUT_FAKE_H_

#include <stdbool.h>
#include <stdint.h>

#include "hal/audio_out.h"

#ifdef __cplusplus
extern "C" {
#endif

void audio_out_fake_init(void);
bool audio_out_fake_amp(void);
enum hal_audio_out_route audio_out_fake_route(void);
/** Uptime of the last amp-on, or -1. */
int64_t audio_out_fake_amp_on_ms(void);
uint32_t audio_out_fake_changes(void);

#ifdef __cplusplus
}
#endif

#endif /* FAKES_AUDIO_OUT_FAKE_H_ */
