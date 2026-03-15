/**
 * @file config_uart.c
 * @brief Config shell for NVS: SET/GET/LIST/REBOOT. Uses console (same UART as debug/flash).
 */

#include "config_uart.h"
#include "nvs_config.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <ctype.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

static const char *TAG = "CFG_UART";
#define CONFIG_LINE_BUF_SIZE  256
#define CONFIG_TASK_STACK    4096
#define CONFIG_READ_DELAY_MS 20   /* yield often so watchdog is fed */

static void send_line(const char *s) {
    printf("%s\n", s);
}

/* Read one line from console with periodic yield (avoids task watchdog). */
static int get_line(char *buf, int size) {
    int fd = fileno(stdin);
    int n = 0;
    bool prompt_shown = false;
    
    while (n < size - 1) {
        char c;
        ssize_t r = read(fd, &c, 1);
        if (r == 1) {
            if (!prompt_shown) {
                printf("\nCFG> ");  // Show prompt when first character arrives
                fflush(stdout);
                prompt_shown = true;
            }
            
            if (c == '\n' || c == '\r') {
                buf[n] = '\0';
                printf("\n");  // Echo newline
                fflush(stdout);
                return n;
            }
            
            // Echo printable characters
            if (c >= 32 && c < 127) {
                printf("%c", c);
                fflush(stdout);
            }
            
            // Handle backspace
            if (c == '\b' || c == 127) {
                if (n > 0) {
                    n--;
                    printf("\b \b");  // Erase character
                    fflush(stdout);
                }
            } else if (c >= 32 && c < 127) {
                buf[n++] = c;
            }
        } else if (r < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            vTaskDelay(pdMS_TO_TICKS(CONFIG_READ_DELAY_MS));
        } else {
            if (n > 0) buf[n] = '\0';
            return n > 0 ? n : -1;
        }
    }
    buf[n] = '\0';
    return n;
}

static void trim(char *buf) {
    // Trim leading whitespace
    char *start = buf;
    while (*start && isspace((unsigned char)*start)) start++;
    
    // Find end of string
    char *end = start + strlen(start);
    
    // Trim trailing whitespace
    while (end > start && isspace((unsigned char)*(end - 1))) end--;
    *end = '\0';
    
    // Move trimmed string to start of buffer
    if (start != buf) {
        memmove(buf, start, (size_t)(end - start) + 1);
    }
}

/* Parse hex string (e.g. "0000", "0x0000", "FF") to uint16_t. Returns true on success. */
static bool parse_hex_uint16(const char *hex_str, uint16_t *out) {
    if (!hex_str || !out) return false;
    char *end;
    unsigned long val = strtoul(hex_str, &end, 16);
    if (*end != '\0' && (*end != ' ' && *end != '\t')) return false;
    if (val > 0xFFFF) return false;
    *out = (uint16_t)val;
    return true;
}

static void list_all(void) {
    char buf[NVS_CONFIG_MAX_LEN];
    nvs_config_get_string(NVS_KEY_UNIT_ID,   buf, sizeof(buf), "");
    char line[NVS_CONFIG_MAX_LEN + 16];
    snprintf(line, sizeof(line), "unit_id=%s", buf[0] ? buf : "(default)");
    send_line(line);
    nvs_config_get_string(NVS_KEY_FW_VER, buf, sizeof(buf), "");
    snprintf(line, sizeof(line), "fw_ver=%s", buf[0] ? buf : "(default)");
    send_line(line);
    uint16_t status_reg = nvs_config_get_status_reg(0x0000);
    snprintf(line, sizeof(line), "status_reg=0x%04X", status_reg);
    send_line(line);
}

static void config_uart_task(void *arg) {
    (void)arg;
    char line[CONFIG_LINE_BUF_SIZE];
    /* Non-blocking stdin so we can yield in get_line and avoid task watchdog. */
    int fd = fileno(stdin);
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags >= 0) fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    send_line("=== Config Shell Ready ===");
    send_line("Commands: SET key=val | GET key | LIST | REBOOT");
    send_line("CFG> prompt appears when typing. Logs may scroll, but your input will be captured.");
    printf("CFG> ");
    fflush(stdout);

    while (1) {
        int len = get_line(line, sizeof(line));
        if (len < 0) continue;
        trim(line);
        if (line[0] == '\0') {
            printf("CFG> ");  // Show prompt again for empty line
            fflush(stdout);
            continue;
        }

        if (strncasecmp(line, "SET ", 4) == 0) {
            char *eq = strchr(line + 4, '=');
            if (!eq) {
                send_line("ERR SET key=value");
                continue;
            }
            *eq = '\0';
            char *key = line + 4;
            char *val = eq + 1;
            trim(key); trim(val);
            if (strcmp(key, "status_reg") == 0) {
                uint16_t reg_val;
                if (parse_hex_uint16(val, &reg_val)) {
                    if (nvs_config_set_status_reg(reg_val)) {
                        char line[32];
                        snprintf(line, sizeof(line), "OK status_reg=0x%04X saved", reg_val);
                        send_line(line);
                    } else
                        send_line("ERR NVS write failed");
                } else
                    send_line("ERR invalid hex (e.g. 0000 or 0x0000)");
            } else if (strcmp(key, "unit_id") == 0) {
                const char *nkey = NVS_KEY_UNIT_ID;
                if (strlen(val) >= NVS_CONFIG_MAX_LEN) {
                    send_line("ERR value too long");
                } else if (nvs_config_set_string(nkey, val)) {
                    send_line("OK saved");
                } else
                    send_line("ERR NVS write failed");
            } else if (strcmp(key, "fw_ver") == 0) {
                const char *nkey = NVS_KEY_FW_VER;
                if (strlen(val) >= NVS_CONFIG_MAX_LEN) {
                    send_line("ERR value too long");
                } else if (nvs_config_set_string(nkey, val)) {
                    send_line("OK saved");
                } else
                    send_line("ERR NVS write failed");
            } else
                send_line("ERR unknown key (unit_id,fw_ver,status_reg)");
        } else if (strncasecmp(line, "GET ", 4) == 0) {
            char *key = line + 4;
            trim(key);
            char buf[NVS_CONFIG_MAX_LEN];
            if (strcmp(key, "status_reg") == 0) {
                uint16_t reg_val = nvs_config_get_status_reg(0x0000);
                snprintf(buf, sizeof(buf), "0x%04X", reg_val);
                send_line(buf);
            } else {
                const char *nkey = NULL;
                if (strcmp(key, "unit_id") == 0) nkey = NVS_KEY_UNIT_ID;
                if (strcmp(key, "fw_ver")  == 0) nkey = NVS_KEY_FW_VER;
                if (nkey) {
                    nvs_config_get_string(nkey, buf, sizeof(buf), "");
                    send_line(buf[0] ? buf : "(not set)");
                } else
                    send_line("ERR unknown key");
            }
        } else if (strcasecmp(line, "LIST") == 0) {
            list_all();
        } else if (strcasecmp(line, "REBOOT") == 0) {
            send_line("Rebooting...");
            vTaskDelay(pdMS_TO_TICKS(200));
            esp_restart();
        } else {
            /* Debug: show what we received so paste/typing issues can be seen */
            ESP_LOGI(TAG, "cfg received %d chars: [%s]", len, line);
            send_line("? SET key=val | GET key | LIST | REBOOT");
        }
        
        // Show prompt after command completes
        printf("CFG> ");
        fflush(stdout);
    }
}

bool config_uart_start(void) {
    if (xTaskCreate(config_uart_task, "cfg_uart", CONFIG_TASK_STACK, NULL, 5, NULL) != pdPASS) {
        ESP_LOGW(TAG, "Config shell task create failed");
        return false;
    }
    ESP_LOGD(TAG, "Config shell on console (same UART as monitor)");
    return true;
}
