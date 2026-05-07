idf_component_register(SRCS "sys_manager.c" "wifi_web_setup.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_event esp_wifi nvs_flash esp_netif audio_hal esp_http_server display_ui
                                led_ctrl
                                buttons_hal
                                wake_word
                                ai_pipeline)