/*
 * Pocket WX Radio: the composition root. Wires the application to the
 * target's real drivers and stand-in fakes (chosen by Kconfig) and runs it.
 * No application logic lives here.
 *
 * Threads:
 *   - main: the decoder (radio_pump_audio), highest application priority;
 *   - supervisor: hal/clock.h timers (drivers/clock_zephyr.c);
 *   - system work queue: low-priority work every second, the UI step
 *     (draw and flush the display, outside the app lock), Bluetooth port
 *     operations;
 *   - Bluetooth host: GATT and connection callbacks.
 * One mutex, the app lock, serialises everything that touches application
 * state.
 *
 * With the embedded clip (WX_AUDIO_CLIP) the decoder first runs through the
 * clip as fast as the CPU allows between two markers, the Renode benchmark,
 * printing each decoded header and EOM; then it carries on with silence in
 * real time, as the radio would between broadcasts.
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include <app_version.h>

#include "app/radio.h"
#include "hal/audio_in.h"
#include "services/same/same_decoder.h"

#if defined(CONFIG_WX_DRV_CLOCK)
#include "drivers/clock_zephyr.h"
#endif
#if defined(CONFIG_WX_FAKE_ALERT_OUT)
#include "fakes/alert_out_fake.h"
#endif
#if defined(CONFIG_WX_FAKE_AUDIO_OUT)
#include "fakes/audio_out_fake.h"
#endif
#if defined(CONFIG_WX_FAKE_BATTERY)
#include "fakes/battery_fake.h"
#endif
#if defined(CONFIG_WX_FAKE_DISPLAY)
#include "fakes/display_fake.h"
#endif
#if defined(CONFIG_WX_FAKE_INPUT)
#include "fakes/input_fake.h"
#endif
#if defined(CONFIG_WX_FAKE_STORAGE)
#include "fakes/storage_fake.h"
#endif
#if defined(CONFIG_WX_FAKE_TUNER)
#include "fakes/tuner_fake.h"
#endif
#if defined(CONFIG_WX_FAKE_WATCHDOG)
#include "fakes/watchdog_fake.h"
#endif
#if defined(CONFIG_WX_BLE_ZEPHYR)
#include "services/ble/ble_zephyr.h"
#endif
#if defined(CONFIG_THREAD_ANALYZER)
#include <zephyr/debug/thread_analyzer.h>
#endif

/* The year floor for SAME issue times (clock_sync): this release's date, 2026-10-01. */
#define FIRMWARE_EPOCH_UTC 1790812800LL

#define LOW_PRIORITY_MS 1000
#define IDLE_STEP_MS    100

static struct radio radio;
K_MUTEX_DEFINE(app_lock);

static void lock(void)
{
	(void)k_mutex_lock(&app_lock, K_FOREVER);
}

static void unlock(void)
{
	(void)k_mutex_unlock(&app_lock);
}

/* ---- Decoder trace on the console ---- */

static void trace_header(void *user, const struct same_header *h)
{
	ARG_UNUSED(user);
	printk("WX HEADER %s\n", h->raw);
}

static void trace_eom(void *user)
{
	ARG_UNUSED(user);
	printk("WX EOM\n");
}

static const struct radio_trace trace = {.header = trace_header, .eom = trace_eom};

/* ---- Low-priority work: the alert log, the clock floor, notifications ---- */

static void low_priority(struct k_work *work)
{
	lock();
	radio_low_priority(&radio);
	unlock();
	(void)k_work_schedule(k_work_delayable_from_work(work), K_MSEC(LOW_PRIORITY_MS));
}

static K_WORK_DELAYABLE_DEFINE(low_priority_work, low_priority);

/* ---- The display: drawn and flushed without the app lock ---- */

#define UI_STEP_MS 1000

static void lock_cb(void *user)
{
	ARG_UNUSED(user);
	lock();
}

static void unlock_cb(void *user)
{
	ARG_UNUSED(user);
	unlock();
}

static const struct radio_lock ui_lock = {.lock = lock_cb, .unlock = unlock_cb};

static void ui_step(struct k_work *work)
{
	radio_ui_tick(&radio, &ui_lock);
	(void)k_work_schedule(k_work_delayable_from_work(work), K_MSEC(UI_STEP_MS));
}

static K_WORK_DELAYABLE_DEFINE(ui_work, ui_step);

/* ---- Bluetooth ---- */

#if defined(CONFIG_WX_BLE_ZEPHYR)
#define BT_START_STACK 2048
K_THREAD_STACK_DEFINE(bt_start_stack, BT_START_STACK);
static struct k_thread bt_start_thread;

/* Its own thread: alerts never wait for the controller to come up. */
static void bt_start(void *a, void *b, void *c)
{
	int err;

	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);
	ble_zephyr_set_lock(&app_lock);
	err = ble_zephyr_init(&radio.ble);
	printk("WX BLE %s (%d)\n", err == 0 ? "ready" : "FAILED", err);
}
#endif

/* ---- Stand-ins ---- */

#if defined(CONFIG_WX_FAKE_WATCHDOG)
static void watchdog_expired(void *user)
{
	ARG_UNUSED(user);
	printk("WX WATCHDOG EXPIRED\n");
}
#endif

static void stand_ins_init(void)
{
#if defined(CONFIG_WX_FAKE_ALERT_OUT)
	alert_out_fake_init();
#endif
#if defined(CONFIG_WX_FAKE_AUDIO_OUT)
	audio_out_fake_init();
#endif
#if defined(CONFIG_WX_FAKE_BATTERY)
	battery_fake_init();
#endif
#if defined(CONFIG_WX_FAKE_DISPLAY)
	display_fake_init();
#endif
#if defined(CONFIG_WX_FAKE_INPUT)
	input_fake_init();
#endif
#if defined(CONFIG_WX_FAKE_STORAGE)
	storage_fake_init();
#endif
#if defined(CONFIG_WX_FAKE_TUNER)
	tuner_fake_init();
#endif
#if defined(CONFIG_WX_FAKE_WATCHDOG)
	watchdog_fake_init(watchdog_expired, NULL);
#endif
}

/* ---- The decoder ---- */

#if defined(CONFIG_WX_AUDIO_CLIP)
/* Decode the whole clip as fast as possible between two markers. */
static void benchmark(void)
{
	uint32_t samples = 0;
	int64_t start;
	int n;

	printk("WX BENCH START\n");
	start = k_uptime_get();
	do {
		lock();
		n = radio_pump_audio(&radio, HAL_AUDIO_IN_BLOCK_SAMPLES);
		unlock();
		samples += n > 0 ? (uint32_t)n : 0U;
	} while (n > 0);
	printk("WX BENCH END samples=%u ms=%lld\n", samples, k_uptime_get() - start);
#if defined(CONFIG_WX_BLE_ZEPHYR)
	/* Let Bluetooth finish starting, so its threads are in the stack report. */
	(void)k_thread_join(&bt_start_thread, K_SECONDS(5));
#endif
#if defined(CONFIG_THREAD_ANALYZER)
	thread_analyzer_print(0);
#endif
	printk("WX BENCH DONE\n");
}
#endif

int main(void)
{
	static const struct health_config cfg = HEALTH_CONFIG_DEFAULT;

	printk("Pocket WX Radio firmware %s (%s)\n", APP_VERSION_EXTENDED_STRING,
	       CONFIG_BOARD_TARGET);
	stand_ins_init();
#if defined(CONFIG_WX_DRV_CLOCK)
	clock_zephyr_set_lock(&app_lock);
#endif

	lock();
	radio_boot(&radio, &cfg, FIRMWARE_EPOCH_UTC);
	radio_set_trace(&radio, &trace);
#if defined(CONFIG_WX_BLE_ZEPHYR)
	radio_ble_start(&radio, &ble_zephyr_port, NULL);
#endif
	unlock();

#if defined(CONFIG_WX_BLE_ZEPHYR)
	k_thread_create(&bt_start_thread, bt_start_stack, K_THREAD_STACK_SIZEOF(bt_start_stack),
			bt_start, NULL, NULL, NULL, K_LOWEST_APPLICATION_THREAD_PRIO, 0, K_NO_WAIT);
	k_thread_name_set(&bt_start_thread, "wx_bt_start");
#endif
	(void)k_work_schedule(&low_priority_work, K_MSEC(LOW_PRIORITY_MS));
	(void)k_work_schedule(&ui_work, K_NO_WAIT);

#if defined(CONFIG_WX_AUDIO_CLIP)
	benchmark();
#endif
	for (;;) {
		k_msleep(IDLE_STEP_MS);
		lock();
		radio_idle_audio(&radio, (uint32_t)SAME_MS_TO_SAMPLES(IDLE_STEP_MS));
		unlock();
	}
	return 0;
}
