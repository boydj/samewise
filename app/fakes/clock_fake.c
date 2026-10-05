/*
 * clock fake: simulated uptime, UTC and timers.
 */

#include <errno.h>
#include <stddef.h>

#include "fakes/clock_fake.h"
#include "hal/clock.h"

static struct {
	int64_t now_ms;
	int64_t utc_base_ms; /* UTC in ms at uptime 0 */
	bool utc_set;
	struct hal_clock_timer *head; /* active timers, sorted by due time */
	uint32_t fired;
} s;

static void unlink_timer(struct hal_clock_timer *timer)
{
	struct hal_clock_timer **pp = &s.head;

	while (*pp != NULL) {
		if (*pp == timer) {
			*pp = timer->next;
			break;
		}
		pp = &(*pp)->next;
	}
	timer->next = NULL;
}

/* Insert after any timer due at the same time, so ties fire in start order. */
static void insert_timer(struct hal_clock_timer *timer)
{
	struct hal_clock_timer **pp = &s.head;

	while (*pp != NULL && (*pp)->due_ms <= timer->due_ms) {
		pp = &(*pp)->next;
	}
	timer->next = *pp;
	*pp = timer;
}

void clock_fake_reset(void)
{
	s.now_ms = 0;
	s.utc_base_ms = 0;
	s.utc_set = false;
	s.fired = 0;
	while (s.head != NULL) {
		struct hal_clock_timer *t = s.head;

		s.head = t->next;
		t->next = NULL;
		t->active = false;
	}
}

void clock_fake_advance_ms(int64_t ms)
{
	int64_t target = s.now_ms + (ms > 0 ? ms : 0);

	while (s.head != NULL && s.head->due_ms <= target) {
		struct hal_clock_timer *t = s.head;

		s.head = t->next;
		t->next = NULL;
		if (t->due_ms > s.now_ms) {
			s.now_ms = t->due_ms;
		}
		if (t->period_ms != 0U) {
			t->due_ms += t->period_ms;
			insert_timer(t); /* before the callback, so it may stop itself */
		} else {
			t->active = false;
		}
		s.fired++;
		t->cb(t, t->user);
	}
	s.now_ms = target;
}

uint32_t clock_fake_fired_count(void)
{
	return s.fired;
}

int64_t hal_clock_uptime_ms(void)
{
	return s.now_ms;
}

int64_t hal_clock_utc_s(void)
{
	int64_t ms;

	if (!s.utc_set) {
		return -1;
	}
	ms = s.utc_base_ms + s.now_ms;
	return ms >= 0 ? ms / 1000 : -1;
}

int hal_clock_set_utc(int64_t utc_s)
{
	if (utc_s < 0) {
		return -EINVAL;
	}
	s.utc_base_ms = utc_s * 1000 - s.now_ms;
	s.utc_set = true;
	return 0;
}

int hal_clock_adjust_utc(int32_t delta_s)
{
	if (!s.utc_set) {
		return -EINVAL;
	}
	s.utc_base_ms += (int64_t)delta_s * 1000;
	return 0;
}

void hal_clock_timer_init(struct hal_clock_timer *timer, hal_clock_timer_cb cb, void *user)
{
	timer->cb = cb;
	timer->user = user;
	timer->due_ms = 0;
	timer->period_ms = 0;
	timer->active = false;
	timer->next = NULL;
}

int hal_clock_timer_start(struct hal_clock_timer *timer, uint32_t delay_ms, uint32_t period_ms)
{
	if (timer->cb == NULL) {
		return -EINVAL;
	}
	if (timer->active) {
		unlink_timer(timer);
	}
	timer->due_ms = s.now_ms + delay_ms;
	timer->period_ms = period_ms;
	timer->active = true;
	insert_timer(timer);
	return 0;
}

void hal_clock_timer_stop(struct hal_clock_timer *timer)
{
	if (timer->active) {
		unlink_timer(timer);
		timer->active = false;
	}
}
