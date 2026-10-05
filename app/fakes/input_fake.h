/*
 * input fake: tests and the scenario runner emit events directly; the fake
 * tracks the lock switch and headphone detect and calls the registered
 * handler, as the board's input work item would.
 */

#ifndef FAKES_INPUT_FAKE_H_
#define FAKES_INPUT_FAKE_H_

#include <stdint.h>

#include "hal/input.h"

#ifdef __cplusplus
extern "C" {
#endif

/** No handler, unlocked, headphones out. */
void input_fake_init(void);

/** Deliver an event now (keys is a hal_input_key mask, 0 for switches). */
void input_fake_emit(enum hal_input_event_type type, uint32_t keys);

#ifdef __cplusplus
}
#endif

#endif /* FAKES_INPUT_FAKE_H_ */
