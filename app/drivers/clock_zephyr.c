/*
 * hal/clock.h on Zephyr. One delayable work item fires at the earliest due
 * timer; the list is kept sorted by due time, ties in start order, as in
 * fakes/clock_fake.c.
 */

#include <errno.h>

#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/spinlock.h>

#if defined(CONFIG_CLOCK_CONTROL_NRF)
#include <zephyr/drivers/clock_control/nrf_clock_control.h>
#include <zephyr/sys/onoff.h>
#endif

#include "drivers/clock_zephyr.h"
#include "hal/clock.h"

K_THREAD_STACK_DEFINE(supervisor_stack, CONFIG_WX_DRV_CLOCK_STACK_SIZE);

static struct k_work_q supervisor;
static struct k_work_delayable dispatch_work;
static struct k_spinlock lock;
static struct hal_clock_timer *head;
static struct k_mutex *app_lock;
static int64_t utc_base_ms;
static bool utc_set;

static void unlink_timer(struct hal_clock_timer *timer)
{
	struct hal_clock_timer **pp = &head;

	while (*pp != NULL) {
		if (*pp == timer) {
			*pp = timer->next;
			break;
		}
		pp = &(*pp)->next;
	}
	timer->next = NULL;
}

static void insert_timer(struct hal_clock_timer *timer)
{
	struct hal_clock_timer **pp = &head;

	while (*pp != NULL && (*pp)->due_ms <= timer->due_ms) {
		pp = &(*pp)->next;
	}
	timer->next = *pp;
	*pp = timer;
}

/* Arm the work item for the earliest timer. */
static void reschedule(void)
{
	k_spinlock_key_t key = k_spin_lock(&lock);
	int64_t due = head != NULL ? head->due_ms : -1;

	k_spin_unlock(&lock, key);
	if (due < 0) {
		(void)k_work_cancel_delayable(&dispatch_work);
		return;
	}
	due -= k_uptime_get();
	(void)k_work_reschedule_for_queue(&supervisor, &dispatch_work, K_MSEC(due > 0 ? due : 0));
}

static void dispatch(struct k_work *work)
{
	int64_t now = k_uptime_get();

	ARG_UNUSED(work);
	if (app_lock != NULL) {
		(void)k_mutex_lock(app_lock, K_FOREVER);
	}
	for (;;) {
		k_spinlock_key_t key = k_spin_lock(&lock);
		struct hal_clock_timer *t = head;

		if (t == NULL || t->due_ms > now) {
			k_spin_unlock(&lock, key);
			break;
		}
		head = t->next;
		t->next = NULL;
		if (t->period_ms != 0U) {
			t->due_ms += t->period_ms;
			insert_timer(t); /* before the callback, so it may stop itself */
		} else {
			t->active = false;
		}
		k_spin_unlock(&lock, key);
		t->cb(t, t->user);
	}
	if (app_lock != NULL) {
		(void)k_mutex_unlock(app_lock);
	}
	reschedule();
}

static int supervisor_init(void)
{
	k_work_queue_init(&supervisor);
	k_work_queue_start(&supervisor, supervisor_stack, K_THREAD_STACK_SIZEOF(supervisor_stack),
			   CONFIG_WX_DRV_CLOCK_PRIORITY, NULL);
	k_thread_name_set(k_work_queue_thread_get(&supervisor), "wx_supervisor");
	k_work_init_delayable(&dispatch_work, dispatch);
	return 0;
}

SYS_INIT(supervisor_init, POST_KERNEL, CONFIG_APPLICATION_INIT_PRIORITY);

void clock_zephyr_set_lock(struct k_mutex *lock_)
{
	app_lock = lock_;
}

struct k_work_q *clock_zephyr_queue(void)
{
	return &supervisor;
}

int64_t hal_clock_uptime_ms(void)
{
	return k_uptime_get();
}

int64_t hal_clock_utc_s(void)
{
	int64_t ms;

	if (!utc_set) {
		return -1;
	}
	ms = utc_base_ms + k_uptime_get();
	return ms >= 0 ? ms / 1000 : -1;
}

int hal_clock_set_utc(int64_t utc_s)
{
	if (utc_s < 0) {
		return -EINVAL;
	}
	utc_base_ms = utc_s * 1000 - k_uptime_get();
	utc_set = true;
	return 0;
}

int hal_clock_adjust_utc(int32_t delta_s)
{
	if (!utc_set) {
		return -EINVAL;
	}
	utc_base_ms += (int64_t)delta_s * 1000;
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
	k_spinlock_key_t key;

	if (timer->cb == NULL) {
		return -EINVAL;
	}
	key = k_spin_lock(&lock);
	if (timer->active) {
		unlink_timer(timer);
	}
	timer->due_ms = k_uptime_get() + delay_ms;
	timer->period_ms = period_ms;
	timer->active = true;
	insert_timer(timer);
	k_spin_unlock(&lock, key);
	reschedule();
	return 0;
}

void hal_clock_timer_stop(struct hal_clock_timer *timer)
{
	k_spinlock_key_t key = k_spin_lock(&lock);

	if (timer->active) {
		unlink_timer(timer);
		timer->active = false;
	}
	k_spin_unlock(&lock, key);
}

#if defined(CONFIG_CLOCK_CONTROL_NRF)
static struct onoff_client hfxo_client;

int hal_clock_hfxo_request(void)
{
	struct onoff_manager *mgr = z_nrf_clock_control_get_onoff(CLOCK_CONTROL_NRF_SUBSYS_HF);

	sys_notify_init_spinwait(&hfxo_client.notify);
	return onoff_request(mgr, &hfxo_client) >= 0 ? 0 : -EIO;
}

int hal_clock_hfxo_release(void)
{
	struct onoff_manager *mgr = z_nrf_clock_control_get_onoff(CLOCK_CONTROL_NRF_SUBSYS_HF);

	return onoff_release(mgr) >= 0 ? 0 : -EIO;
}
#else
/* No separate high-frequency crystal to manage (native_sim). */
int hal_clock_hfxo_request(void)
{
	return 0;
}

int hal_clock_hfxo_release(void)
{
	return 0;
}
#endif
