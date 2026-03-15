/**
 * @file ble_gatt_server.h
 * @brief BLE GATT server for app commands. Init first (fast), then modem.
 *        Shares relay_control with modem OPEN/CHECK_USER flow.
 */

#ifndef BLE_GATT_SERVER_H
#define BLE_GATT_SERVER_H

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize and start BLE (controller, Bluedroid, GATT, advertising).
 *        Call once at startup after relay_control_init/start, before modem init.
 * @return ESP_OK on success
 */
esp_err_t ble_gatt_server_init(void);

/**
 * @brief Send notification to connected BLE client (if connected and notify enabled).
 */
esp_err_t ble_send_notification(const char *message, bool is_response);

/**
 * @brief Check if a BLE client is connected.
 */
bool ble_is_connected(void);

#ifdef __cplusplus
}
#endif

#endif /* BLE_GATT_SERVER_H */
