/*
 * The radio's composition root: settings, screen model, SAME decoder,
 * alert manager and health supervisor wired together.
 *
 * Execution contexts (threads on the board, steps of the scenario runner on
 * native_sim):
 *   - decoder thread: radio_pump_audio() reads hal/audio_in.h and decodes;
 *   - supervisor: hal/clock.h timers inside the alert manager and health;
 *   - low priority: radio_low_priority() writes the alert log and clock
 *     floor, and sends Bluetooth notifications;
 *   - Bluetooth host: the service's GATT callbacks and stack events.
 */

#ifndef APP_RADIO_H_
#define APP_RADIO_H_

#include <stddef.h>
#include <stdint.h>

#include "app/alert_manager.h"
#include "app/health.h"
#include "app/settings.h"
#include "app/ui_model.h"
#include "services/ble/ble_service.h"
#include "services/same/same_decoder.h"

#ifdef __cplusplus
extern "C" {
#endif

struct radio {
	struct settings settings;
	struct ui_model ui;
	struct alert_mgr alerts;
	struct health health;
	struct same_decoder decoder;
	struct ble ble;
	uint32_t idle_samples; /* accounted by radio_idle_audio(), never decoded */
	uint32_t settings_errors;
};

/**
 * Cold or warm boot: load settings, start the alert manager and the health
 * supervisor (which starts the watchdog), and tune the weather channel.
 */
void radio_boot(struct radio *r, const struct health_config *cfg, int64_t firmware_epoch_utc);

/**
 * Start the Bluetooth service on a stack port (ble_zephyr.c on hardware and
 * bsim, a fake in tests). Advertising stays off until a window opens.
 */
void radio_ble_start(struct radio *r, const struct ble_port *port, void *port_user);

/**
 * Decoder thread step: read up to max samples from hal/audio_in.h and
 * decode them. Returns the samples read (0 at end of a fake's WAV file).
 */
int radio_pump_audio(struct radio *r, size_t max);

/**
 * Accelerated idle: report n samples of audio as received and decoded
 * without decoding them (silence), so a simulated week takes seconds.
 */
void radio_idle_audio(struct radio *r, uint32_t n);

/**
 * Low-priority work: write queued alert log entries and the clock floor,
 * then notify Bluetooth of new log entries and Status changes.
 */
void radio_low_priority(struct radio *r);

/** Frequency in kHz for a weather channel 1-7. */
uint32_t radio_weather_khz(uint8_t channel);

#ifdef __cplusplus
}
#endif

#endif /* APP_RADIO_H_ */
