#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "at_command_api.h"
#include "a7670e_config.h"      // modem UART mapping (UART/Pin selection)
#include "a7670e_sequences.h"
#include "server_keepalive.h"
#include "digital_input_alert.h"
#include "relay_control.h"
#include "ble_gatt_server.h"
#include "modem_task_control.h"
#include "nvs_flash.h"
#include "nvs_config.h"
#include "fota_modem.h"
#include "config_uart.h"
#include "at_command_examples.h"
#include "wifi_manager.h"
#include "wifi_tcp_client.h"
#include "unit_ready.h"

// Forward declaration for integration demo
extern void start_integration_demo(void);
extern void start_modem_init(void);
extern void start_tcp_examples(void);

////////////////////////////////////////////////////////////////////
//board: ESP32 VROOM BOARD uart 2 pins: RX: 16, TX: 17
#define UART_NUM              A7670E_UART_NUM           // UART port (from a7670e_config.h)
#define UART_RX_PIN           A7670E_UART_RX_PIN        // ESP32 RX (from modem TX)
#define UART_TX_PIN           A7670E_UART_TX_PIN        // ESP32 TX (to modem RX)
/////////////////////////////////////////////////////////////////////
#define UART_BAUD_RATE        115200
#define UART_BUF_SIZE         1024                 // Driver buffer size
/* RX must hold a full binary line until \\n (OK) — 1024 was too small for 1500 B FOTA chunks. */
#define RX_BUFFER_SIZE        4096
/* LINE_BUFFER_SIZE from at_command_api.h */
#define MAX_AT_COMMAND_LEN    64                   // Maximum AT command length
#define MAX_EXPECTED_RESP_LEN 64                   // Maximum expected response length
#define AT_TIMEOUT_MS         5000                 // AT command timeout in ms
#define AT_QUEUE_SIZE         10                   // AT command queue size
#define WATCHDOG_TIMEOUT_SECONDS  8                // Task WDT timeout (panic if not fed)

/* Modem power sequencing timing (tune for hardware). */
#define MODEM_SYSTEM_SETTLE_MS         200   /* was 2000; let tasks start, then proceed quickly */
#define MODEM_FORCE_OFF_BEFORE_ON_MS  1000   /* was 3000; ensure a minimum OFF time before first ON */
#define MODEM_POWERUP_WAIT_MS         1200   /* was 3000; modem basic-AT retries will cover remaining boot */
#define APPMODEM_START_DELAY_MS          0   /* was 3000; no demo delay before starting modem init */

/* UART RX debug: 1 = log each read as "UART RX (ascii)" (use ESP_LOG_DEBUG for tag UART_AT).
 * Leave 0 during FOTA — binary image bytes look like random text, flood serial, and can fill
 * the AT response queue ("Response queue full, dropping"). */
#define DEBUG_UART_RX_DUMP_RAW  0

//static const char *TAG = "UART_AT";
static const char *TAG = "UART_AT";

// AT Command result enumeration  - declaired at h file
// typedef enum {
//     AT_RESULT_SUCCESS = 0,
//     AT_RESULT_TIMEOUT,
//     AT_RESULT_ERROR,
//     AT_RESULT_UNEXPECTED_RESPONSE
// } at_result_t;

// AT Command structure
typedef struct {
    char command[MAX_AT_COMMAND_LEN];               // AT command to send
    char expected_response[MAX_EXPECTED_RESP_LEN];  // Expected response from modem
    uint32_t timeout_ms;                            // Timeout for this command
    bool wait_for_ok;                               // Whether to wait for OK after expected response
    at_result_t *result;                            // Pointer to store result
    SemaphoreHandle_t completion_sem;               // Semaphore to signal completion
} at_command_t;

// Global variables
static uint8_t rx_buffer[RX_BUFFER_SIZE];
static size_t rx_buffer_pos = 0;
static char line_buffer[LINE_BUFFER_SIZE];
static QueueHandle_t at_command_queue;
static SemaphoreHandle_t uart_mutex;
char last_response[LINE_BUFFER_SIZE]; // Made public for examples
/** Matched data line from last send_at_command_ex (not overwritten by process_line "OK"). Use for CREG etc. */
char last_matched_response[LINE_BUFFER_SIZE];
static SemaphoreHandle_t response_ready_sem;

// Response queue for handling multiple quick responses (URCs: RING, +CLIP, +CLCC, +CGEV, +CIPRXGET, etc.)
/* Bursts of TCP-as-lines (+IPD / long payloads) can fill a small queue and drop URCs/OK. */
#define RESPONSE_QUEUE_SIZE 56
typedef struct {
    uint16_t len;
    char data[LINE_BUFFER_SIZE];
} modem_rsp_line_t;
static QueueHandle_t response_queue;

/* wait_for_line_containing() runs on caller task (e.g. modem_init, srv_ka) after CIPSEND — avoid ~3.2k stack. */
static modem_rsp_line_t s_wait_line_item;
static char s_wait_line_cstr[LINE_BUFFER_SIZE + 1];
static SemaphoreHandle_t s_wait_line_mutex;

/* Only at_command_task drains the inner AT response loop. */
static modem_rsp_line_t s_at_dequeue_rsp;
static char s_at_dequeue_buf[LINE_BUFFER_SIZE + 1];

// Task control variables
static TaskHandle_t modem_init_task_handle = NULL;
static bool modem_init_active = false;
static SemaphoreHandle_t init_control_mutex;

/* Modem connection state for status LED. */
static volatile bool modem_connected = false;

/* Modem boot URC gating (cold power-up): wait for these before sending first AT. */
static volatile bool s_modem_urc_atready = false;
static volatile bool s_modem_urc_cpin_ready = false;
static volatile bool s_modem_urc_sms_done = false;

/* Modem status LED task handle (GPIO2 per a7670e_config.h). */
static TaskHandle_t modem_status_led_task_handle = NULL;
/** When true, GPIO2 follows WiFi link (WiFi-only); when false, follows modem init/conn/FOTA. */
static bool s_modem_led_wifi_link_mode = false;
/* Local button task handle (GPIO15 active-low input). */
static TaskHandle_t local_button_task_handle = NULL;

static void modem_status_led_task(void *arg);
static void local_button_task(void *arg);

// Function prototypes
static void uart_rx_task(void *arg);
static void at_command_task(void *arg);
static void modem_init_task(void *arg);
at_result_t send_at_command(const char *command, const char *expected_response, uint32_t timeout_ms);
static bool process_line(const char *line, size_t line_len);
static bool response_matches(const char *response, const char *expected);

// Task control functions (public API)
bool start_modem_init_task(void);
bool stop_modem_init_task(void);
bool is_modem_init_active(void);

/**
 * @brief Modem status LED on GPIO2: modem path = init/conn/FOTA patterns; WiFi-only = 500/500 no IP,
 *        150/2000 with IP, solid ON while server command is handled (wifi_manager_link_led_command_busy).
 */
static void modem_status_led_task(void *arg)
{
    (void)arg;

    gpio_reset_pin(A7670E_MODEM_STATUS_LED_GPIO);
    gpio_set_direction(A7670E_MODEM_STATUS_LED_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(A7670E_MODEM_STATUS_LED_GPIO, 0); /* OFF */

    while (1) {
        if (s_modem_led_wifi_link_mode) {
            if (wifi_manager_link_led_command_busy()) {
                gpio_set_level(A7670E_MODEM_STATUS_LED_GPIO, 1);
                vTaskDelay(pdMS_TO_TICKS(100));
                continue;
            }
            if (!wifi_manager_is_connected()) {
                gpio_set_level(A7670E_MODEM_STATUS_LED_GPIO, 1);
                vTaskDelay(pdMS_TO_TICKS(500));
                gpio_set_level(A7670E_MODEM_STATUS_LED_GPIO, 0);
                vTaskDelay(pdMS_TO_TICKS(500));
            } else {
                gpio_set_level(A7670E_MODEM_STATUS_LED_GPIO, 1);
                vTaskDelay(pdMS_TO_TICKS(150));
                gpio_set_level(A7670E_MODEM_STATUS_LED_GPIO, 0);
                vTaskDelay(pdMS_TO_TICKS(2000));
            }
            continue;
        }

        bool init_active = modem_init_active;
        bool connected = modem_connected;

        if (!init_active && !connected) {
            /* Idle: LED off */
            gpio_set_level(A7670E_MODEM_STATUS_LED_GPIO, 0);
            vTaskDelay(pdMS_TO_TICKS(300));
        } else if (init_active && !connected) {
            /* Init / connect phase: solid ON */
            gpio_set_level(A7670E_MODEM_STATUS_LED_GPIO, 1);
            vTaskDelay(pdMS_TO_TICKS(200));
        } else if (fota_session_active()) {
            /* FOTA in progress: fast blink */
            gpio_set_level(A7670E_MODEM_STATUS_LED_GPIO, 1);
            vTaskDelay(pdMS_TO_TICKS(300));
            gpio_set_level(A7670E_MODEM_STATUS_LED_GPIO, 0);
            vTaskDelay(pdMS_TO_TICKS(300));
        } else {
            /* Connected: blink 300 ms ON, 2300 ms OFF */
            gpio_set_level(A7670E_MODEM_STATUS_LED_GPIO, 1);
            vTaskDelay(pdMS_TO_TICKS(300));
            gpio_set_level(A7670E_MODEM_STATUS_LED_GPIO, 0);
            vTaskDelay(pdMS_TO_TICKS(2300));
        }
    }
}

/**
 * @brief Extract complete lines from RX buffer and process them
 */
static void process_rx_buffer(void) {
    /* IMPORTANT: rx_buffer is binary data and may contain 0x00 bytes.
     * Do not use C-string functions like strchr/strstr on it.
     */
    uint8_t *start = rx_buffer;
    size_t avail = rx_buffer_pos;
    uint8_t *end;

    while (avail > 0 && (end = memchr(start, '\n', avail)) != NULL) {
        size_t line_len = (size_t)(end - start); /* excludes '\n' */

        /* Trim trailing '\r' characters (handles \r\n and \r\r\n cases). */
        while (line_len > 0 && start[line_len - 1] == '\r') {
            line_len--;
        }

        size_t eff_len = line_len;
        if (eff_len >= LINE_BUFFER_SIZE) {
            ESP_LOGW(TAG, "RX line %u bytes, truncating to %d", (unsigned)eff_len, LINE_BUFFER_SIZE - 1);
            eff_len = LINE_BUFFER_SIZE - 1;
        }
        memcpy(line_buffer, start, eff_len);
        line_buffer[eff_len] = '\0';

        /* Normalize any stray '\r' inside the line (some modems echo "AT\r\r\nOK\r\n"). */
        for (size_t i = 0; i < eff_len; ++i) {
            if (line_buffer[i] == '\r') {
                line_buffer[i] = ' ';
            }
        }

        /* Skip leading whitespace/CR/LF so URCs like "\r\n+CIPRXGET: 1,1" are matched. */
        char *p = line_buffer;
        while (*p == '\r' || *p == '\n' || *p == ' ' || *p == '\t') {
            p++;
        }
        if (*p != '\0') {
            size_t plen = eff_len - (size_t)(p - line_buffer);
            bool looks_text = true;
            for (size_t i = 0; i < plen && i < 80; i++) {
                unsigned char c = (unsigned char)p[i];
                if (c < 0x20u && c != '\t' && c != '\r' && c != '\n') {
                    looks_text = false;
                    break;
                }
            }
            if (looks_text && plen < 200) {
                if (fota_session_active()) {
                    ESP_LOGD(TAG, "RX Line: %s", p);
                } else {
                    ESP_LOGI(TAG, "RX Line: %s", p);
                }
            } else {
                if (fota_session_active()) {
                    ESP_LOGD(TAG, "RX chunk: %u bytes", (unsigned)plen);
                } else {
                    ESP_LOGI(TAG, "RX chunk: %u bytes", (unsigned)plen);
                }
            }
            process_line(p, plen);
        }

        /* Advance past '\n'. */
        size_t consumed = (size_t)((end - start) + 1);
        start += consumed;
        avail -= consumed;
    }

    /* Move any remaining bytes to the beginning of the buffer. */
    if (avail > 0 && start != rx_buffer) {
        memmove(rx_buffer, start, avail);
    }
    rx_buffer_pos = avail;

    /* Special handling for bare '>' CIPSEND prompt (often sent without \r\n).
     * If the remaining buffer only contains '>' (plus optional spaces/\r),
     * treat it as a complete line so CIPSEND can see the prompt.
     */
    if (rx_buffer_pos > 0) {
        bool only_prompt = false;
        for (size_t i = 0; i < rx_buffer_pos; ++i) {
            if (rx_buffer[i] == '>') {
                // Mark that we saw the prompt character
                only_prompt = true;
            } else if (rx_buffer[i] != '\r' && rx_buffer[i] != ' ' && rx_buffer[i] != '\t') {
                // Some other non-whitespace/non-CR character present – not a pure prompt
                only_prompt = false;
                break;
            }
        }

        if (only_prompt) {
            line_buffer[0] = '>';
            line_buffer[1] = '\0';
            if (fota_session_active()) {
                ESP_LOGD(TAG, "RX Line: %s", line_buffer);
            } else {
                ESP_LOGI(TAG, "RX Line: %s", line_buffer);
            }
            process_line(line_buffer, 1);
            rx_buffer_pos = 0;
        }
    }
}

/**
 * @brief Process a complete line received from modem
 */
static bool process_line(const char *line, size_t line_len) {
    /* Modem boot URCs (cold power-up): use as a gate before starting AT traffic. */
    if (strcmp(line, "*ATREADY: 1") == 0) {
        s_modem_urc_atready = true;
    } else if (strcmp(line, "+CPIN: READY") == 0) {
        s_modem_urc_cpin_ready = true;
    } else if (strcmp(line, "SMS DONE") == 0) {
        s_modem_urc_sms_done = true;
    }

    /* Buffered mode: modem notifies with +CIPRXGET: 1,<link> when data is buffered (mode 1). */
    if (strstr(line, "+CIPRXGET:") == line) {
        int mode = 0, val = 0;
        if (sscanf(line, "+CIPRXGET: %d,%d", &mode, &val) == 2 && mode == 1) {
            server_on_ciprxget_urc(val, 0);
        }
    }

    /* +IPCLOSE: <link>,<reason> – TCP connection closed by server/network; trigger reconnect for link 1 */
    if (strstr(line, "+IPCLOSE:") != NULL) {
        int link_id = 0;
        const char *p = strstr(line, "+IPCLOSE:");
        if (sscanf(p, "+IPCLOSE: %d", &link_id) == 1 || sscanf(p, "+IPCLOSE:%d", &link_id) == 1) {
            server_on_ipclose(link_id);
        }
    }

    /* Incoming call: +CLCC: 1,1,4,0,0,"0522784873",129,"" → caller in first quoted field */
    if (strstr(line, "+CLCC:") == line) {
        const char *q1 = strchr(line, '"');
        if (q1) {
            q1++;
            const char *q2 = strchr(q1, '"');
            if (q2 != NULL && (size_t)(q2 - q1) < 32) {
                char caller[32];
                size_t clen = (size_t)(q2 - q1);
                memcpy(caller, q1, clen);
                caller[clen] = '\0';
                server_on_ring(caller);
            }
        }
    }

    /* Copy for AT command handler (NUL-terminated; may truncate for display). */
    size_t lr = line_len < LINE_BUFFER_SIZE - 1 ? line_len : LINE_BUFFER_SIZE - 1;
    memcpy(last_response, line, lr);
    last_response[lr] = '\0';

    modem_rsp_line_t item;
    item.len = (uint16_t)(line_len > LINE_BUFFER_SIZE ? LINE_BUFFER_SIZE : line_len);
    memcpy(item.data, line, item.len);

    if (xQueueSend(response_queue, &item, 0) != pdTRUE) {
        ESP_LOGW(TAG, "Response queue full, dropping (%u bytes)", (unsigned)item.len);
    }
    
    // Signal that a response is ready
    xSemaphoreGive(response_ready_sem);
    
    return true;
}

/**
 * @brief Check if modem response matches expected response
 */
static bool response_matches(const char *response, const char *expected) {

    // Simple substring match - can be enhanced for more complex matching
    bool matches = (strstr(response, expected) != NULL);
    ESP_LOGD(TAG, "response_matches: '%s' contains '%s' = %s", response, expected, matches ? "YES" : "NO");
    return matches;
}

/**
 * @brief Return true only if the line is exactly "OK" (with optional leading/trailing whitespace).
 *        Used so "HTTP/1.1 200 OK" is not mistaken for the AT command response "OK".
 */
static bool line_is_only_ok(const char *line) {
    while (*line == ' ' || *line == '\t') line++;
    if (line[0] != 'O' || line[1] != 'K') return false;
    line += 2;
    while (*line == ' ' || *line == '\t') line++;
    return *line == '\0';
}

/** SIMCOM: AT+CIPRXGET=2 with nothing to read returns +IP ERROR: No data (not a hard failure). */
static bool line_is_ciprxget_no_data_urc(const char *line)
{
    if (strstr(line, "+IP ERROR:") == NULL) {
        return false;
    }
    return (strstr(line, "no data") != NULL || strstr(line, "No data") != NULL ||
            strstr(line, "NO DATA") != NULL);
}

static bool line_is_only_error(const char *line)
{
    while (*line == ' ' || *line == '\t' || *line == '\r' || *line == '\n') {
        line++;
    }
    return (strncmp(line, "ERROR", 5) == 0 &&
            (line[5] == '\0' || line[5] == ' ' || line[5] == '\t'));
}

static bool line_is_fatal_at_error(const char *line)
{
    if (strstr(line, "FAIL") != NULL) {
        return true;
    }
    if (line_is_ciprxget_no_data_urc(line)) {
        return false;
    }
    return (strstr(line, "ERROR") != NULL);
}

/**
 * After synthetic success for empty CIPRXGET=2 read, drop trailing OK/ERROR/+IP ERROR lines
 * so the next AT command does not see a stale "ERROR".
 */
static void drain_trailing_ciprxget2_noise(void)
{
    modem_rsp_line_t d;
    for (int k = 0; k < 8; k++) {
        TickType_t wait = (k == 0) ? pdMS_TO_TICKS(35) : 0;
        if (xQueueReceive(response_queue, &d, wait) != pdTRUE) {
            return;
        }
        size_t n = d.len < LINE_BUFFER_SIZE - 1 ? d.len : LINE_BUFFER_SIZE - 1;
        char buf[LINE_BUFFER_SIZE];
        memcpy(buf, d.data, n);
        buf[n] = '\0';
        char *p = buf;
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
            p++;
        }
        if (line_is_only_ok(p) || line_is_only_error(p) || line_is_ciprxget_no_data_urc(p)) {
            continue;
        }
        if (xQueueSend(response_queue, &d, 0) != pdTRUE) {
            ESP_LOGW(TAG, "CIPRXGET=2 drain: queue full, dropping restored line");
        }
        return;
    }
}

static void log_uart_rx_ascii(const uint8_t *data, int len)
{
    if (!data || len <= 0) return;
    /* Show only the first chunk for readability; long payloads get truncated. */
    char out[96];
    int n = 0;
    int max = (int)sizeof(out) - 4; /* leave room for "..." and terminator */
    for (int i = 0; i < len && n < max; i++) {
        uint8_t c = data[i];
        if (c == '\r') {
            if (n + 2 <= max) { out[n++] = '\\'; out[n++] = 'r'; }
            else break;
        } else if (c == '\n') {
            if (n + 2 <= max) { out[n++] = '\\'; out[n++] = 'n'; }
            else break;
        } else if (c >= 32 && c <= 126) {
            out[n++] = (char)c;
        } else {
            out[n++] = '.';
        }
    }
    if (n == max && len > n) {
        out[n++] = '.';
        out[n++] = '.';
        out[n++] = '.';
    }
    out[n] = '\0';
    ESP_LOGD(TAG, "UART RX (ascii): %s", out);
}
//wait for modem startup URCs before sending first AT
static bool wait_for_modem_boot_urcs(uint32_t timeout_ms)
{
    TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(timeout_ms);
    while (xTaskGetTickCount() < deadline) {
        if (s_modem_urc_atready && s_modem_urc_cpin_ready && s_modem_urc_sms_done)
         return true;
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    return false;
}

/**
 * @brief UART RX Task - handles incoming data and line extraction
 */
static void uart_rx_task(void *arg) {
    (void)arg;
    if (esp_task_wdt_add(NULL) != ESP_OK) {
        ESP_LOGW(TAG, "UART RX task: WDT add failed");
    }

    uint8_t data[64];

    ESP_LOGI(TAG, "UART RX Task started - monitoring GPIO%d for data", UART_RX_PIN);
    
    while (1) {
        esp_task_wdt_reset();
        // Wait for RX data, up to 100ms
        int len = uart_read_bytes(UART_NUM, data, sizeof(data), pdMS_TO_TICKS(100));
        if (len > 0) {
            if (fota_session_active()) {
                ESP_LOGD(TAG, "UART RX: Got %d bytes", len);
            } else {
                ESP_LOGI(TAG, "UART RX: Got %d bytes", len);
            }
            
            /* Optional: log raw bytes (ASCII view) for UART debug (helps detect URCs/OK). */
#if DEBUG_UART_RX_DUMP_RAW
            log_uart_rx_ascii(data, len);
#endif
            
            // Copy received data to buffer (with overflow protection)
            size_t to_copy = len;
            if (rx_buffer_pos + to_copy >= RX_BUFFER_SIZE) {
                ESP_LOGW(TAG, "RX buffer overflow, resetting");
                rx_buffer_pos = 0;
                to_copy = (len < RX_BUFFER_SIZE) ? len : RX_BUFFER_SIZE - 1;
            }
            
            memcpy(rx_buffer + rx_buffer_pos, data, to_copy);
            rx_buffer_pos += to_copy;
            
            // Process complete lines
            process_rx_buffer();
        }

        vTaskDelay(pdMS_TO_TICKS(10)); // Small delay to prevent overwhelming
    }
}

/**
 * @brief AT Command Task - processes AT commands from queue
 */
static void at_command_task(void *arg) {
    at_command_t cmd;
    
    while (1) {
        // Wait for AT command in queue
        if (xQueueReceive(at_command_queue, &cmd, portMAX_DELAY) == pdTRUE) {
            if (fota_session_active()) {
                ESP_LOGD(TAG, "Processing AT command: %s", cmd.command);
            } else {
                ESP_LOGI(TAG, "Processing AT command: %s", cmd.command);
            }
            
            // Take UART mutex
            if (xSemaphoreTake(uart_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
                // Clear any pending responses
                xSemaphoreTake(response_ready_sem, 0);
                
                // Send AT command
                int tx_bytes = uart_write_bytes(UART_NUM, cmd.command, strlen(cmd.command));
                uart_write_bytes(UART_NUM, "\r\n", 2);
                
                if (fota_session_active()) {
                    ESP_LOGD(TAG, "Sent: %s (TX bytes: %d)", cmd.command, tx_bytes);
                } else {
                    ESP_LOGI(TAG, "Sent: %s (TX bytes: %d)", cmd.command, tx_bytes);
                }
                
                // Flush TX to ensure data is sent immediately
                uart_wait_tx_done(UART_NUM, pdMS_TO_TICKS(100));
                
                // Two-phase response handling: Data response (optional) + OK
                bool data_received = false;
                TickType_t start_time = xTaskGetTickCount();
                TickType_t timeout_ticks = pdMS_TO_TICKS(cmd.timeout_ms);
                
                // Determine if we're waiting for data response or just OK
                bool expect_data = (cmd.expected_response[0] != '\0' && strcmp(cmd.expected_response, "OK") != 0);
                
                // ESP_LOGI(TAG, "AT command setup: expect_data=%s, wait_for_ok=%s, expected='%s'",
                //     expect_data ? "true" : "false", cmd.wait_for_ok ? "true" : "false", cmd.expected_response);
                
                while ((xTaskGetTickCount() - start_time) < timeout_ticks) {
                    if (xSemaphoreTake(response_ready_sem, pdMS_TO_TICKS(100)) == pdTRUE) {
                        // Process all available responses in the queue
                        while (xQueueReceive(response_queue, &s_at_dequeue_rsp, 0) == pdTRUE) {
                            size_t cn = s_at_dequeue_rsp.len < LINE_BUFFER_SIZE ? s_at_dequeue_rsp.len : LINE_BUFFER_SIZE;
                            memcpy(s_at_dequeue_buf, s_at_dequeue_rsp.data, cn);
                            s_at_dequeue_buf[cn] = '\0';
                            ESP_LOGD(TAG, "AT response (%u bytes)", (unsigned)s_at_dequeue_rsp.len);
                            /* CIPRXGET=2 poll: empty modem buffer → +IP ERROR: No data (or bare ERROR). */
                            if (expect_data && strncmp(cmd.command, "AT+CIPRXGET=2,", 14) == 0) {
                                if (line_is_ciprxget_no_data_urc(s_at_dequeue_buf) ||
                                    line_is_only_error(s_at_dequeue_buf)) {
                                    int cip_lk = 1;
                                    (void)sscanf(cmd.command, "AT+CIPRXGET=2,%d", &cip_lk);
                                    snprintf(last_response, sizeof(last_response),
                                             "+CIPRXGET: 2,%d,0,0", cip_lk);
                                    snprintf(last_matched_response, sizeof(last_matched_response), "%s",
                                             last_response);
                                    *cmd.result = AT_RESULT_SUCCESS;
                                    ESP_LOGD(TAG, "CIPRXGET=2: no data in buffer (retry later)");
                                    drain_trailing_ciprxget2_noise();
                                    goto command_complete;
                                }
                            }
                            if (line_is_fatal_at_error(s_at_dequeue_buf)) {
                                *cmd.result = AT_RESULT_ERROR;
                                ESP_LOGW(TAG, "AT command ERROR: %s", s_at_dequeue_buf);
                                goto command_complete;
                            }

                            // Phase 1: Check for expected data response (if expected)
                            if (expect_data && !data_received && response_matches(s_at_dequeue_buf, cmd.expected_response)) {
                                data_received = true;
                                memcpy(last_response, s_at_dequeue_rsp.data, cn);
                                last_response[cn] = '\0';
                                memcpy(last_matched_response, s_at_dequeue_rsp.data, cn);
                                last_matched_response[cn] = '\0';
                                ESP_LOGD(TAG, "Data response received (%u bytes)", (unsigned)s_at_dequeue_rsp.len);
                                
                                // If we don't need to wait for OK, we're done
                                if (!cmd.wait_for_ok) {
                                    *cmd.result = AT_RESULT_SUCCESS;
                                  //  ESP_LOGI(TAG, "AT command SUCCESS: Got data '%s' (not waiting for OK)", cmd.expected_response);
                                    goto command_complete;
                                }
                                // Continue processing more responses for OK
                                continue;
                            }
                            
                            // Phase 2: Check for OK response (only if wait_for_ok is true).
                            // Use exact "OK" line match so "HTTP/1.1 200 OK" is not consumed as AT OK.
                            if (cmd.wait_for_ok && line_is_only_ok(s_at_dequeue_buf)) {
                                if (expect_data) {
                                    // For data commands: need both data AND OK
                                    if (data_received) {
                                        *cmd.result = AT_RESULT_SUCCESS;
                                       // ESP_LOGI(TAG, "AT command SUCCESS: Got data '%s' + OK", cmd.expected_response);
                                        goto command_complete;
                                    }
                                    // Got OK but no data yet - continue processing
                                    ESP_LOGI(TAG, "Got OK but still waiting for data response '%s'", cmd.expected_response);
                                } else {
                                    // For simple commands: just OK is enough
                                    *cmd.result = AT_RESULT_SUCCESS;
                                    ESP_LOGI(TAG, "AT command SUCCESS: Got OK");
                                    goto command_complete;
                                }
                            }
                            
                            // Handle case where expected_response is "OK" directly (exact line only)
                            if (!expect_data && cmd.expected_response[0] != '\0' && 
                                strcmp(cmd.expected_response, "OK") == 0 && 
                                line_is_only_ok(s_at_dequeue_buf)) {
                                *cmd.result = AT_RESULT_SUCCESS;
                               // ESP_LOGI(TAG, "AT command SUCCESS: Expected and got OK");
                                goto command_complete;
                            }
                            
                            // Log unmatched responses for debugging
                            ESP_LOGD(TAG, "Response '%s' didn't match any expected pattern", s_at_dequeue_buf);
                        }
                    }
                }
                
                // If we reach here, it's a timeout
                *cmd.result = AT_RESULT_TIMEOUT;
                ESP_LOGW(TAG, "AT command TIMEOUT: %s", cmd.command);
                
command_complete:
                // Release UART mutex
                xSemaphoreGive(uart_mutex);
            } else {
                *cmd.result = AT_RESULT_ERROR;
                ESP_LOGE(TAG, "Failed to take UART mutex");
            }
            
            // Signal completion
            xSemaphoreGive(cmd.completion_sem);
        }
    }
}

/**
 * @brief Send AT command and wait for response (enhanced version with OK control)
 * @param command AT command to send (without \r\n)
 * @param expected_response Expected response substring
 * @param timeout_ms Timeout in milliseconds
 * @param wait_for_ok Whether to wait for OK after expected response
 * @return at_result_t Result of the command
 */
at_result_t send_at_command_ex(const char *command, const char *expected_response, uint32_t timeout_ms, bool wait_for_ok) {

    at_command_t cmd = {0};  //command structure
    at_result_t result = AT_RESULT_ERROR;
    
    // Prepare command structure
    strncpy(cmd.command, command, MAX_AT_COMMAND_LEN - 1);
    strncpy(cmd.expected_response, expected_response, MAX_EXPECTED_RESP_LEN - 1);
    cmd.timeout_ms = timeout_ms;
    cmd.wait_for_ok = wait_for_ok;
    cmd.result = &result;
    cmd.completion_sem = xSemaphoreCreateBinary(); //create semaphore

    if (cmd.completion_sem == NULL) {
        ESP_LOGE(TAG, "Failed to create completion semaphore");
        return AT_RESULT_ERROR;
    }
    
    // Send command to queue
    if (xQueueSend(at_command_queue, &cmd, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to send AT command to queue");
        vSemaphoreDelete(cmd.completion_sem);
        return AT_RESULT_ERROR;
    }
    
    // Wait for completion; at_command_task sets result before giving the semaphore.
    if (xSemaphoreTake(cmd.completion_sem, pdMS_TO_TICKS(timeout_ms + 1000)) != pdTRUE) {
        ESP_LOGE(TAG, "AT command completion timeout");
        result = AT_RESULT_TIMEOUT;
    }
    
    vSemaphoreDelete(cmd.completion_sem);
    return result;
}

/**
 * @brief Send AT command and wait for response (backward compatibility version)
 * @param command AT command to send (without \r\n)
 * @param expected_response Expected response substring
 * @param timeout_ms Timeout in milliseconds
 * @return at_result_t Result of the command
 */
at_result_t send_at_command(const char *command, const char *expected_response, uint32_t timeout_ms) {

    // Default to waiting for OK for backward compatibility
    return send_at_command_ex(command, expected_response, timeout_ms, true);
}

/**
 * @brief Wait for a line containing substr from modem (e.g. "200 OK"). Call when no AT command is in progress.
 */
at_result_t wait_for_line_containing(const char *substr, uint32_t timeout_ms) {
    if (s_wait_line_mutex == NULL) {
        ESP_LOGE(TAG, "wait_for_line: mutex not initialized");
        return AT_RESULT_ERROR;
    }
    if (xSemaphoreTake(s_wait_line_mutex, portMAX_DELAY) != pdTRUE) {
        return AT_RESULT_ERROR;
    }
    at_result_t out = AT_RESULT_TIMEOUT;
    TickType_t start = xTaskGetTickCount();
    TickType_t timeout_ticks = pdMS_TO_TICKS(timeout_ms);
    while ((xTaskGetTickCount() - start) < timeout_ticks) {
        if (xQueueReceive(response_queue, &s_wait_line_item, pdMS_TO_TICKS(200)) == pdTRUE) {
            size_t n = s_wait_line_item.len < LINE_BUFFER_SIZE ? s_wait_line_item.len : LINE_BUFFER_SIZE;
            memcpy(s_wait_line_cstr, s_wait_line_item.data, n);
            s_wait_line_cstr[n] = '\0';
            if (strstr(s_wait_line_cstr, substr) != NULL) {
                out = AT_RESULT_SUCCESS;
                break;
            }
        }
    }
    if (out != AT_RESULT_SUCCESS) {
        ESP_LOGW(TAG, "wait_for_line: timeout waiting for '%s'", substr);
    }
    xSemaphoreGive(s_wait_line_mutex);
    return out;
}

/**
 * @brief Get next line from response queue (for draining payload after AT+CIPRXGET=2,1).
 */
bool get_next_response_line_ex(char *buf, size_t buf_size, size_t *out_len, uint32_t timeout_ms)
{
    if (buf == NULL || buf_size == 0) {
        return false;
    }
    modem_rsp_line_t item;
    if (xQueueReceive(response_queue, &item, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
        return false;
    }
    size_t copy = item.len < buf_size ? item.len : buf_size;
    memcpy(buf, item.data, copy);
    if (copy < buf_size) {
        buf[copy] = '\0';
    } else {
        buf[buf_size - 1] = '\0';
    }
    if (out_len != NULL) {
        *out_len = item.len;
    }
    return true;
}

bool get_next_response_line(char *buf, size_t buf_size, uint32_t timeout_ms)
{
    return get_next_response_line_ex(buf, buf_size, NULL, timeout_ms);
}

/**
 * @brief Send raw bytes on UART (e.g. after CIPSEND ">").
 */
void uart_send_raw_bytes(const uint8_t *data, size_t len) {
    if (data == NULL || len == 0) return;
    if (xSemaphoreTake(uart_mutex, pdMS_TO_TICKS(2000)) == pdTRUE) {
        uart_write_bytes(UART_NUM, data, len);
        uart_wait_tx_done(UART_NUM, pdMS_TO_TICKS(500));
        xSemaphoreGive(uart_mutex);
    }
}

/**
 * @brief A7670E: send AT+CIPSEND=socket_id,len, wait for ">" prompt, then send exactly len bytes.
 * Do not send payload unless ">" was received (e.g. on CIPERROR 4 we abort).
 */
at_result_t send_at_then_raw_data(int socket_id, size_t len, const uint8_t *payload) {
    char cmd[48];
    snprintf(cmd, sizeof(cmd), "AT+CIPSEND=%d,%u", socket_id, (unsigned)len);

    // Try up to two times to get the '>' prompt, with a 1s timeout each time.
    for (int attempt = 1; attempt <= 2; ++attempt) {
        ESP_LOGD(TAG, "CIPSEND attempt %d: waiting up to 1s for '>' prompt", attempt);
        at_result_t r = send_at_command_ex(cmd, ">", 1000, false);
        if (r == AT_RESULT_SUCCESS) {
            vTaskDelay(pdMS_TO_TICKS(50));
            ESP_LOGD(TAG, "Got '>' (attempt %d), sending exactly %u bytes", attempt, (unsigned)len);
            uart_send_raw_bytes(payload, len);
            return wait_for_line_containing("+CIPSEND", 10000);
        }
        // If first attempt failed, small delay before retrying
        if (attempt == 1) {
            ESP_LOGW(TAG, "No '>' prompt on first CIPSEND attempt, retrying once...");
            vTaskDelay(pdMS_TO_TICKS(200));
        } else {
            ESP_LOGW(TAG, "Did not get '>' prompt after 2 attempts - not sending payload (required before CIPSEND data)");
            return r;
        }
    }
    // Should not reach here
    return AT_RESULT_TIMEOUT;
}

/**
 * @brief Execute a sequence of AT commands with enhanced error handling
 * @param sequence Array of command definitions
 * @param sequence_length Number of commands in sequence
 * @param sequence_name Name for logging purposes
 * @return Number of successful commands, -1 if critical failure
 */
int execute_command_sequence(const at_command_def_t *sequence, int sequence_length, const char *sequence_name) {
    ESP_LOGI(TAG, "Starting command sequence: %s (%d commands)", sequence_name, sequence_length);
    
    int successful_commands = 0;
    
    for (int i = 0; i < sequence_length; i++) {
        ESP_LOGI(TAG, "Sequence %s - Step %d/%d: %s", sequence_name, i + 1, sequence_length, sequence[i].description);
        
        at_result_t result;
        if (sequence[i].wait_for_ok) {
            // Use standard API (waits for OK)
            result = send_at_command(
                sequence[i].command,
                sequence[i].expected_response,
                sequence[i].timeout_ms
            );
        } else {
            // Use enhanced API (data only, no OK wait)
            result = send_at_command_ex(
                sequence[i].command,
                sequence[i].expected_response,
                sequence[i].timeout_ms,
                false
            );
        }
        
        if (result == AT_RESULT_SUCCESS) {
            successful_commands++;
            ESP_LOGI(TAG, "✓ %s - SUCCESS", sequence[i].description);
        } else {
            ESP_LOGW(TAG, "✗ %s - FAILED (result: %d)", sequence[i].description, result);
            
            // Log failure action if specified
            if (sequence[i].failure_action && strlen(sequence[i].failure_action) > 0) {
                ESP_LOGW(TAG, "Failure action: %s", sequence[i].failure_action);
            }
            
            // Check if this is a critical command
            if (sequence[i].critical) {
                ESP_LOGE(TAG, "Critical command failed - stopping sequence %s", sequence_name);
                return -1; // Indicate critical failure
            }
        }
        
        /* Give modem time to be ready for next command (some boards need a bit more) */
        vTaskDelay(pdMS_TO_TICKS(300));
    }
    
    ESP_LOGI(TAG, "Command sequence %s complete: %d/%d commands successful", 
             sequence_name, successful_commands, sequence_length);
    
    return successful_commands;
}

/**
 * @brief Network Reconnection - Re-establish network connection (optional, not currently called)
 */
__attribute__((unused))
static int reconnect_network(void) {
    // Network reconnection sequence
    static const at_command_def_t reconnect_commands[] = {
        // Check network registration
        {"AT+CREG?", "+CREG:", 3000, false, false, "Check network registration", "Wait for registration"},
        
        // Check signal quality
        {"AT+CSQ", "+CSQ:", 3000, false, false, "Check signal quality", "Move to better location"},
        
        // Reset network registration if needed
        {"AT+COPS=2", "OK", 30000, true, false, "Deregister from network", "Manual network selection"},
        {"AT+COPS=0", "OK", 60000, true, true, "Auto-register to network", "Check SIM card"},
        
        // Verify registration
        {"AT+CREG?", "+CREG:", 5000, false, true, "Verify network registration", "Check operator"}
    };
    
    return execute_command_sequence(reconnect_commands, 
                                   sizeof(reconnect_commands) / sizeof(reconnect_commands[0]), 
                                   "NETWORK_RECONNECT");
}

/**
 * @brief Modem initialization task that can be started/stopped on demand
 */
static void modem_init_task(void *arg) {

    ESP_LOGD(TAG, "Modem initialization task started");
    
    // Wait briefly for system to settle
    vTaskDelay(pdMS_TO_TICKS(MODEM_SYSTEM_SETTLE_MS));

    /* Configure modem power GPIO once. Always start with modem power switch OFF (active-low, HIGH = OFF)
     * for a guaranteed minimum off-time before the first controlled power-on, even if hardware briefly
     * enabled the modem during reset/boot.
     */
    gpio_reset_pin(A7670E_MODEM_PWR_GPIO);
    gpio_set_direction(A7670E_MODEM_PWR_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(A7670E_MODEM_PWR_GPIO, 1);  /* OFF */
    vTaskDelay(pdMS_TO_TICKS(MODEM_FORCE_OFF_BEFORE_ON_MS));  /* Ensure a minimum OFF time before first ON */

    bool connect_ok = false;
    const int max_attempts_per_cycle = 2;   /* First attempt + one full re-init/power-cycle on failure. */
    const int retry_delay_sec        = 5;   /* Wait before starting a new full init/connect cycle on hard failure. */
    int failed_cycles                = 0;   /* Count consecutive full init/connect cycles that failed. */

    /* Keep trying to bring modem + TCP link up until success. After several full failures, reset CPU (WD-like). */
    while (!connect_ok) {
        for (int attempt = 1; attempt <= max_attempts_per_cycle && !connect_ok; ++attempt) {
            /* Power ON modem (active-low switch) and wait for power-up. */
            gpio_set_level(A7670E_MODEM_PWR_GPIO, 0);
            ESP_LOGI(TAG, "Modem power GPIO %d LOW (ON, attempt %d/%d), waiting %d ms for power-up",
                     A7670E_MODEM_PWR_GPIO, attempt, max_attempts_per_cycle, MODEM_POWERUP_WAIT_MS);
            vTaskDelay(pdMS_TO_TICKS(MODEM_POWERUP_WAIT_MS));

            /* Cold boot: wait for startup URCs before sending first AT, to avoid early timeouts. */
            s_modem_urc_atready = false;
            s_modem_urc_cpin_ready = false;
            s_modem_urc_sms_done = false;
            if (!wait_for_modem_boot_urcs(7000)) {
                ESP_LOGW(TAG, "Startup URCs not complete (*ATREADY/+CPIN/SMS DONE) – continuing anyway");
            }

            /* Old code gated on *ATREADY: 1 here. With the current hardware (modem powered
             * directly from 5 V), that URC is not reliable as a gate. Instead we rely on the
             * robust basic-AT retry logic already in run_a7670e_init() (wait_for_basic_at),
             * so after this power-up delay we go straight into the normal init.
             */

            /* Initialize modem (A7670E sequence). Full mode then opens cellular TCP; slave mode uses WiFi for server. */
            int init_res = run_a7670e_init();
            if (init_res < 0) {
                ESP_LOGE(TAG, "Modem init (run_a7670e_init) failed on attempt %d/%d", attempt, max_attempts_per_cycle);
            } else if (nvs_config_connectivity_mode() == NVS_CONN_MODEM_SLAVE_WIFI) {
                connect_ok = true;
                ESP_LOGI(TAG, "Modem slave: RF/init OK — server path is WiFi (attempt %d/%d)",
                         attempt, max_attempts_per_cycle);
                break;
            } else {
                connect_ok = (run_a7670e_connect_and_register() == 0);
                if (connect_ok) {
                    ESP_LOGI(TAG, "Modem connect OK on attempt %d/%d", attempt, max_attempts_per_cycle);
                    break;
                } else {
                    ESP_LOGW(TAG, "Modem connect failed on attempt %d/%d (will power-cycle and retry if attempts remain)",
                             attempt, max_attempts_per_cycle);
                }
            }

            /* On failure: power OFF modem and wait a bit before next attempt (full re-init). */
            gpio_set_level(A7670E_MODEM_PWR_GPIO, 1);
            ESP_LOGW(TAG, "Modem power GPIO %d set HIGH (OFF) for power cycle", A7670E_MODEM_PWR_GPIO);
            vTaskDelay(pdMS_TO_TICKS(1000));
        }

        if (connect_ok) {
            break;
        }

        /* All attempts in this cycle failed – keep modem powered OFF and try again after a long delay. */
        gpio_set_level(A7670E_MODEM_PWR_GPIO, 1);
        failed_cycles++;
        ESP_LOGE(TAG, "Modem initialization/connect failed after %d attempts – cycle %d, will retry in %d s",
                 max_attempts_per_cycle, failed_cycles, retry_delay_sec);

        /* If modem has failed for even a single full cycle, restart CPU to mimic manual reset behavior. */
        if (failed_cycles >= 1) {
            ESP_LOGE(TAG, "Modem failed after %d full init/connect cycles – restarting CPU", failed_cycles);
            vTaskDelay(pdMS_TO_TICKS(500)); /* small delay to flush logs */
            esp_restart();
        }
        vTaskDelay(pdMS_TO_TICKS((uint32_t)retry_delay_sec * 1000));
    }

    if (connect_ok) {
        if (nvs_config_connectivity_mode() == NVS_CONN_MODEM_SLAVE_WIFI) {
            if (server_keepalive_ring_worker_start()) {
                modem_connected = true;
            }
        } else {
            server_keepalive_task_start();
            modem_connected = true;
        }
    }
    ESP_LOGI(TAG, "Modem initialization task completed - task will now terminate");
    
    // Mark initialization as completed and clean up
    if (xSemaphoreTake(init_control_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        modem_init_active = false;
        modem_init_task_handle = NULL;
        xSemaphoreGive(init_control_mutex);
    }
    
    /* Unit Ready after server path: WiFi TCP does it (200 OK). Pure modem uses this. */
    if (connect_ok && nvs_config_connectivity_mode() == NVS_CONN_MODEM_FULL) {
        unit_ready_try_announce_once();
    }
    // Delete this task
    vTaskDelete(NULL);
}

/**
 * @brief Local button task: GPIO15 active-low input with pull-up.
 *        On a short LOW pulse, activate both relays for 2 seconds.
 */
static void local_button_task(void *arg)
{
    (void)arg;

    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = (1ULL << A7670E_LOCAL_BUTTON_GPIO),
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_ENABLE,  /* External pull-up present; internal helps keep stable idle HIGH. */
    };
    gpio_config(&io_conf);

    bool last_level = gpio_get_level(A7670E_LOCAL_BUTTON_GPIO) ? true : false;
    TickType_t last_change = xTaskGetTickCount();

    ESP_LOGD(TAG, "Local button task started on GPIO%d (active-low)", A7670E_LOCAL_BUTTON_GPIO);

    while (1) {
        bool level = gpio_get_level(A7670E_LOCAL_BUTTON_GPIO) ? true : false;
        TickType_t now = xTaskGetTickCount();

        if (level != last_level) {
            last_level = level;
            last_change = now;
        }

        /* Simple debounce: consider a stable state if unchanged for at least 50 ms. */
        if ((now - last_change) >= pdMS_TO_TICKS(50)) {
            /* Detect a falling edge: HIGH -> LOW after debounce. */
            static bool prev_stable_high = true;
            if (!level && prev_stable_high) {
                prev_stable_high = false;

                relay_command_t cmd = {
                    .relay_number = 3,              /* both relays */
                    .duration_ms = 2000U,           /* 2 seconds */
                    .activate = true,
                };
                snprintf(cmd.description, sizeof(cmd.description), "Local button 2s both relays");
                (void)relay_execute_command(&cmd);
                ESP_LOGI(TAG, "Local button pressed: both relays ON for 2 s");
            } else if (level) {
                prev_stable_high = true;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

/**
 * @brief Demo task showing periodic modem monitoring (separate from init). Optional, not currently started.
 */
__attribute__((unused))
static void demo_task(void *arg) {  

    ESP_LOGI(TAG, "Demo monitoring task started");
    
    // Wait for system to settle
    vTaskDelay(pdMS_TO_TICKS(5000));
    
    // Run enhanced AT command examples first
    ESP_LOGI(TAG, "--- Running Enhanced AT Command Examples ---");
    ESP_LOGI(TAG, "--- At demo_task()-caling example_enhanced_at_commands()");
    example_enhanced_at_commands();

/////////////////////////////////////////////////////////////////////
   
    // Demo: Continuous monitoring (independent of init task)
    while (1) {
        ESP_LOGI(TAG, "--- Periodic modem status check ---");
        
        // First, test basic modem connectivity with simple AT
        ESP_LOGI(TAG, "Testing basic modem connectivity:");
        at_result_t basic_result = send_at_command("AT", "OK", 2000);
        if (basic_result == AT_RESULT_SUCCESS) {
            ESP_LOGI(TAG, "Basic AT test: SUCCESS - Modem is responding");
        } else {
            ESP_LOGW(TAG, "Basic AT test: FAILED - Modem not responding (result: %d)", basic_result);
            ESP_LOGW(TAG, "Check: 1) Power, 2) Wiring (TX↔RX), 3) Baud rate, 4) Modem initialization");
            
            // Suggest potential troubleshooting steps
            ESP_LOGW(TAG, "Troubleshooting suggestions:");
            ESP_LOGW(TAG, "- Try different baud rates: 9600, 38400, 57600, 115200");
            ESP_LOGW(TAG, "- Check if modem needs DTR/RTS signals");
            ESP_LOGW(TAG, "- Verify modem power and status LEDs");
            ESP_LOGW(TAG, "- Test with simple loopback (connect TX to RX temporarily)");
        }
        
        // Example of using enhanced API: Get signal strength without waiting for OK
        ESP_LOGI(TAG, "Testing enhanced API (data only):");
        at_result_t result = send_at_command_ex("AT+CSQ", "+CSQ:", 3000, false);
        if (result == AT_RESULT_SUCCESS) {
            ESP_LOGI(TAG, "Signal quality check (data only): SUCCESS");
        } else {
            ESP_LOGW(TAG, "Signal quality check (data only): FAILED");
        }
        
        // Example of standard API: Check network registration
        ESP_LOGI(TAG, "Testing standard API (with OK):");
        result = send_at_command("AT+CREG?", "+CREG:", 3000);
        if (result == AT_RESULT_SUCCESS) {
            ESP_LOGI(TAG, "Network registration check: SUCCESS");
        } else {
            ESP_LOGW(TAG, "Network registration check: FAILED");
        }
        
        // Wait 30 seconds before next check
        vTaskDelay(pdMS_TO_TICKS(30000));
    }
}

/**
 * @brief Start the modem initialization task
 * @return true if task started successfully, false otherwise
 */
bool start_modem_init_task(void) {

    if (xSemaphoreTake(init_control_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to take init control mutex");
        return false;
    }
    
    if (modem_init_active || modem_init_task_handle != NULL) {

        ESP_LOGW(TAG, "Modem init task is already active");
        xSemaphoreGive(init_control_mutex);
        return false;
    }
    
    // Create the modem init task
    BaseType_t result = xTaskCreate(

        modem_init_task,
        "modem_init_task",
        12288,
        NULL,
        9,  // Higher priority than demo task
        &modem_init_task_handle
    );
    
    if (result == pdPASS) {
        modem_init_active = true;
        ESP_LOGD(TAG, "Modem initialization task started successfully");
        xSemaphoreGive(init_control_mutex);
        return true;
    } else {
        ESP_LOGE(TAG, "Failed to create modem init task");
        modem_init_task_handle = NULL;
        xSemaphoreGive(init_control_mutex);
        return false;
    }
}

/**
 * @brief Stop the modem initialization task (if running)
 * @return true if task stopped successfully, false otherwise
 */
bool stop_modem_init_task(void) {
    if (xSemaphoreTake(init_control_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to take init control mutex");
        return false;
    }
    
    if (!modem_init_active || modem_init_task_handle == NULL) {
        ESP_LOGW(TAG, "Modem init task is not currently active");
        xSemaphoreGive(init_control_mutex);
        return false;
    }
    
    // Delete the task
    vTaskDelete(modem_init_task_handle);
    modem_init_task_handle = NULL;
    modem_init_active = false;
    
    ESP_LOGI(TAG, "Modem initialization task stopped");
    xSemaphoreGive(init_control_mutex);
    return true;
}

/**
 * @brief Check if modem initialization task is currently active
 * @return true if active, false otherwise
 */
bool is_modem_init_active(void) {
    bool active = false;
    if (xSemaphoreTake(init_control_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        active = modem_init_active;
        xSemaphoreGive(init_control_mutex);
    }
    
    return active;
}

static const char *reset_reason_str(esp_reset_reason_t r)
{
    switch (r) {
    case ESP_RST_POWERON:   return "POWERON";
    case ESP_RST_EXT:       return "EXT_PIN_EN";
    case ESP_RST_SW:        return "SW_esp_restart";
    case ESP_RST_PANIC:     return "PANIC";
    case ESP_RST_INT_WDT:   return "INT_WDT";
    case ESP_RST_TASK_WDT:  return "TASK_WDT";
    case ESP_RST_WDT:       return "WDT_OTHER";
    case ESP_RST_DEEPSLEEP: return "DEEPSLEEP";
    case ESP_RST_BROWNOUT:  return "BROWNOUT";
    case ESP_RST_SDIO:      return "SDIO";
    default:                return "UNKNOWN";
    }
}

/////////////////////////////////////////////////////////////////////////////////
void app_main(void) {

    /* ROM line "rst:0x1" is not always literal wall-power loss; compare to this. */
    esp_reset_reason_t rr = esp_reset_reason();
    ESP_LOGW(TAG, "CPU reset reason: %s (%d) — if BROWNOUT/EXT_PIN_EN, check supply & EN wiring",
             reset_reason_str(rr), (int)rr);

    ESP_LOGI(TAG, "app_main()-EG unit - ESP32 WROOM 32E - Modem A7670E");

    /* Reduce noisy component logs for cleaner startup output. */
    esp_log_level_set("gpio", ESP_LOG_WARN);         /* suppress GPIO[xx] info prints */
    esp_log_level_set("CFG_UART", ESP_LOG_WARN);     /* config shell prints its own banner/prompt */
    esp_log_level_set("MODEM_EXAMPLE", ESP_LOG_WARN);/* hide demo helper info logs */
    esp_log_level_set("wifi", ESP_LOG_WARN);         /* suppress ch/BW/state spam; keep W warnings */

    /* NVS: init first so status_reg can gate modem GPIO and tasks below. */
    esp_err_t nvs = nvs_flash_init();
    if (nvs == ESP_ERR_NVS_NO_FREE_PAGES || nvs == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }
    nvs_config_init();
    nvs_config_ensure_unit_id_default(A7670E_UNIT_ID);

    uint16_t early_sr = nvs_config_get_status_reg(0x0000);
    nvs_config_set_connectivity_mode_from_reg(early_sr);
    nvs_connectivity_mode_t conn = nvs_config_connectivity_mode();

    bool modem_on = (conn == NVS_CONN_MODEM_FULL || conn == NVS_CONN_MODEM_SLAVE_WIFI);
    bool wifi_on  = (conn == NVS_CONN_WIFI_ONLY || conn == NVS_CONN_MODEM_SLAVE_WIFI);
    bool modem_disabled = !modem_on;

    s_modem_led_wifi_link_mode = modem_disabled && wifi_on;

    if (!modem_on) {
        ESP_LOGI(TAG, "*** Modem path off (status_reg=0x%04X, WiFi-only) — UART/GPIO/modem tasks skipped ***", early_sr);
    } else {
        /* Keep modem supply OFF until UART/firmware are ready. GPIO12 active-low: HIGH = off. */
        gpio_reset_pin(A7670E_MODEM_PWR_GPIO);
        gpio_set_direction(A7670E_MODEM_PWR_GPIO, GPIO_MODE_OUTPUT);
        gpio_set_level(A7670E_MODEM_PWR_GPIO, 1);
    }

    if (wifi_on) {
        wifi_manager_init();
        wifi_tcp_client_start();
    }

    /* After OTA: confirm image so bootloader rollback does not revert on next reset. */
    fota_mark_current_app_valid_if_needed();

    ESP_LOGI(TAG, "status_reg=0x%04X  mode=%d  modem=%s  wifi=%s",
             early_sr, (int)conn,
             modem_on ? (conn == NVS_CONN_MODEM_SLAVE_WIFI ? "slave" : "full") : "OFF",
             wifi_on ? (wifi_manager_is_connected() ? "connected" : "starting") : "OFF");

    /* 1) Relay first (shared by modem OPEN, BLE app, and CHECK_USER flow). */
    if (relay_control_init() != ESP_OK || relay_control_start() != ESP_OK) {
        ESP_LOGE(TAG, "Relay control init/start failed");
    }
    /* Restore persistent KEEPOPEN relay state from status_reg (bits 0/1). */
    {
        uint16_t sr = nvs_config_get_status_reg(0x0000);
        relay_command_t rcmd = {0};

        if (sr & STATUS_KEEP_RELAY1) {
            rcmd.relay_number = 1;
            rcmd.duration_ms = 0;     /* permanent until CLOSE[1]/CLOSE[3] */
            rcmd.activate = true;
            snprintf(rcmd.description, sizeof(rcmd.description), "Restore KEEPOPEN[1] from NVS");
            (void)relay_execute_command(&rcmd);
        }

        if (sr & STATUS_KEEP_RELAY2) {
            memset(&rcmd, 0, sizeof(rcmd));
            rcmd.relay_number = 2;
            rcmd.duration_ms = 0;     /* permanent until CLOSE[2]/CLOSE[3] */
            rcmd.activate = true;
            snprintf(rcmd.description, sizeof(rcmd.description), "Restore KEEPOPEN[2] from NVS");
            (void)relay_execute_command(&rcmd);
        }
    }

    /* 2) BLE init (fast); then modem/UART. KA has lowest priority. */
    if (ble_gatt_server_init() != ESP_OK) {
        ESP_LOGE(TAG, "BLE GATT server init failed");
    }

    /* 3) Digital input monitor: GPIO22 pull-up, active-low; sends ALERT01 after 180s active. */
    if (!digital_input_alert_task_start()) {
        ESP_LOGW(TAG, "Digital input alert task not started");
    }

    /* Task watchdog: configurable timeout, panic if not fed. Fed by uart_rx_task (modem) and status_led_task (BLE). */
    esp_task_wdt_config_t wdt_cfg = {
        .timeout_ms = (uint32_t)WATCHDOG_TIMEOUT_SECONDS * 1000,
        .idle_core_mask = 0,
        .trigger_panic = true,
    };
    if (esp_task_wdt_init(&wdt_cfg) != ESP_OK) {
        ESP_LOGW(TAG, "Task WDT init failed (may already be inited)");
    } else {
        ESP_LOGI(TAG, "Task WDT: %d s, panic=true (fed by %s + BLE status_led)",
                 (int)WATCHDOG_TIMEOUT_SECONDS, modem_disabled ? "wifi tasks" : "uart_rx");
    }

    /* Config shell before modem setup so it still starts if modem mutex/queue creation fails below. */
    if (!config_uart_start()) {
        ESP_LOGW(TAG, "Config shell not started (xTaskCreate failed)");
    }

    if (!modem_disabled) {
        /* ── Modem sync objects ─────────────────────────────────────────── */
        at_command_queue   = xQueueCreate(AT_QUEUE_SIZE, sizeof(at_command_t));
        uart_mutex         = xSemaphoreCreateMutex();
        response_ready_sem = xSemaphoreCreateBinary();
        init_control_mutex = xSemaphoreCreateMutex();
        response_queue     = xQueueCreate(RESPONSE_QUEUE_SIZE, sizeof(modem_rsp_line_t));
        s_wait_line_mutex  = xSemaphoreCreateMutex();

        if (at_command_queue == NULL || uart_mutex == NULL || response_ready_sem == NULL ||
            init_control_mutex == NULL || response_queue == NULL || s_wait_line_mutex == NULL) {
            ESP_LOGE(TAG, "Failed to create modem synchronization objects");
            return;
        }

        /* ── Modem UART ─────────────────────────────────────────────────── */
        uart_config_t uart_config = {
            .baud_rate = UART_BAUD_RATE,
            .data_bits = UART_DATA_8_BITS,
            .parity    = UART_PARITY_DISABLE,
            .stop_bits = UART_STOP_BITS_1,
            .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        };
        ESP_ERROR_CHECK(uart_param_config(UART_NUM, &uart_config));
        ESP_ERROR_CHECK(uart_set_pin(UART_NUM, UART_TX_PIN, UART_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
        ESP_ERROR_CHECK(uart_driver_install(UART_NUM, UART_BUF_SIZE, 0, 0, NULL, 0));

        /* ── Modem tasks ────────────────────────────────────────────────── */
        xTaskCreate(uart_rx_task, "uart_rx_task", 6144, NULL, 12, NULL);
        /* Large LINE_BUFFER_SIZE: s_at_dequeue_* statics are big; keep stack generous. */
        xTaskCreate(at_command_task, "at_command_task", 12288, NULL, 10, NULL);
        if (modem_status_led_task_handle == NULL) {
            xTaskCreate(modem_status_led_task, "modem_status_led", 2048, NULL, 4, &modem_status_led_task_handle);
        }
    } else {
        ESP_LOGI(TAG, "WiFi-only: modem queues / UART / tasks not created");
        if (wifi_on && modem_status_led_task_handle == NULL) {
            xTaskCreate(modem_status_led_task, "modem_status_led", 2048, NULL, 4,
                        &modem_status_led_task_handle);
            // ESP_LOGI(TAG, "GPIO%d link LED task (WiFi-only: 500/500 → 150/2000 → ON on server cmd)",
            //          A7670E_MODEM_STATUS_LED_GPIO);
        }
    }

    /* Local button: GPIO15 active-low → relay pulse. Not modem-specific; always active. */
    if (local_button_task_handle == NULL) {
        xTaskCreate(local_button_task, "local_button", 2048, NULL, 5, &local_button_task_handle);
    }
    /* Note: modem_init_task is created on-demand via start_modem_init_task() */

    if (modem_disabled) {
        ESP_LOGI(TAG, "System ready – WiFi-only mode (modem disabled)");
    } else if (conn == NVS_CONN_MODEM_SLAVE_WIFI) {
        ESP_LOGI(TAG, "System ready – modem slave + WiFi server path");
    } else {
        ESP_LOGI(TAG, "System ready – modem full (cellular TCP)");
    }

    if (wifi_on && wifi_manager_wps_boot_pending()) {
        ESP_LOGI(TAG,
                 "WiFi WPS: unit waiting for router — press WPS/PBC on the access point (timeout %d s)",
                 WIFI_WPS_TIMEOUT_S);
    }

    vTaskDelay(pdMS_TO_TICKS(APPMODEM_START_DELAY_MS));

    if (!modem_disabled) {
        start_modem_init();
    }
  //======================================================================= 
    // Uncomment the line below to automatically run the TCP/IP task examples:
    // code in file: tcp_task_usage_example.c
    // start_tcp_examples();
  //=======================================================================
    // Optional: callback_dive_in_test.h / force_all_callbacks_now() for Modem_Config_Handling callback testing
}
