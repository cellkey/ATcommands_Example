/**
 * @file unit_ready.c
 * Announced after: Wi‑Fi path → TCP 200 OK + server CONNECTED (wifi_tcp_client);
 * pure modem → modem init connect_ok (NVS_CONN_MODEM_FULL only).
 */

#include "unit_ready.h"
#include "esp_log.h"
#include <stdbool.h>

static const char *TAG = "SYS";
static bool s_unit_ready_announced;

void unit_ready_try_announce_once(void)
{
    if (s_unit_ready_announced) {
        return;
    }
    s_unit_ready_announced = true;
    ESP_LOGI(TAG, "\033[1;32mUnit Ready..\033[0m");
}
