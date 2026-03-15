/**
 * @file modem_task_control.h
 * @brief Public API for controlling the modem initialization task
 * 
 * This header provides the public interface for starting, stopping, and
 * monitoring the modem initialization task. The task can be activated
 * when needed and will automatically deactivate when initialization is complete.
 */

#ifndef MODEM_TASK_CONTROL_H
#define MODEM_TASK_CONTROL_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Start the modem initialization task
 * 
 * Creates and starts a new task that will perform the complete modem
 * initialization sequence. The task will automatically terminate when
 * the initialization is complete.
 * 
 * @return true if task started successfully, false if already running or failed to start
 */
bool start_modem_init_task(void);

/**
 * @brief Stop the modem initialization task
 * 
 * Forcefully stops the modem initialization task if it's currently running.
 * This should only be used if you need to abort the initialization process.
 * 
 * @return true if task stopped successfully, false if not running or failed to stop
 */
bool stop_modem_init_task(void);

/**
 * @brief Check if modem initialization task is currently active
 * 
 * @return true if the initialization task is currently running, false otherwise
 */
bool is_modem_init_active(void);

/**
 * @brief Usage Examples:
 * 
 * // Start modem initialization when needed
 * if (start_modem_init_task()) {
 *     ESP_LOGI(TAG, "Modem initialization started");
 *     
 *     // Wait for completion (optional)
 *     while (is_modem_init_active()) {
 *         vTaskDelay(pdMS_TO_TICKS(500));
 *     }
 *     ESP_LOGI(TAG, "Modem initialization completed");
 * }
 * 
 * // Check status
 * if (is_modem_init_active()) {
 *     ESP_LOGI(TAG, "Modem initialization in progress...");
 * } else {
 *     ESP_LOGI(TAG, "Modem initialization not running");
 * }
 * 
 * // Emergency stop (if needed)
 * if (stop_modem_init_task()) {
 *     ESP_LOGI(TAG, "Modem initialization stopped");
 * }
 */

 /**
 * @brief Initialize and start the modem control demo
 * 
 * Call this function from your main application to see the examples in action.
 * Note: Make sure the main UART AT command system is already initialized.
 */
void start_modem_init(void);
void start_modem_control_examples(void);  /* Legacy: calls start_modem_init() */

#ifdef __cplusplus
}
#endif

#endif // MODEM_TASK_CONTROL_H