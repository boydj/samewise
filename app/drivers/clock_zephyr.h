/*
 * hal/clock.h on Zephyr: uptime from the kernel (the nRF RTC on the board),
 * UTC as an offset from it, and hal/clock.h timers dispatched on one work
 * queue thread, the radio's supervisor.
 */

#ifndef DRIVERS_CLOCK_ZEPHYR_H_
#define DRIVERS_CLOCK_ZEPHYR_H_

#include <zephyr/kernel.h>

/**
 * Timer callbacks run holding this mutex (the radio's app lock), so they
 * never race the decoder thread or Bluetooth callbacks. NULL: no lock.
 */
void clock_zephyr_set_lock(struct k_mutex *lock);

/** The supervisor work queue, for other work that must run in its context. */
struct k_work_q *clock_zephyr_queue(void);

#endif /* DRIVERS_CLOCK_ZEPHYR_H_ */
