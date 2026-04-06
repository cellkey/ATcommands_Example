/**
 * @file wifi_manager.h
 * @brief WiFi STA connection manager with NVS-backed credentials.
 *
 * Reads wifi_ssid / wifi_pass from NVS namespace "unit_cfg".
 * If no SSID is configured the WiFi stack is not started.
 * Set credentials via the config shell:
 *   SET wifi_ssid=MyNetwork
 *   SET wifi_pass=MyPassword
 *   REBOOT
 *
 * WPS (push-button): hold WIFI_WPS_BUTTON_GPIO (default 15, active LOW) at WiFi init so
 * the pin reads LOW (then ~120 ms debounce). If the switch is open (HIGH), no extra delay.
 * Then press WPS on
 * the router within WIFI_WPS_TIMEOUT_S seconds. SSID/password are saved to NVS on success.
 * If nothing pairs, firmware falls back to normal scan + saved SSID.
 *
 * Auto-reconnects on disconnect (5-second retry, using esp_timer).
 * Call wifi_manager_init() from app_main after nvs_config_init().
 */

#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** NVS keys stored in NVS_CONFIG_NAMESPACE ("unit_cfg"). */
#define NVS_KEY_WIFI_SSID    "wifi_ssid"
#define NVS_KEY_WIFI_PASS    "wifi_pass"

/** Max lengths including null terminator (SSID ≤ 32 chars, WPA2 pass ≤ 63 chars). */
#define WIFI_MGR_SSID_MAX    33
#define WIFI_MGR_PASS_MAX    64

/** Compile-time default credentials — used when NVS keys wifi_ssid / wifi_pass are not set.
 *  NVS always wins if present.  Override at runtime via config shell:
 *    SET wifi_ssid=<name>   SET wifi_pass=<pass>   REBOOT
 *  If NVS SSID is missing from scan, firmware may connect using these defaults at runtime only;
 *  NVS is not overwritten, so the preferred SSID is tried again on the next boot when visible. */
//#define WIFI_DEFAULT_SSID    "Gross_home"  //change to your actual wifi ssid
//#define WIFI_DEFAULT_PASS    "avivaaviva1" //change to your actual wifi password
//#define WIFI_DEFAULT_SSID    "CREACELL"     /* must match beacon case (scan log) */
//#define WIFI_DEFAULT_PASS    "creacell2018" //Creacell Slocal wifi password
#define WIFI_DEFAULT_SSID    "south up guest"    //south up Slocal wifi password
#define WIFI_DEFAULT_PASS    ""                 //south up Slocal wifi password
/** Status LED GPIO (active-high, GPIO23).
 *  Steady ON          → not connected to local network
 *  Blink 500 / 500 ms → connected to local network (no server yet)
 *  Blink 800 / 200 ms → connected to server (Phase 2)               */
#define WIFI_STATUS_LED_GPIO  23

/**
 * WPS at boot (push-button config): hold this tact switch active while releasing reset
 * (or keep pressed for debounce window at start of WiFi init). Active LOW, internal pull-up.
 * Same pin as A7670E_LOCAL_BUTTON_GPIO (15) unless you wire a dedicated switch — change if needed.
 * Set to -1 to disable WPS-at-boot sampling.
 */
#ifndef WIFI_WPS_BUTTON_GPIO
#define WIFI_WPS_BUTTON_GPIO  15
#endif

/** Max WPS wait before fallback (enforced in firmware via esp_timer; IDF WPS stack stays 120s internally). */
#ifndef WIFI_WPS_TIMEOUT_S
#define WIFI_WPS_TIMEOUT_S    30
#endif

/**
 * After configuring the WPS GPIO, if the line is still HIGH, poll up to this many ms for LOW
 * (10 ms steps). Helps reset+tact timing and pin settle vs reading tact once. Same pin as the
 * relay local_button task, but that task starts later and uses edge detect — WPS only runs here.
 * Adds up to this delay on every boot when tact is not pressed; set to 0 for fastest boot.
 */
#ifndef WIFI_WPS_BOOT_GRACE_MS
#define WIFI_WPS_BOOT_GRACE_MS  200
#endif

/** Connection state visible to the rest of the application. */
typedef enum {
    WIFI_MGR_STATE_IDLE        = 0,  /**< No credentials or init not called yet */
    WIFI_MGR_STATE_CONNECTING  = 1,  /**< Attempting to connect / awaiting IP */
    WIFI_MGR_STATE_CONNECTED   = 2,  /**< Connected with a valid IP address */
    WIFI_MGR_STATE_DISCONNECTED = 3, /**< Was connected, lost link – retrying */
} wifi_mgr_state_t;

/**
 * @brief Initialise WiFi manager.
 *        Reads credentials from NVS, inits the netif/event/WiFi driver stack,
 *        and starts the STA connection in the background.
 *        Returns immediately; connection result arrives via event callbacks.
 * @return true  if SSID was found and WiFi stack started successfully.
 * @return false if no SSID is configured or init failed (WiFi not started).
 */
bool wifi_manager_init(void);

/**
 * @brief True while boot WPS is in progress: tact was held at init until success, fail, or timeout.
 *        Use for user-facing logs (e.g. after app_main "ready" banner).
 */
bool wifi_manager_wps_boot_pending(void);

/**
 * @brief Returns true when WiFi is connected and the device has an IP address.
 */
bool wifi_manager_is_connected(void);

/**
 * @brief Returns the current connection state enum.
 */
wifi_mgr_state_t wifi_manager_get_state(void);

/**
 * @brief Copies the current IPv4 address string (e.g. "192.168.1.10") into buf.
 * @return true if connected and IP copied, false otherwise (buf set to "").
 */
bool wifi_manager_get_ip(char *buf, size_t size);

/**
 * @brief Notify the WiFi manager that the TCP connection to the server is up or down.
 *        Switches the status LED from 500/500 ms blink → 800/200 ms blink when true.
 *        Call from the WiFi TCP transport layer (Phase 2).
 */
void wifi_manager_set_server_connected(bool connected);

#ifdef __cplusplus
}
#endif

#endif /* WIFI_MANAGER_H */
