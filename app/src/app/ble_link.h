/*
 * The application side of the Bluetooth settings service: values from and
 * to the settings, alert log, alert manager and health supervisor (struct
 * ble_app_ops over struct radio).
 */

#ifndef APP_BLE_LINK_H_
#define APP_BLE_LINK_H_

#include "services/ble/ble_service.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Operations for ble_init(); user is the struct radio. */
extern const struct ble_app_ops ble_link_ops;

#ifdef __cplusplus
}
#endif

#endif /* APP_BLE_LINK_H_ */
