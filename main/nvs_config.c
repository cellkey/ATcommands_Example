/**
 * @file nvs_config.c
 * @brief NVS config implementation (unit_cfg namespace).
 */

#include "nvs_config.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static const char *TAG = "NVS_CFG";
static nvs_handle_t s_nvs = 0;
static bool s_init = false;
static nvs_connectivity_mode_t s_conn_mode = NVS_CONN_MODEM_FULL;

bool nvs_config_init(void) {
    if (s_init) return true;
    esp_err_t err = nvs_open(NVS_CONFIG_NAMESPACE, NVS_READWRITE, &s_nvs);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "nvs_open %s failed %s", NVS_CONFIG_NAMESPACE, esp_err_to_name(err));
        return false;
    }
    s_init = true;
    ESP_LOGI(TAG, "NVS config open: %s", NVS_CONFIG_NAMESPACE);
    return true;
}

bool nvs_config_get_string(const char *key, char *buf, size_t size, const char *default_val) {
    if (!buf || size == 0) return false;
    if (default_val) {
        strncpy(buf, default_val, size - 1);
        buf[size - 1] = '\0';
    } else {
        buf[0] = '\0';
    }
    if (!s_init) return false;
    size_t len = size;
    esp_err_t err = nvs_get_str(s_nvs, key, buf, &len);
    if (err == ESP_OK) return true;
    return false;
}

bool nvs_config_set_string(const char *key, const char *value) {
    if (!s_init || !key || !value) return false;
    esp_err_t err = nvs_set_str(s_nvs, key, value);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "nvs_set_str %s failed %s", key, esp_err_to_name(err));
        return false;
    }
    err = nvs_commit(s_nvs);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "nvs_commit failed %s", esp_err_to_name(err));
        return false;
    }
    ESP_LOGI(TAG, "NVS set %s=%s", key, value);
    return true;
}

void nvs_config_ensure_unit_id_default(const char *default_id) {
    if (!s_init || !default_id) return;
    char buf[NVS_CONFIG_MAX_LEN];
    size_t len = sizeof(buf);
    esp_err_t err = nvs_get_str(s_nvs, NVS_KEY_UNIT_ID, buf, &len);
    /* Write default only if key is missing. If any unit_id is already in NVS, do not change it. */
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        if (nvs_config_set_string(NVS_KEY_UNIT_ID, default_id)) {
            ESP_LOGI(TAG, "unit_id key was empty – wrote default %s to NVS", default_id);
        }
    }
}

int nvs_config_get_port(const char *key, int default_val) {
    char buf[16];
    if (!nvs_config_get_string(key, buf, sizeof(buf), NULL)) return default_val;
    int p = atoi(buf);
    return (p > 0 && p <= 65535) ? p : default_val;
}

bool nvs_config_set_port(const char *key, int port) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", port);
    return nvs_config_set_string(key, buf);
}

bool nvs_config_get_blob(const char *key, void *buf, size_t *size, const void *default_data, size_t default_size) {
    if (!buf || !size || *size == 0) return false;
    if (default_data && default_size > 0 && default_size <= *size) {
        memcpy(buf, default_data, default_size);
        *size = default_size;
    }
    if (!s_init) return false;
    size_t required_size = *size;
    esp_err_t err = nvs_get_blob(s_nvs, key, buf, &required_size);
    if (err == ESP_OK) {
        *size = required_size;
        return true;
    }
    return false;
}

bool nvs_config_set_blob(const char *key, const void *data, size_t size) {
    if (!s_init || !key || !data || size == 0) return false;
    esp_err_t err = nvs_set_blob(s_nvs, key, data, size);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "nvs_set_blob %s failed %s", key, esp_err_to_name(err));
        return false;
    }
    err = nvs_commit(s_nvs);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "nvs_commit failed %s", esp_err_to_name(err));
        return false;
    }
    ESP_LOGI(TAG, "NVS set blob %s (%zu bytes)", key, size);
    return true;
}

uint16_t nvs_config_get_status_reg(uint16_t default_val) {
    uint16_t val = default_val;
    size_t size = sizeof(val);
    nvs_config_get_blob(NVS_KEY_STATUS_REG, &val, &size, &default_val, sizeof(default_val));
    return val;
}

bool nvs_config_set_status_reg(uint16_t value) {
    return nvs_config_set_blob(NVS_KEY_STATUS_REG, &value, sizeof(value));
}

void nvs_config_set_connectivity_mode_from_reg(uint16_t sr) {
    unsigned m = sr & STATUS_CONN_MODE_MASK;
    if ((m & STATUS_MODEM_SLAVE) != 0) {
        s_conn_mode = NVS_CONN_MODEM_SLAVE_WIFI;
    } else if ((m & STATUS_WIFI_ONLY) != 0) {
        s_conn_mode = NVS_CONN_WIFI_ONLY;
    } else {
        s_conn_mode = NVS_CONN_MODEM_FULL;
    }
}

nvs_connectivity_mode_t nvs_config_connectivity_mode(void) {
    return s_conn_mode;
}
