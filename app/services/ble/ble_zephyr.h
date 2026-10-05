/*
 * Binding of the Bluetooth settings service to Zephyr's Bluetooth host:
 * the GATT service built from services/ble/gatt_table.h (LE Secure
 * Connections permissions on every characteristic), advertising with the
 * filter accept list, display-only passkey pairing, and the stack events
 * forwarded to services/ble/ble_service.h.
 *
 * Only this file includes Zephyr headers in services/ble (spec: Bluetooth
 * is not behind an interface). It runs on nrf52_bsim and the board.
 */

#ifndef SERVICES_BLE_BLE_ZEPHYR_H_
#define SERVICES_BLE_BLE_ZEPHYR_H_

#include "services/ble/ble_service.h"

#ifdef __cplusplus
extern "C" {
#endif

/** The stack port for ble_init(); its user pointer is unused. */
extern const struct ble_port ble_zephyr_port;

/**
 * Enable Bluetooth, load bonds, register the GATT service and route stack
 * events to b. Call after ble_init(b, ..., &ble_zephyr_port, NULL).
 */
int ble_zephyr_init(struct ble *b);

#ifdef __cplusplus
}
#endif

#endif /* SERVICES_BLE_BLE_ZEPHYR_H_ */
