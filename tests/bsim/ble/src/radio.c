/*
 * Device 0: the radio. The whole application on the native fakes, with
 * the Bluetooth service on Zephyr's real host. Fake time follows the
 * simulation's time in 10 ms steps, with healthy heartbeats, so windows,
 * confirmations and battery polls run as on the board.
 *
 * It serves the phones' requests (keys, fakes, state) until phone 1 says
 * the test is over.
 */

#include <zephyr/kernel.h>

#include "app/radio.h"
#include "babblekit/testcase.h"
#include "bstests.h"
#include "fakes/alert_out_fake.h"
#include "fakes/audio_out_fake.h"
#include "fakes/battery_fake.h"
#include "fakes/clock_fake.h"
#include "fakes/input_fake.h"
#include "fakes/storage_fake.h"
#include "fakes/tuner_fake.h"
#include "fakes/watchdog_fake.h"
#include "link.h"
#include "services/ble/ble_zephyr.h"
#include "services/same/same_decoder.h"

#define STEP_MS 10
/* 2026-10-01T00:00:00Z */
#define EPOCH 1790812800LL

static struct radio radio;
static uint32_t watchdog_resets;

static void on_watchdog_reset(void *user)
{
	ARG_UNUSED(user);
	watchdog_resets++;
}

static struct link_msg state(void)
{
	const struct ble *b = &radio.ble;
	struct link_msg m = {.cmd = LINK_STATE};

	m.a = (clock_fake_hfxo_on() ? LINK_HFXO : 0U) | (b->connected ? LINK_CONNECTED : 0U) |
	      (b->secure ? LINK_SECURE : 0U) | (b->window != BLE_WINDOW_NONE ? LINK_WINDOW : 0U);
	m.b = radio.ui.ble_screen;
	m.c = (uint32_t)hal_clock_utc_s();
	m.d = radio.ui.ble_screen == BLE_SCREEN_PASSKEY ? radio.ui.passkey : 0U;
	m.e = ble_zephyr_port.bond_count(NULL);
	return m;
}

/* Returns false when the test is over. */
static bool serve(unsigned int dev, const struct link_msg *m)
{
	struct link_msg reply;

	switch (m->cmd) {
	case LINK_KEY:
		input_fake_emit((enum hal_input_event_type)m->a, m->b);
		break;
	case LINK_BATTERY:
		battery_fake_set(m->a, 3800, HAL_BATTERY_DISCHARGING);
		break;
	case LINK_SIGNAL:
		tuner_fake_set_signal((int16_t)m->a, (int16_t)m->b);
		break;
	case LINK_DONE:
		return false;
	default:
		break;
	}
	reply = state();
	link_send(dev, &reply);
	return true;
}

static void test_radio_main(void)
{
	static const struct health_config cfg = HEALTH_CONFIG_DEFAULT;
	bool running = true;
	int err;

	link_init();
	clock_fake_reset();
	storage_fake_init();
	alert_out_fake_init();
	audio_out_fake_init();
	input_fake_init();
	tuner_fake_init();
	battery_fake_init();
	watchdog_fake_init(on_watchdog_reset, NULL);
	radio_boot(&radio, &cfg, EPOCH);
	radio_ble_start(&radio, &ble_zephyr_port, NULL);
	err = ble_zephyr_init(&radio.ble);
	TEST_ASSERT(err == 0, "Bluetooth failed to start (%d)", err);
	TEST_START("radio");

	while (running) {
		struct link_msg m;

		k_sleep(K_MSEC(STEP_MS));
		radio_idle_audio(&radio, (uint32_t)SAME_MS_TO_SAMPLES(STEP_MS));
		clock_fake_advance_ms(STEP_MS);
		radio_low_priority(&radio);
		for (unsigned int d = DEV_PHONE1; d < DEV_COUNT; d++) {
			while (running && link_poll(d, &m)) {
				running = serve(d, &m);
			}
		}
	}

	TEST_ASSERT(watchdog_resets == 0, "the radio reset %u times", watchdog_resets);
	TEST_ASSERT(clock_fake_hfxo_misuse() == 0, "crystal requests unbalanced");
	TEST_ASSERT(alert_mgr_state(&radio.alerts) != ALERT_STATE_LISTENING, "left standby");
	TEST_PASS("radio");
}

static const struct bst_test_instance test_radio[] = {
	{
		.test_id = "radio",
		.test_descr = "The radio: settings service, policy and fakes",
		.test_main_f = test_radio_main,
	},
	BSTEST_END_MARKER,
};

struct bst_test_list *test_radio_install(struct bst_test_list *tests)
{
	return bst_add_tests(tests, test_radio);
}
