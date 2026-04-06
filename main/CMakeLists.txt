# CMakeLists.txt for Enhanced UART AT Command Example

idf_component_register(SRCS  "Encryption.c"
                             "unit_ready.c"
                             "fota_modem.c"
                             "ble_gatt_server.c"
                             "relay_control.c"
                             "server_keepalive.c"
                             "digital_input_alert.c"
                             "a7670e_sequences.c"
                             "nvs_config.c"
                             "config_uart.c"
                             "wifi_manager.c"
                             "wifi_tcp_client.c"
                             "enhanced_freertos_uart_at_commands.c" 
                            "force_callback_triggers.c" 
                            "Modem_Config_Handling.c" 
                            "modem_definitions.c" 
                            "tcp_task_usage_example.c" 
                            "tcp_task_management.c" 
                            "integration_example.c" 
                            "modem_init_usage_example.c" 
                            "at_command_examples.c"
                            INCLUDE_DIRS "." ".."
                            REQUIRES driver freertos esp_timer nvs_flash json bt app_update
                                     esp_wifi wpa_supplicant esp_netif esp_event)
                             