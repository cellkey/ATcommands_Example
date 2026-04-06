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

/* status_reg (persisted). BLE is always on — not controlled here.
 *
 *  Bits 0–1: relay KEEPOPEN (unchanged).
 *  Bits 4–5: connectivity profile (see nvs_connectivity_mode_t):
 *    0x0000  Modem full — cellular TCP to server, KA on modem; WiFi off.
 *    0x0010  WiFi only — no modem UART/tasks; WiFi STA + TCP to server.
 *    0x0020  Modem slave + WiFi — modem for voice/dial/+CLCC only; server (KA, CHECK_USER, …) on WiFi.
 *    0x0030  Same as 0x0020 (bit 5 dominates).
 *
 *  Upgrade note: older firmware used 0x0000 as WiFi-only. After this change, use 0x0010 for that.
 */
#define STATUS_KEEP_RELAY1      0x0001
#define STATUS_KEEP_RELAY2      0x0002
#define STATUS_WIFI_ONLY        0x0010
#define STATUS_MODEM_SLAVE      0x0020
#define STATUS_CONN_MODE_MASK   (STATUS_WIFI_ONLY | STATUS_MODEM_SLAVE)

typedef enum {
    NVS_CONN_MODEM_FULL = 0,       /**< 0x0000 */
    NVS_CONN_WIFI_ONLY,            /**< 0x0010 */
    NVS_CONN_MODEM_SLAVE_WIFI,     /**< 0x0020 (or 0x0030) */
} nvs_connectivity_mode_t;

/** Call after reading status_reg (e.g. app_main) to cache connectivity profile. */
void nvs_config_set_connectivity_mode_from_reg(uint16_t status_reg);

/** Cached mode from last nvs_config_set_connectivity_mode_from_reg(); default MODEM_FULL before set. */
nvs_connectivity_mode_t nvs_config_connectivity_mode(void);

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
