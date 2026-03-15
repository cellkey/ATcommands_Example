/**
 * @file at_command_examples.h
 * @brief Example usage patterns for the AT command system
 * 
 * This file demonstrates how to use the AT command system in your application.
 * Include this header after the main AT command system is initialized.
 */

#ifndef AT_COMMAND_EXAMPLES_H
#define AT_COMMAND_EXAMPLES_H

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "esp_log.h"
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "at_command_api.h"

#ifdef __cplusplus
extern "C" {
#endif

// Tag for examples logging
#define AT_EXAMPLES_TAG "AT_EXAMPLES"

// Function declarations only - implementations moved to separate file
void example_enhanced_at_commands(void);
void example_simple_at_command(void);
void example_check_network_registration(void);
void example_check_signal_quality(void);
void example_send_sms(const char *phone_number, const char *message);
void example_setup_gsm_network(const char *apn, const char *username, const char *password);
void example_http_get(const char *url);
void example_complete_modem_test(void);

#ifdef __cplusplus
}
#endif

#endif // AT_COMMAND_EXAMPLES_H