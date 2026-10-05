/*
 * Input interface: buttons with debounce, long press and combos, the key
 * lock switch and headphone detect. The fake takes keyboard input in the
 * SDL window or a scripted event list.
 */

#ifndef HAL_INPUT_H_
#define HAL_INPUT_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Buttons, as bit flags so combos can be expressed as a mask. */
enum hal_input_key {
	HAL_INPUT_KEY_STBY = 1U << 0,
	HAL_INPUT_KEY_BAND = 1U << 1,
	HAL_INPUT_KEY_TUNE_UP = 1U << 2,
	HAL_INPUT_KEY_TUNE_DOWN = 1U << 3,
	HAL_INPUT_KEY_VOL_UP = 1U << 4,
	HAL_INPUT_KEY_VOL_DOWN = 1U << 5,
};

enum hal_input_event_type {
	HAL_INPUT_PRESS,           /* short press, reported on release */
	HAL_INPUT_LONG_PRESS,      /* held past the long-press time */
	HAL_INPUT_COMBO,           /* two or more keys held together */
	HAL_INPUT_LOCK_ON,         /* key lock switch */
	HAL_INPUT_LOCK_OFF,
	HAL_INPUT_HEADPHONES_IN,   /* headphone detect */
	HAL_INPUT_HEADPHONES_OUT,
};

struct hal_input_event {
	enum hal_input_event_type type;
	uint32_t keys; /* hal_input_key mask; 0 for switch and detect events */
	int64_t uptime_ms;
};

typedef void (*hal_input_cb)(const struct hal_input_event *event, void *user);

/**
 * Register the single event handler. Events are delivered from the input
 * work item, never from an interrupt.
 */
int hal_input_init(hal_input_cb cb, void *user);

/** Current key lock switch position (1 = locked). */
int hal_input_locked(void);

/** Current headphone detect state (1 = headphones in). */
int hal_input_headphones(void);

#ifdef __cplusplus
}
#endif

#endif /* HAL_INPUT_H_ */
