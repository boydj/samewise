/*
 * clock fake: simulated uptime, UTC and timers.
 */

#include <zephyr/ztest.h>

#include "fakes/clock_fake.h"
#include "hal/clock.h"

#define MAX_LOG 64

static struct {
	int64_t at[MAX_LOG];
	int id[MAX_LOG];
	int n;
} log;

static void record(struct hal_clock_timer *t, void *user)
{
	ARG_UNUSED(t);
	if (log.n < MAX_LOG) {
		log.at[log.n] = hal_clock_uptime_ms();
		log.id[log.n] = (int)(intptr_t)user;
		log.n++;
	}
}

/* Static, so timers a test leaves running stay valid for the next reset. */
static struct hal_clock_timer t, a, b, c, minute;

static void before(void *f)
{
	ARG_UNUSED(f);
	clock_fake_reset();
	memset(&log, 0, sizeof(log));
}

ZTEST_SUITE(clock_fake, NULL, NULL, before, NULL, NULL);

ZTEST(clock_fake, test_uptime_advances_only_when_told)
{
	zassert_equal(hal_clock_uptime_ms(), 0);
	clock_fake_advance_ms(1234);
	zassert_equal(hal_clock_uptime_ms(), 1234);
	clock_fake_advance_ms(-5);
	zassert_equal(hal_clock_uptime_ms(), 1234, "time must not go backwards");
}

ZTEST(clock_fake, test_utc_set_and_adjust)
{
	zassert_equal(hal_clock_utc_s(), -1, "UTC unset after reset");
	zassert_equal(hal_clock_adjust_utc(10), -EINVAL);

	clock_fake_advance_ms(500);
	zassert_ok(hal_clock_set_utc(1790000000));
	zassert_equal(hal_clock_utc_s(), 1790000000);
	clock_fake_advance_ms(1500);
	zassert_equal(hal_clock_utc_s(), 1790000001);
	zassert_ok(hal_clock_adjust_utc(-60));
	zassert_equal(hal_clock_utc_s(), 1789999941);
	zassert_equal(hal_clock_set_utc(-1), -EINVAL);
}

ZTEST(clock_fake, test_one_shot_fires_once_at_due_time)
{
	hal_clock_timer_init(&t, record, (void *)1);
	zassert_ok(hal_clock_timer_start(&t, 100, 0));
	clock_fake_advance_ms(99);
	zassert_equal(log.n, 0);
	clock_fake_advance_ms(1000);
	zassert_equal(log.n, 1);
	zassert_equal(log.at[0], 100, "callback sees its due time");
	zassert_equal(hal_clock_uptime_ms(), 1099);
	zassert_false(t.active);
}

ZTEST(clock_fake, test_periodic_fires_on_schedule)
{
	hal_clock_timer_init(&t, record, (void *)1);
	zassert_ok(hal_clock_timer_start(&t, 10, 25));
	clock_fake_advance_ms(100);
	zassert_equal(log.n, 4); /* 10, 35, 60, 85 */
	for (int i = 0; i < 4; i++) {
		zassert_equal(log.at[i], 10 + 25 * i);
	}
}

ZTEST(clock_fake, test_ordering_by_due_time_then_start_order)
{
	hal_clock_timer_init(&a, record, (void *)1);
	hal_clock_timer_init(&b, record, (void *)2);
	hal_clock_timer_init(&c, record, (void *)3);
	zassert_ok(hal_clock_timer_start(&a, 50, 0));
	zassert_ok(hal_clock_timer_start(&b, 20, 0));
	zassert_ok(hal_clock_timer_start(&c, 50, 0));
	clock_fake_advance_ms(60);
	zassert_equal(log.n, 3);
	zassert_equal(log.id[0], 2);
	zassert_equal(log.id[1], 1);
	zassert_equal(log.id[2], 3);
}

static int self_stop_count;

static void stop_self_after_three(struct hal_clock_timer *t, void *user)
{
	ARG_UNUSED(user);
	if (++self_stop_count == 3) {
		hal_clock_timer_stop(t);
	}
}

ZTEST(clock_fake, test_stop_from_own_callback)
{
	self_stop_count = 0;
	hal_clock_timer_init(&t, stop_self_after_three, NULL);
	zassert_ok(hal_clock_timer_start(&t, 1, 1));
	clock_fake_advance_ms(100);
	zassert_equal(self_stop_count, 3);
	zassert_false(t.active);
}

ZTEST(clock_fake, test_restart_replaces_schedule)
{
	hal_clock_timer_init(&t, record, (void *)1);
	zassert_ok(hal_clock_timer_start(&t, 10, 0));
	clock_fake_advance_ms(5);
	zassert_ok(hal_clock_timer_start(&t, 10, 0));
	clock_fake_advance_ms(9);
	zassert_equal(log.n, 0);
	clock_fake_advance_ms(1);
	zassert_equal(log.n, 1);
	zassert_equal(log.at[0], 15);
}

ZTEST(clock_fake, test_week_runs_faster_than_real_time)
{
	const int64_t week_ms = 7LL * 24 * 3600 * 1000;

	hal_clock_timer_init(&minute, record, (void *)1);
	zassert_ok(hal_clock_timer_start(&minute, 60000, 60000));
	clock_fake_advance_ms(week_ms);
	zassert_equal(clock_fake_fired_count(), 7 * 24 * 60);
	zassert_equal(hal_clock_uptime_ms(), week_ms);
}
