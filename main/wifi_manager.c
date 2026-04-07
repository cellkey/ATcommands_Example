/**
 * @file wifi_manager.c
 * @brief WiFi STA connection manager.
 *
 * Lifecycle:
 *   1. wifi_manager_init()  →  reads NVS, inits stack, calls esp_wifi_start()
 *   2. WIFI_EVENT_STA_START →  esp_wifi_connect()
 *   3. IP_EVENT_STA_GOT_IP  →  state = CONNECTED, stores IP string
 *   4. WIFI_EVENT_STA_DISCONNECTED → state = DISCONNECTED, schedules 5-s retry via esp_timer
 *   5. Timer fires → esp_wifi_connect(), or full scan first if last disconnect was
 *      NO_AP_FOUND / BEACON_TIMEOUT (BLE coexist / stale BSSID).
 *
 *   On first STA start: passive/active scan → log visible APs → if profile SSID missing,
 *   fall back to WIFI_DEFAULT_* in the STA config only (NVS wifi_ssid unchanged so preferred
 *   SSID is retried next boot when it reappears); if still missing, skip connect and rescan in 5 s.
 *   Note: esp_wifi_scan_get_ap_records() clears the driver scan list — one fetch per SCAN_DONE,
 *   shared for log + fallback + open BSSID pin + SSID presence (do not call get_ap_records twice).
 *
 *   WPS at boot: GPIO must read LOW at init (then short debounce). If idle HIGH, no delay.
 *   Press the router WPS button. On success, SSID/pass saved to NVS; on timeout,
 *   falls back to normal scan + saved credentials.
 */

#include "wifi_manager.h"    /* WIFI_DEFAULT_SSID / WIFI_DEFAULT_PASS / WIFI_STATUS_LED_GPIO */
#include "nvs_config.h"

#include "esp_wifi.h"
#include "esp_wps.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stdlib.h>
#include <string.h>

static const char *TAG = "WIFI_MGR";

/* ── module state ───────────────────────────────────────────────────── */
static volatile wifi_mgr_state_t s_state           = WIFI_MGR_STATE_IDLE;
static volatile bool             s_server_connected = false;
/** WiFi TCP path: true while processing a server command line (GPIO2 link LED solid ON). */
static volatile bool             s_link_led_cmd_busy   = false;
static char                      s_ip_str[16]      = {0};   /* "xxx.xxx.xxx.xxx\0" */
static esp_timer_handle_t        s_reconnect_timer  = NULL;
/** One-shot: enforce WIFI_WPS_TIMEOUT_S (IDF supplicant ignores esp_wifi_wps_start(ms), uses 120s). */
static esp_timer_handle_t        s_wps_cap_timer    = NULL;
/** After first boot scan completes, reconnects skip scanning unless s_reconnect_scan_first. */
static bool                      s_scan_before_connect_done = false;
/** Set on BEACON_TIMEOUT / NO_AP_FOUND so next reconnect does a full scan (BLE coexist, stale BSSID). */
static volatile bool             s_reconnect_scan_first     = false;
/** Tact held at boot → run WPS PBC once after STA_START. */
static bool                      s_wps_boot_requested       = false;
/** True between esp_wifi_wps_start() and esp_wifi_wps_disable(). */
static bool                      s_wps_session_active       = false;
/** Copied at init: used after scan to pin OPEN/OWE BSSID (avoids 210 on same-SSID mixed BSS). */
static char                      s_profile_ssid[WIFI_MGR_SSID_MAX];
static bool                      s_profile_open             = false;

static bool wps_button_held_at_boot(void)
{
#if WIFI_WPS_BUTTON_GPIO < 0
    return false;
#else
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << (unsigned)WIFI_WPS_BUTTON_GPIO,
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    if (gpio_config(&io) != ESP_OK) {
        return false;
    }
#if WIFI_WPS_BOOT_GRACE_MS > 0
    for (int w = 0; w < WIFI_WPS_BOOT_GRACE_MS && gpio_get_level(WIFI_WPS_BUTTON_GPIO) != 0; w += 10) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
#endif
    /* Not pressed (HIGH) → no WPS. */
    if (gpio_get_level(WIFI_WPS_BUTTON_GPIO) != 0) {
        return false;
    }
    /* Line low: debounce before treating as WPS request (~120 ms extra). */
    int lows = 1;
    for (int i = 0; i < 4; i++) {
        vTaskDelay(pdMS_TO_TICKS(30));
        if (gpio_get_level(WIFI_WPS_BUTTON_GPIO) == 0) {
            lows++;
        }
    }
    return lows >= 3;
#endif
}

/**
 * Connect policy for esp_wifi_set_config (STA).
 * - rssi -127: zero-init rssi==0 would mean an impossible 0 dBm floor (IDF fast_scan example).
 * - ALL_CHANNEL_SCAN + sort by RSSI: same SSID on several BSSIDs/channels (guest meshes).
 * - failure_retry_cnt: try next matching AP after failures (requires all-channel scan).
 * - Open password: owe_enabled 0 here; after scan we set owe per BSS (OPEN vs OWE) in
 *   open_pick_bssid_from_scan_buf() on that same buffer.
 */
static void sta_apply_connect_policy(wifi_config_t *wc)
{
    if (!wc) {
        return;
    }
    wc->sta.scan_method  = WIFI_ALL_CHANNEL_SCAN;
    wc->sta.sort_method  = WIFI_CONNECT_AP_BY_SIGNAL;
    wc->sta.failure_retry_cnt = 8;
    wc->sta.threshold.rssi = -127;
    wc->sta.pmf_cfg.capable  = true;
    wc->sta.pmf_cfg.required = false;

    if (wc->sta.password[0] == '\0') {
        wc->sta.threshold.authmode = WIFI_AUTH_OPEN;
        wc->sta.owe_enabled        = 0;
    } else {
        wc->sta.threshold.authmode = WIFI_AUTH_WPA_WPA2_PSK;
        wc->sta.owe_enabled        = 0;
    }
}

/**
 * After a full scan, bind STA to the strongest OPEN or OWE BSS for s_profile_ssid.
 * Mitigates 210 when the same SSID is also broadcast encrypted (empty password vs WPA beacon).
 */
static bool open_pick_bssid_from_scan_buf(const wifi_ap_record_t *rec, uint16_t cnt)
{
    if (!rec || cnt == 0 || !s_profile_open || s_profile_ssid[0] == '\0') {
        return false;
    }

    int best_rssi = -128;
    int best_i    = -1;
    for (uint16_t i = 0; i < cnt; i++) {
        if (strcmp((char *)rec[i].ssid, s_profile_ssid) != 0) {
            continue;
        }
        if (rec[i].authmode != WIFI_AUTH_OPEN && rec[i].authmode != WIFI_AUTH_OWE) {
            continue;
        }
        if (rec[i].rssi > best_rssi) {
            best_rssi = rec[i].rssi;
            best_i    = (int)i;
        }
    }

    if (best_i < 0) {
        ESP_LOGW(TAG, "Open profile: no OPEN/OWE BSS for \"%s\" in scan — may get 210 if SSID is mixed",
                 s_profile_ssid);
        return false;
    }

    wifi_config_t wc = {0};
    if (esp_wifi_get_config(WIFI_IF_STA, &wc) != ESP_OK) {
        return false;
    }

    const wifi_ap_record_t *ap = &rec[best_i];
    wc.sta.bssid_set = true;
    memcpy(wc.sta.bssid, ap->bssid, 6);
    wc.sta.channel   = ap->primary;
    sta_apply_connect_policy(&wc);
    wc.sta.owe_enabled = (ap->authmode == WIFI_AUTH_OWE) ? 1 : 0;

    esp_err_t e = esp_wifi_set_config(WIFI_IF_STA, &wc);
    if (e != ESP_OK) {
        ESP_LOGW(TAG, "open BSSID pin: esp_wifi_set_config: %s", esp_err_to_name(e));
        return false;
    }

    ESP_LOGI(TAG, "Open profile: pin BSSID %02x:%02x:%02x:%02x:%02x:%02x ch %u RSSI %d (%s)",
             ap->bssid[0], ap->bssid[1], ap->bssid[2], ap->bssid[3], ap->bssid[4], ap->bssid[5],
             ap->primary, ap->rssi, ap->authmode == WIFI_AUTH_OWE ? "OWE" : "OPEN");
    return true;
}

static wifi_scan_config_t default_scan_config(void)
{
    /* active min/max = 0 → driver defaults; required when BT/BLE is on (coexistence). */
    wifi_scan_config_t sc = {
        .ssid        = NULL,
        .bssid       = NULL,
        .channel     = 0,
        .show_hidden = true,
        .scan_type   = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time.active.min = 0,
        .scan_time.active.max = 0,
    };
    return sc;
}

static void wps_save_credentials_to_nvs(const wifi_config_t *wc)
{
    if (!wc) {
        return;
    }
    const char *ssid = (const char *)wc->sta.ssid;
    const char *pass = (const char *)wc->sta.password;
    if (ssid[0] == '\0') {
        return;
    }
    (void)nvs_config_set_string(NVS_KEY_WIFI_SSID, ssid);
    (void)nvs_config_set_string(NVS_KEY_WIFI_PASS, pass[0] ? pass : "");
}

static void wps_cap_timer_stop(void)
{
    if (s_wps_cap_timer != NULL) {
        esp_timer_stop(s_wps_cap_timer);
    }
}

static void wps_abort_fallback_scan(void)
{
    wps_cap_timer_stop();
    if (!s_wps_session_active && !s_wps_boot_requested) {
        return;
    }
    s_wps_session_active = false;
    s_wps_boot_requested = false;
    esp_err_t e = esp_wifi_wps_disable();
    if (e != ESP_OK && e != ESP_ERR_WIFI_STATE) {
        ESP_LOGW(TAG, "WPS disable: %s", esp_err_to_name(e));
    }
    ESP_LOGI(TAG, "WPS ended — scanning, then normal connect…");
    wifi_scan_config_t sc = default_scan_config();
    e = esp_wifi_scan_start(&sc, false);
    if (e != ESP_OK) {
        ESP_LOGW(TAG, "scan after WPS failed (%s), connecting anyway", esp_err_to_name(e));
        s_scan_before_connect_done = true;
        esp_wifi_connect();
    }
}

static void wps_cap_timer_cb(void *arg)
{
    (void)arg;
    if (!s_wps_session_active) {
        return;
    }
    ESP_LOGW(TAG, "WPS timeout (%ds) — falling back to saved Wi-Fi", WIFI_WPS_TIMEOUT_S);
    wps_abort_fallback_scan();
}

static void wps_handle_success(void *event_data)
{
    wps_cap_timer_stop();
    s_wps_session_active = false;
    s_wps_boot_requested = false;

    wifi_config_t wc = {0};
    wifi_event_sta_wps_er_success_t *evt = (wifi_event_sta_wps_er_success_t *)event_data;
    bool have_cred = false;

    if (evt != NULL && evt->ap_cred_cnt > 0) {
        memset(&wc, 0, sizeof(wc));
        memcpy(wc.sta.ssid, evt->ap_cred[0].ssid, sizeof(wc.sta.ssid));
        memcpy(wc.sta.password, evt->ap_cred[0].passphrase, sizeof(wc.sta.password));
        sta_apply_connect_policy(&wc);
        if (esp_wifi_set_config(WIFI_IF_STA, &wc) == ESP_OK) {
            wps_save_credentials_to_nvs(&wc);
            ESP_LOGI(TAG, "WPS OK — SSID \"%s\" saved to NVS", wc.sta.ssid);
            have_cred = true;
        }
    } else if (esp_wifi_get_config(WIFI_IF_STA, &wc) == ESP_OK && wc.sta.ssid[0] != '\0') {
        /* Single-credential case: driver may have applied config already. */
        sta_apply_connect_policy(&wc);
        if (esp_wifi_set_config(WIFI_IF_STA, &wc) == ESP_OK) {
            wps_save_credentials_to_nvs(&wc);
            ESP_LOGI(TAG, "WPS OK — SSID \"%s\" saved to NVS (legacy path)", wc.sta.ssid);
            have_cred = true;
        }
    }

    esp_err_t wd = esp_wifi_wps_disable();
    if (wd != ESP_OK && wd != ESP_ERR_WIFI_STATE) {
        ESP_LOGW(TAG, "wps_disable: %s", esp_err_to_name(wd));
    }

    if (have_cred) {
        s_scan_before_connect_done = true;
        ESP_LOGI(TAG, "Connecting after WPS…");
        esp_wifi_connect();
    } else {
        ESP_LOGW(TAG, "WPS success but no usable credentials — fallback scan");
        wps_abort_fallback_scan();
    }
}

static const char *authmode_str(wifi_auth_mode_t m)
{
    switch (m) {
    case WIFI_AUTH_OPEN:            return "OPEN";
    case WIFI_AUTH_WEP:             return "WEP";
    case WIFI_AUTH_WPA_PSK:         return "WPA_PSK";
    case WIFI_AUTH_WPA2_PSK:        return "WPA2_PSK";
    case WIFI_AUTH_WPA_WPA2_PSK:    return "WPA_WPA2_PSK";
    case WIFI_AUTH_WPA2_ENTERPRISE: return "WPA2_ENT";
    case WIFI_AUTH_WPA3_PSK:        return "WPA3_PSK";
    case WIFI_AUTH_WPA2_WPA3_PSK:   return "WPA2_WPA3_PSK";
    default:                        return "OTHER";
    }
}

static const char *disconnect_reason_str(wifi_err_reason_t r)
{
    switch (r) {
    case WIFI_REASON_AUTH_EXPIRE:        return "AUTH_EXPIRE";
    case WIFI_REASON_AUTH_FAIL:          return "AUTH_FAIL";
    case WIFI_REASON_NO_AP_FOUND:        return "NO_AP_FOUND";
    case WIFI_REASON_ASSOC_FAIL:         return "ASSOC_FAIL";
    case WIFI_REASON_HANDSHAKE_TIMEOUT:  return "HANDSHAKE_TIMEOUT";
    case WIFI_REASON_CONNECTION_FAIL:    return "CONNECTION_FAIL";
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT: return "4WAY_HS_TIMEOUT";
    case WIFI_REASON_BEACON_TIMEOUT:     return "BEACON_TIMEOUT";
    case WIFI_REASON_MIC_FAILURE:        return "MIC_FAILURE";
    case WIFI_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY:
        return "NO_AP_SEC_MISMATCH";
    case WIFI_REASON_NO_AP_FOUND_IN_AUTHMODE_THRESHOLD:
        return "NO_AP_AUTH_THRESHOLD";
    case WIFI_REASON_NO_AP_FOUND_IN_RSSI_THRESHOLD:
        return "NO_AP_RSSI_THRESHOLD";
    default:                             return "";
    }
}

/**
 * One esp_wifi_scan_get_ap_records() per scan — IDF clears internal list after it.
 * Caller must free() the returned buffer.
 */
static wifi_ap_record_t *scan_results_take(uint16_t *out_count)
{
    uint16_t n = 0;
    if (esp_wifi_scan_get_ap_num(&n) != ESP_OK || n == 0) {
        if (out_count) {
            *out_count = 0;
        }
        return NULL;
    }
    wifi_ap_record_t *rec = (wifi_ap_record_t *)calloc(n, sizeof(wifi_ap_record_t));
    if (!rec) {
        ESP_LOGE(TAG, "Scan: OOM for %u AP record(s)", (unsigned)n);
        if (out_count) {
            *out_count = 0;
        }
        return NULL;
    }
    uint16_t cnt = n;
    if (esp_wifi_scan_get_ap_records(&cnt, rec) != ESP_OK) {
        ESP_LOGE(TAG, "Scan: get_ap_records failed");
        free(rec);
        if (out_count) {
            *out_count = 0;
        }
        return NULL;
    }
    if (out_count) {
        *out_count = cnt;
    }
    return rec;
}

static void log_scan_results_buf(const wifi_ap_record_t *rec, uint16_t count)
{
    if (!rec || count == 0) {
        ESP_LOGW(TAG, "Scan: 0 APs (check antenna / band — office may be 5 GHz only)");
        return;
    }
    ESP_LOGI(TAG, "Scan: %u AP(s) visible:", (unsigned)count);
    for (uint16_t i = 0; i < count; i++) {
        const wifi_ap_record_t *ap = &rec[i];
        const char *ssid = (const char *)ap->ssid;
        if (ssid[0] == '\0') {
            ssid = "(hidden)";
        }
        ESP_LOGI(TAG, "  [%2u] %-32s  RSSI %4d  ch %3u  %s",
                 (unsigned)(i + 1), ssid, ap->rssi, ap->primary, authmode_str(ap->authmode));
    }
}

static bool scan_contains_ssid_buf(const wifi_ap_record_t *rec, uint16_t cnt, const char *ssid)
{
    if (!rec || cnt == 0 || !ssid || ssid[0] == '\0') {
        return false;
    }
    for (uint16_t i = 0; i < cnt; i++) {
        if (strcmp((char *)rec[i].ssid, ssid) == 0) {
            return true;
        }
    }
    return false;
}

/**
 * If current STA SSID does not appear in scan, switch STA + s_profile_* to compile-time defaults
 * for this run only — does not write NVS, so LIST/GET wifi_ssid still show preferred (e.g. CREACELL)
 * and the next cold boot tries NVS again when that AP is back.
 * @param rec,cnt  Same snapshot from single scan_results_take() (IDF clears list after read).
 */
static void maybe_fallback_to_default_wifi_after_scan_buf(const wifi_ap_record_t *rec, uint16_t count)
{
    if (WIFI_DEFAULT_SSID[0] == '\0' || !rec || count == 0) {
        return;
    }

    wifi_config_t wc = {0};
    if (esp_wifi_get_config(WIFI_IF_STA, &wc) != ESP_OK) {
        return;
    }
    const char *cur = (const char *)wc.sta.ssid;
    if (cur[0] == '\0') {
        return;
    }
    if (scan_contains_ssid_buf(rec, count, cur)) {
        return;
    }
    if (strcmp(cur, WIFI_DEFAULT_SSID) == 0) {
        ESP_LOGW(TAG, "Default SSID \"%s\" not in scan — will not connect until it appears", cur);
        return;
    }

    ESP_LOGW(TAG, "SSID \"%s\" not in scan — falling back to default \"%s\"", cur, WIFI_DEFAULT_SSID);

    memset(&wc, 0, sizeof(wc));
    strncpy((char *)wc.sta.ssid, WIFI_DEFAULT_SSID, sizeof(wc.sta.ssid) - 1);
    strncpy((char *)wc.sta.password, WIFI_DEFAULT_PASS, sizeof(wc.sta.password) - 1);
    wc.sta.bssid_set = false;
    memset(wc.sta.bssid, 0, sizeof(wc.sta.bssid));
    wc.sta.channel = 0;
    sta_apply_connect_policy(&wc);

    if (esp_wifi_set_config(WIFI_IF_STA, &wc) != ESP_OK) {
        ESP_LOGW(TAG, "Fallback default: esp_wifi_set_config failed");
        return;
    }
    ESP_LOGI(TAG, "Runtime Wi‑Fi fallback to \"%s\" — NVS wifi_ssid/wifi_pass unchanged (preferred retried next boot)",
             WIFI_DEFAULT_SSID);

    strncpy(s_profile_ssid, WIFI_DEFAULT_SSID, sizeof(s_profile_ssid) - 1);
    s_profile_ssid[sizeof(s_profile_ssid) - 1] = '\0';
    s_profile_open = (WIFI_DEFAULT_PASS[0] == '\0');
}

/* ── WiFi status LED task ───────────────────────────────────────────── */
/*
 *  Not connected to network  →  steady ON
 *  Connected to network      →  blink 500 ms ON / 500 ms OFF
 *  Connected to server       →  blink 200 ms ON / 1500 ms OFF
 */
static void wifi_status_led_task(void *arg)
{
    (void)arg;
    gpio_reset_pin(WIFI_STATUS_LED_GPIO);
    gpio_set_direction(WIFI_STATUS_LED_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(WIFI_STATUS_LED_GPIO, 1);   /* start: steady ON (not yet connected) */

    while (1) {
        if (s_server_connected) {
            /* Server TCP up → 200 ms ON / 1500 ms OFF */
            gpio_set_level(WIFI_STATUS_LED_GPIO, 1);
            vTaskDelay(pdMS_TO_TICKS(200));
            gpio_set_level(WIFI_STATUS_LED_GPIO, 0);
            vTaskDelay(pdMS_TO_TICKS(1500));
        } else if (s_state == WIFI_MGR_STATE_CONNECTED) {
            /* Local network only → 500 ms ON / 500 ms OFF */
            gpio_set_level(WIFI_STATUS_LED_GPIO, 1);
            vTaskDelay(pdMS_TO_TICKS(500));
            gpio_set_level(WIFI_STATUS_LED_GPIO, 0);
            vTaskDelay(pdMS_TO_TICKS(500));
        } else {
            /* Not connected → steady ON, poll every 100 ms for state change */
            gpio_set_level(WIFI_STATUS_LED_GPIO, 1);
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
}

/* ── reconnect timer callback ───────────────────────────────────────── */
static void reconnect_timer_cb(void *arg)
{
    (void)arg;
    if (s_state != WIFI_MGR_STATE_DISCONNECTED) {
        return;
    }
    if (s_reconnect_scan_first) {
        s_reconnect_scan_first = false;
        s_scan_before_connect_done = false;
        wifi_scan_config_t sc = default_scan_config();
        esp_err_t se = esp_wifi_scan_start(&sc, false);
        if (se != ESP_OK) {
            ESP_LOGW(TAG, "Reconnect scan failed (%s) — direct connect", esp_err_to_name(se));
            s_scan_before_connect_done = true;
            esp_wifi_connect();
        } else {
            ESP_LOGI(TAG, "Reconnect: scanning before connect…");
        }
        return;
    }
    ESP_LOGI(TAG, "Reconnect attempt...");
    esp_wifi_connect();
}

/* ── unified event handler ──────────────────────────────────────────── */
static void wifi_event_handler(void *arg,
                                esp_event_base_t event_base,
                                int32_t          event_id,
                                void            *event_data)
{
    if (event_base == WIFI_EVENT) {
        switch (event_id) {

        case WIFI_EVENT_STA_START:
            s_state = WIFI_MGR_STATE_CONNECTING;
            if (s_wps_boot_requested) {
                esp_wps_config_t wcfg = WPS_CONFIG_INIT_DEFAULT(WPS_TYPE_PBC);
                esp_err_t we = esp_wifi_wps_enable(&wcfg);
                if (we != ESP_OK) {
                    ESP_LOGW(TAG, "WPS enable failed (%s) — normal Wi‑Fi", esp_err_to_name(we));
                    s_wps_boot_requested = false;
                } else {
                    we = esp_wifi_wps_start(WIFI_WPS_TIMEOUT_S * 1000);
                    if (we != ESP_OK) {
                        ESP_LOGW(TAG, "WPS start failed (%s) — normal Wi‑Fi", esp_err_to_name(we));
                        esp_wifi_wps_disable();
                        s_wps_boot_requested = false;
                    } else {
                        s_wps_session_active = true;
                        ESP_LOGI(TAG, "WPS PBC active (%ds) — press WPS on router now",
                                 WIFI_WPS_TIMEOUT_S);
                        if (s_wps_cap_timer != NULL) {
                            (void)esp_timer_start_once(
                                    s_wps_cap_timer,
                                    (uint64_t)WIFI_WPS_TIMEOUT_S * 1000000ULL);
                        }
                        break;
                    }
                }
            }
            if (!s_scan_before_connect_done) {
                wifi_scan_config_t sc = default_scan_config();
                esp_err_t se = esp_wifi_scan_start(&sc, false);
                if (se != ESP_OK) {
                    ESP_LOGW(TAG, "STA started – scan failed (%s), connecting anyway",
                             esp_err_to_name(se));
                    s_scan_before_connect_done = true;
                    ESP_LOGI(TAG, "Connecting…");
                    esp_wifi_connect();
                } else {
                    ESP_LOGI(TAG, "STA started – scanning channels (results before connect)…");
                }
            } else {
                ESP_LOGI(TAG, "STA started – connecting…");
                esp_wifi_connect();
            }
            break;

        case WIFI_EVENT_STA_WPS_ER_SUCCESS:
            ESP_LOGI(TAG, "WPS registration succeeded");
            wps_handle_success(event_data);
            break;

        case WIFI_EVENT_STA_WPS_ER_FAILED:
            ESP_LOGW(TAG, "WPS failed — falling back to saved Wi‑Fi");
            wps_abort_fallback_scan();
            break;

        case WIFI_EVENT_STA_WPS_ER_TIMEOUT:
            ESP_LOGW(TAG, "WPS protocol timeout — falling back to saved Wi‑Fi");
            wps_abort_fallback_scan();
            break;

        case WIFI_EVENT_STA_WPS_ER_PIN:
            ESP_LOGI(TAG, "WPS PIN mode not used (PBC only)");
            break;

        case WIFI_EVENT_SCAN_DONE:
            if (!s_scan_before_connect_done) {
                s_scan_before_connect_done = true;
                uint16_t ap_count = 0;
                wifi_ap_record_t *ap_list = scan_results_take(&ap_count);
                if (ap_list == NULL || ap_count == 0) {
                    ESP_LOGW(TAG, "Scan: 0 APs (check antenna / band — office may be 5 GHz only)");
                    if (ap_list) {
                        free(ap_list);
                    }
                    s_state = WIFI_MGR_STATE_DISCONNECTED;
                    s_reconnect_scan_first = true;
                    if (s_reconnect_timer) {
                        esp_timer_start_once(s_reconnect_timer, 5ULL * 1000000ULL);
                    }
                    break;
                }
                log_scan_results_buf(ap_list, ap_count);
                maybe_fallback_to_default_wifi_after_scan_buf(ap_list, ap_count);
                (void)open_pick_bssid_from_scan_buf(ap_list, ap_count);
                if (!scan_contains_ssid_buf(ap_list, ap_count, s_profile_ssid)) {
                    ESP_LOGW(TAG, "SSID \"%s\" not in scan — skip connect; rescan in 5 s",
                             s_profile_ssid);
                    free(ap_list);
                    s_state = WIFI_MGR_STATE_DISCONNECTED;
                    s_reconnect_scan_first = true;
                    if (s_reconnect_timer) {
                        esp_timer_start_once(s_reconnect_timer, 5ULL * 1000000ULL);
                    }
                    break;
                }
                free(ap_list);
                ESP_LOGI(TAG, "Connecting to configured SSID…");
                esp_wifi_connect();
            }
            break;

        case WIFI_EVENT_STA_CONNECTED:
            /* Associated; wait for DHCP (IP_EVENT_STA_GOT_IP). */
            ESP_LOGI(TAG, "STA associated – awaiting IP...");
            break;

        case WIFI_EVENT_STA_DISCONNECTED: {
            if (s_wps_session_active) {
                ESP_LOGI(TAG, "STA disconnected during WPS (ignored for reconnect timer)");
                break;
            }
            wifi_event_sta_disconnected_t *d =
                (wifi_event_sta_disconnected_t *)event_data;
            s_ip_str[0] = '\0';
            s_state     = WIFI_MGR_STATE_DISCONNECTED;
            {
                const char *rs = disconnect_reason_str(d->reason);
                if (rs[0]) {
                    ESP_LOGW(TAG, "Disconnected: %s (%d) – retry in 5 s", rs, (int)d->reason);
                } else {
                    ESP_LOGW(TAG, "Disconnected (reason %d) – retry in 5 s", (int)d->reason);
                }
                if (d->reason == WIFI_REASON_NO_AP_FOUND) {
                    ESP_LOGW(TAG, "  → SSID not seen; confirm 2.4 GHz, exact name, and scan list above.");
                    s_reconnect_scan_first = true;
                } else if (d->reason == WIFI_REASON_BEACON_TIMEOUT) {
                    s_reconnect_scan_first = true;
                } else if (d->reason == WIFI_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY ||
                           d->reason == WIFI_REASON_NO_AP_FOUND_IN_AUTHMODE_THRESHOLD ||
                           d->reason == WIFI_REASON_NO_AP_FOUND_IN_RSSI_THRESHOLD) {
                    ESP_LOGW(TAG, "  → Security vs STA profile (OWE off?, encrypted BSS w/ empty pass?, see IDFGH-12537).");
                } else if (d->reason == WIFI_REASON_AUTH_FAIL ||
                           d->reason == WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT) {
                    ESP_LOGW(TAG, "  → Check password. WPA2-Enterprise is not supported with current STA config.");
                }
            }
            if (s_reconnect_timer) {
                /* 5 000 000 µs = 5 s */
                esp_timer_start_once(s_reconnect_timer, 5ULL * 1000000ULL);
            }
            break;
        }

        default:
            break;
        }

    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *ev = (ip_event_got_ip_t *)event_data;
        snprintf(s_ip_str, sizeof(s_ip_str), IPSTR, IP2STR(&ev->ip_info.ip));
        s_state = WIFI_MGR_STATE_CONNECTED;

        wifi_config_t wc = {0};
        char ssid_disp[sizeof(wc.sta.ssid) + 1] = {0};
        if (esp_wifi_get_config(WIFI_IF_STA, &wc) == ESP_OK) {
            strncpy(ssid_disp, (char *)wc.sta.ssid, sizeof(ssid_disp) - 1);
        }
        if (ssid_disp[0] == '\0') {
            strncpy(ssid_disp, s_profile_ssid, sizeof(ssid_disp) - 1);
        }
        if (wc.sta.bssid_set) {
            ESP_LOGI(TAG,
                     "Connected — SSID \"%s\"  BSSID %02x:%02x:%02x:%02x:%02x:%02x  IP %s  GW " IPSTR
                     "  mask " IPSTR,
                     ssid_disp[0] ? ssid_disp : "(unknown)",
                     wc.sta.bssid[0], wc.sta.bssid[1], wc.sta.bssid[2], wc.sta.bssid[3], wc.sta.bssid[4],
                     wc.sta.bssid[5], s_ip_str, IP2STR(&ev->ip_info.gw), IP2STR(&ev->ip_info.netmask));
        } else {
            ESP_LOGI(TAG,
                     "Connected — SSID \"%s\"  IP %s  GW " IPSTR "  mask " IPSTR,
                     ssid_disp[0] ? ssid_disp : "(unknown)", s_ip_str, IP2STR(&ev->ip_info.gw),
                     IP2STR(&ev->ip_info.netmask));
        }
    }
}

/* ── public API ─────────────────────────────────────────────────────── */

bool wifi_manager_wps_boot_pending(void)
{
    return s_wps_boot_requested;
}

bool wifi_manager_init(void)
{
    /* Read credentials: NVS value wins; compiled default is the fallback. */
    char ssid[WIFI_MGR_SSID_MAX] = {0};
    char pass[WIFI_MGR_PASS_MAX] = {0};

    bool ssid_from_nvs = nvs_config_get_string(NVS_KEY_WIFI_SSID, ssid, sizeof(ssid), WIFI_DEFAULT_SSID);
    bool pass_from_nvs = nvs_config_get_string(NVS_KEY_WIFI_PASS, pass, sizeof(pass), WIFI_DEFAULT_PASS);

    s_wps_boot_requested = wps_button_held_at_boot();
    if (s_wps_boot_requested) {
        ESP_LOGI(TAG, "WPS boot: button held — will start WPS PBC after Wi‑Fi STA starts");
    }

    memset(s_profile_ssid, 0, sizeof(s_profile_ssid));
    if (ssid[0] != '\0') {
        strncpy(s_profile_ssid, ssid, sizeof(s_profile_ssid) - 1);
    }
    s_profile_open = (pass[0] == '\0');

    if (ssid[0] == '\0' && !s_wps_boot_requested) {
        /* Both NVS and the compiled default are empty – nothing to connect to. */
        ESP_LOGW(TAG, "No SSID configured (NVS empty, WIFI_DEFAULT_SSID not set) – WiFi not started");
        return false;
    }

    /* Debug: SSID + password in log — avoid on production units if serial/USB is exposed. */
    if (ssid[0] != '\0') {
        ESP_LOGI(TAG, "WiFi STA — SSID: \"%s\" (%s)  password: %s (%s)",
                 ssid,
                 ssid_from_nvs ? "NVS" : "compile default",
                 pass[0] ? pass : "(empty, open AP)",
                 pass_from_nvs ? "NVS" : (WIFI_DEFAULT_PASS[0] ? "compile default" : "none"));
    } else {
        ESP_LOGI(TAG, "WiFi STA — no SSID yet (WPS will provision)");
    }

    /* ── esp_netif ── init once (safe to call if already done by another component) */
    esp_err_t err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "esp_netif_init: %s", esp_err_to_name(err));
        return false;
    }

    /* ── default event loop ── create once */
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "esp_event_loop_create_default: %s", esp_err_to_name(err));
        return false;
    }

    /* ── netif STA object ── */
    esp_netif_create_default_wifi_sta();

    /* ── WiFi driver ── */
    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init_cfg));

    /* ── event handlers ── */
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
            WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
            IP_EVENT,   IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL, NULL));

    /* ── reconnect timer (created once here, started on disconnect) ── */
    esp_timer_create_args_t timer_args = {
        .callback = reconnect_timer_cb,
        .name     = "wifi_reconnect",
    };
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &s_reconnect_timer));

    esp_timer_create_args_t wps_cap_args = {
        .callback = wps_cap_timer_cb,
        .name     = "wps_cap",
    };
    if (esp_timer_create(&wps_cap_args, &s_wps_cap_timer) != ESP_OK) {
        s_wps_cap_timer = NULL;
        ESP_LOGW(TAG, "WPS cap timer not created — WPS may run up to 120s (IDF default)");
    }

    /* ── WiFi config ── */
    wifi_config_t wifi_cfg = {0};
    strncpy((char *)wifi_cfg.sta.ssid,     ssid, sizeof(wifi_cfg.sta.ssid)     - 1);
    strncpy((char *)wifi_cfg.sta.password, pass, sizeof(wifi_cfg.sta.password) - 1);

    sta_apply_connect_policy(&wifi_cfg);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());  /* triggers WIFI_EVENT_STA_START → connect */
    /* Reduces STA+BLE coexist connect failures that show as "Coexist: Wi-Fi connect fail". */
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));

    s_state = WIFI_MGR_STATE_CONNECTING;

    /* Status LED task: lightweight, low priority. */
    xTaskCreate(wifi_status_led_task, "wifi_led", 1536, NULL, 3, NULL);

    return true;
}

bool wifi_manager_is_connected(void)
{
    return s_state == WIFI_MGR_STATE_CONNECTED;
}

wifi_mgr_state_t wifi_manager_get_state(void)
{
    return s_state;
}

bool wifi_manager_get_ip(char *buf, size_t size)
{
    if (!buf || size == 0) return false;
    buf[0] = '\0';
    if (s_state != WIFI_MGR_STATE_CONNECTED || s_ip_str[0] == '\0') return false;
    strncpy(buf, s_ip_str, size - 1);
    buf[size - 1] = '\0';
    return true;
}

void wifi_manager_set_server_connected(bool connected)
{
    s_server_connected = connected;
    ESP_LOGI(TAG, "Server connection status: %s", connected ? "CONNECTED" : "NOT CONNECTED !");
}

bool wifi_manager_link_led_command_busy(void)
{
    return s_link_led_cmd_busy;
}

void wifi_manager_set_link_led_command_busy(bool busy)
{
    s_link_led_cmd_busy = busy;
}
