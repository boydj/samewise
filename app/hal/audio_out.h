/*
 * Audio output interface: the TPA6132A2 headphone amplifier and routing of
 * the broadcast audio to it. The fake logs every state change.
 */

#ifndef HAL_AUDIO_OUT_H_
#define HAL_AUDIO_OUT_H_

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

enum hal_audio_out_route {
	HAL_AUDIO_OUT_ROUTE_NONE,  /* nothing to the headphones */
	HAL_AUDIO_OUT_ROUTE_RADIO, /* AM/FM listening */
	HAL_AUDIO_OUT_ROUTE_ALERT, /* weather broadcast during an alert */
};

/**
 * Turn the headphone amplifier on or off. Keep it off unless headphones are
 * in and audio is playing (power budget).
 */
int hal_audio_out_amp(bool on);

/** Select what plays in the headphones. */
int hal_audio_out_route(enum hal_audio_out_route route);

#ifdef __cplusplus
}
#endif

#endif /* HAL_AUDIO_OUT_H_ */
