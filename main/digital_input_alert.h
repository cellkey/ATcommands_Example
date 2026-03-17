/**
 * @file digital_input_alert.h
 * @brief Monitor a digital input and send ALERT to server after sustained active.
 */

#ifndef DIGITAL_INPUT_ALERT_H
#define DIGITAL_INPUT_ALERT_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** GPIO used for digital input (active-low with pull-up). */
#define DIGITAL_INPUT_GPIO 22

/** Active time required before sending alert (seconds). */
#define DIGITAL_INPUT_ALERT_DELAY_SEC 10 //180

/**
 * @brief Start digital input monitoring task (idempotent).
 * @return true if started (or already running), false on failure.
 */
bool digital_input_alert_task_start(void);

/**
 * @brief Stop digital input monitoring task.
 */
void digital_input_alert_task_stop(void);

/**
 * @brief Get current input state (true when active/LOW).
 */
bool digital_input_is_active(void);

#ifdef __cplusplus
}
#endif

#endif /* DIGITAL_INPUT_ALERT_H */

