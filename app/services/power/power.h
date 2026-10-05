/*
 * Power manager: clocks for now (spec: Power). Modes, amplifier and
 * backlight arrive with the radio UI.
 *
 * The 32 MHz crystal runs only while some client needs it: a Bluetooth
 * window is open or a phone is connected. Clients are bits, so a repeated
 * request or release from one client is harmless.
 */

#ifndef SERVICES_POWER_POWER_H_
#define SERVICES_POWER_POWER_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum power_hfxo_client {
	POWER_HFXO_BLE_WINDOW = 1U << 0, /* connect or pairing window open */
	POWER_HFXO_BLE_CONN = 1U << 1,   /* a phone is connected */
};

/** Crystal off, no clients (boot). */
void power_init(void);

void power_hfxo_request(uint32_t client);
void power_hfxo_release(uint32_t client);

/** Whether any client holds the crystal. */
bool power_hfxo_held(void);

#ifdef __cplusplus
}
#endif

#endif /* SERVICES_POWER_POWER_H_ */
