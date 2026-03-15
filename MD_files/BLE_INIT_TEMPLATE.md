# BLE Init Function Template

## Suggested Structure for `ble_gatt_server.h`

```c
#ifndef BLE_GATT_SERVER_H
#define BLE_GATT_SERVER_H

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize BLE GATT server (call once at startup, before modem)
 * @return ESP_OK on success
 */
esp_err_t ble_gatt_server_init(void);

/**
 * @brief Start BLE advertising (call after init)
 * @return ESP_OK on success
 */
esp_err_t ble_gatt_server_start(void);

/**
 * @brief Stop BLE (stop advertising, disconnect clients)
 */
void ble_gatt_server_stop(void);

/**
 * @brief Send notification to connected BLE client
 * @param message Message to send
 * @param is_response true if this is a response to a command
 * @return ESP_OK on success
 */
esp_err_t ble_send_notification(const char* message, bool is_response);

/**
 * @brief Check if BLE client is connected
 * @return true if connected
 */
bool ble_is_connected(void);

#ifdef __cplusplus
}
#endif

#endif // BLE_GATT_SERVER_H
```

## Suggested Structure for `ble_gatt_server.c`

### Key Sections:

1. **Includes & Defines**
   ```c
   #include "ble_gatt_server.h"
   #include "relay_control.h"
   #include "encryption.h"
   #include "esp_bt.h"
   #include "esp_bt_main.h"
   #include "esp_gatts_api.h"
   #include "esp_gap_ble_api.h"
   // ... other includes
   ```

2. **Static Variables** (from BLE project)
   - `service_handle`, `char1_handle`, `char2_handle`
   - `conn_id`, `is_connected`, `gatt_if_for_send`
   - `encrypted_data`, `rnd`, `rnd_ptr`
   - `notification_queue`, `notification_task_handle`
   - `connection_timeout_task_handle`
   - `message_buffer`, `message_index`
   - `pending_relay_cmd`, `relay_cmd_pending`

3. **Init Function**
   ```c
   esp_err_t ble_gatt_server_init(void) {
       // 1. Release classic BT memory
       ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT));
       
       // 2. Init BT controller
       esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
       ESP_ERROR_CHECK(esp_bt_controller_init(&bt_cfg));
       ESP_ERROR_CHECK(esp_bt_controller_enable(ESP_BT_MODE_BLE));
       
       // 3. Init Bluedroid
       ESP_ERROR_CHECK(esp_bluedroid_init());
       ESP_ERROR_CHECK(esp_bluedroid_enable());
       
       // 4. Set MTU
       esp_ble_gatt_set_local_mtu(50);
       
       // 5. Register callbacks
       ESP_ERROR_CHECK(esp_ble_gatts_register_callback(gatts_event_handler));
       ESP_ERROR_CHECK(esp_ble_gap_register_callback(gap_event_handler));
       ESP_ERROR_CHECK(esp_ble_gatts_app_register(0));
       
       // 6. Setup encryption (extract unit ID, generate key)
       // ... encryption setup code ...
       
       // 7. Create notification queue
       notification_queue = xQueueCreate(NOTIFICATION_QUEUE_SIZE, sizeof(notification_msg_t));
       
       return ESP_OK;
   }
   ```

4. **Event Handlers** (copy from BLE project)
   - `gatts_event_handler()` - handles GATT events
   - `gap_event_handler()` - handles GAP events

5. **Helper Functions** (copy from BLE project)
   - `notification_task()` - sends queued notifications
   - `connection_timeout_task()` - auto-disconnect timeout
   - `start_notification_service()` / `stop_notification_service()`
   - `start_connection_timeout()` / `stop_connection_timeout()`
   - `ble_send_notification()` - public API

## Integration in `app_main()`

```c
void app_main(void) {
    // 1. NVS init
    ESP_ERROR_CHECK(nvs_flash_init());
    
    // 2. Relay init (shared by BLE + Modem)
    relay_control_init();
    relay_control_start();
    
    // 3. BLE init (fast, ~100ms)
    if (ble_gatt_server_init() == ESP_OK) {
        ble_gatt_server_start();
        ESP_LOGI("MAIN", "BLE initialized and advertising");
    } else {
        ESP_LOGE("MAIN", "BLE init failed");
    }
    
    // 4. Modem init (slower, ~5-10s)
    // ... existing modem init code ...
}
```

## Key Points

- **Relay is shared**: Both BLE and Modem use `relay_execute_command()`
- **BLE init first**: Fast startup, then modem connects
- **No conflicts**: BLE uses different GPIO pins than modem UART
- **Encryption**: Uses unit ID from NVS (same namespace as modem if needed)
