/**
 * @file nvs_config.h
 * @brief NVS-backed config: unit_id, fw_ver, apn, etc. Read at runtime; update via config UART or code.
 */

#ifndef NVS_CONFIG_H
#define NVS_CONFIG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NVS_CONFIG_NAMESPACE   "unit_cfg"
#define NVS_KEY_UNIT_ID       "unit_id"
#define NVS_KEY_FW_VER        "fw_ver"
#define NVS_KEY_APN           "apn"
#define NVS_KEY_SERVER_HOST   "host"
#define NVS_KEY_SERVER_PORT   "port"
#define NVS_KEY_STATUS_REG    "status_reg"

/* WiFi STA credentials (optional – WiFi not started if wifi_ssid is empty). */
#define NVS_KEY_WIFI_SSID     "wifi_ssid"
#define NVS_KEY_WIFI_PASS     "wifi_pass"

/* status_reg bit assignments (persisted in NVS):
 *  - Bit 0 (0x0001): Relay 1 kept active via KEEPOPEN[1] / KEEPOPEN[3] (permanent ON until CLOSE).
 *  - Bit 1 (0x0002): Relay 2 kept active via KEEPOPEN[2] (permanent ON until CLOSE).
 *  Higher bits can be used for mode/flags (see app_main).
 */
#define STATUS_KEEP_RELAY1    0x0001
#define STATUS_KEEP_RELAY2    0x0002

/** Max string length for config values (including null). */
#define NVS_CONFIG_MAX_LEN    64

/**
 * @brief Init NVS config (open namespace). Call after nvs_flash_init().
 * @return true on success.
 */
bool nvs_config_init(void);

/**
 * @brief Get string from NVS; if missing, copy default and return false.
 * @param key NVS key (e.g. NVS_KEY_UNIT_ID)
 * @param buf output buffer
 * @param size buffer size
 * @param default_val default if key not set
 * @return true if key was present in NVS, false if default was used
 */
bool nvs_config_get_string(const char *key, char *buf, size_t size, const char *default_val);

/**
 * @brief Set string in NVS and commit.
 * @return true on success
 */
bool nvs_config_set_string(const char *key, const char *value);

/**
 * @brief If unit_id key is empty, write default_id to NVS so ID can be changed later via config shell.
 * Call once after nvs_config_init() (e.g. from app_main with A7670E_UNIT_ID).
 */
void nvs_config_ensure_unit_id_default(const char *default_id);

/**
 * @brief Get port as int; default_val used if not set or invalid.
 */
int nvs_config_get_port(const char *key, int default_val);

/**
 * @brief Set port (stored as string in NVS).
 */
bool nvs_config_set_port(const char *key, int port);

/**
 * @brief Get binary blob from NVS; if missing, copy default_data and return false.
 * @param key NVS key
 * @param buf output buffer
 * @param size buffer size (in), actual size (out)
 * @param default_data default binary data if key not set
 * @param default_size size of default_data
 * @return true if key was present in NVS, false if default was used
 */
bool nvs_config_get_blob(const char *key, void *buf, size_t *size, const void *default_data, size_t default_size);

/**
 * @brief Set binary blob in NVS and commit.
 * @return true on success
 */
bool nvs_config_set_blob(const char *key, const void *data, size_t size);

/**
 * @brief Get status_reg as uint16_t; default_val used if not set.
 */
uint16_t nvs_config_get_status_reg(uint16_t default_val);

/**
 * @brief Set status_reg (stored as 2-byte blob in NVS).
 */
bool nvs_config_set_status_reg(uint16_t value);

#ifdef __cplusplus
}
#endif

#endif /* NVS_CONFIG_H */
