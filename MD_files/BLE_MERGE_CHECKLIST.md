# BLE Project Merge Checklist

## Goal
Extract BLE functionality into `ATcommands_Example` so both BLE (app) and Modem (server) control the same relays.

**Which BLE project to use:** **EG_BLE_server** (non-latching relays, ESP32 WROOM 32E — same board as modem project). See `RELAY_SOURCE_AND_BLE_PROJECTS.md`.

## Current Status ✅
- ✅ `relay_control.c/h` already copied to modem project
- ✅ OPEN102/202/302 from server already wired to `relay_execute_command()`
- ✅ Relay init/start already in `app_main()` (before modem)

## BLE Project Organization Tasks

### 1. **Create BLE Module Files** 📁
   - [ ] Create `main/ble_gatt_server.c` and `main/ble_gatt_server.h`
   - [ ] Extract BLE init code from `ESP32_EG_code_ESP32-WROOM-32E.c`
   - [ ] Extract GATT/GAP event handlers
   - [ ] Extract notification/connection management

### 2. **Dependencies to Copy** 📦
   - [ ] Copy `Encryption.c` and `Encryption.h` from BLE project
   - [ ] Verify `Encryption.c` compiles (may need minor adjustments)

### 3. **BLE API Functions to Expose** 🔌
   ```c
   // In ble_gatt_server.h:
   esp_err_t ble_gatt_server_init(void);
   esp_err_t ble_gatt_server_start(void);
   void ble_gatt_server_stop(void);
   esp_err_t ble_send_notification(const char* message, bool is_response);
   ```

### 4. **Key Code Sections to Extract** 📝

   **From `ESP32_EG_code_ESP32-WROOM-32E.c`:**

   - **BLE Stack Init** (lines ~798-812):
     - `esp_bt_controller_init()`
     - `esp_bluedroid_init()`
     - `esp_ble_gatts_register_callback()`
     - `esp_ble_gap_register_callback()`

   - **GATT Event Handler** (`gatts_event_handler`):
     - `ESP_GATTS_REG_EVT` - service registration
     - `ESP_GATTS_WRITE_EVT` - app writes JSON commands
     - `ESP_GATTS_CONF_EVT` - confirmations
     - `ESP_GATTS_CONNECT_EVT` / `ESP_GATTS_DISCONNECT_EVT`

   - **GAP Event Handler** (`gap_event_handler`):
     - `ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT`
     - `ESP_GAP_BLE_ADV_START_COMPLETE_EVT`
     - Connection management

   - **Notification System**:
     - `notification_task()` - sends queued notifications
     - `start_notification_service()` / `stop_notification_service()`
     - `ble_send_notification()` - public API

   - **Connection Timeout**:
     - `connection_timeout_task()` - auto-disconnect if no message
     - `start_connection_timeout()` / `stop_connection_timeout()`

   - **Encryption Setup** (lines ~817-841):
     - Extract numeric part from unit ID
     - Call `GetEncryptedData()` to generate key
     - Store in `encrypted_data` for command validation

### 5. **Integration Points** 🔗

   **In merged `app_main()`:**
   ```c
   void app_main(void) {
       // 1. NVS init (already done in modem project)
       ESP_ERROR_CHECK(nvs_flash_init());
       
       // 2. Relay init FIRST (shared by BLE + Modem)
       relay_control_init();
       relay_control_start();
       
       // 3. BLE init SECOND (fast, ~100ms)
       ble_gatt_server_init();
       ble_gatt_server_start();
       
       // 4. Modem init THIRD (slower, ~5-10s)
       // ... existing modem init code ...
   }
   ```

### 6. **Command Flow** 🔄

   **BLE App → Relay:**
   ```
   App writes JSON → ESP_GATTS_WRITE_EVT → Validate encryption → 
   relay_parse_command_from_json() → relay_execute_command() → Relay activates
   ```

   **Modem Server → Relay:**
   ```
   Server sends "OPEN302" → +CIPRXGET read → server_handle_incoming_line() → 
   relay_execute_command() → Relay activates
   ```

   **Both paths use the same `relay_execute_command()` API!** ✅

### 7. **Configuration** ⚙️

   - **Unit ID**: BLE project uses NVS `"unit_config"` namespace with key `"unit_id"`
   - **Default**: `"cr16061952"` if not in NVS
   - **Encryption**: Uses numeric part of unit ID as seed
   - **GATT UUIDs**: Service `0xFFE0`, RX `0xFFE1` (write), TX `0xFFE2` (notify)

### 8. **Files to Create** 📄

   ```
   main/
   ├── ble_gatt_server.c      (new - extracted BLE code)
   ├── ble_gatt_server.h       (new - BLE API)
   ├── Encryption.c            (copy from BLE project)
   ├── Encryption.h            (copy from BLE project)
   └── relay_control.c          (already done ✅)
   ```

### 9. **CMakeLists.txt Updates** 🔧

   Add to `REQUIRES`:
   ```
   REQUIRES driver freertos esp_timer nvs_flash json bt
   ```

   Add to `SRCS`:
   ```
   "ble_gatt_server.c" "Encryption.c"
   ```

### 10. **Testing Order** 🧪

   1. ✅ Test relay from modem OPEN (already working)
   2. ⏳ Test BLE init (should start advertising)
   3. ⏳ Test BLE app connection
   4. ⏳ Test BLE app → relay command
   5. ⏳ Test both BLE + Modem simultaneously

## Notes 📝

- **NVS Namespace**: BLE uses `"unit_config"`, modem may use different namespace - verify no conflicts
- **GPIO Conflicts**: BLE project doesn't use UART pins, so no GPIO conflicts expected
- **Task Priorities**: BLE notification task priority 5, relay task priority 5, modem tasks vary
- **Memory**: BLE stack uses ~50KB RAM, ensure sufficient free heap

## Next Steps 🚀

1. Extract BLE code into `ble_gatt_server.c/h`
2. Copy `Encryption.c/h`
3. Update `CMakeLists.txt`
4. Update `app_main()` to call BLE init before modem
5. Test BLE advertising and connection
6. Test BLE → relay command flow
