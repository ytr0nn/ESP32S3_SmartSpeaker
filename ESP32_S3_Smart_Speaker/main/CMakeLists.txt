idf_component_register(SRCS "main.c" 
                            "audio_i2s.c"
                            "ui_hardware.c"
                            "network_api.c"
                            "wake_word.c"
                       INCLUDE_DIRS "."
                       REQUIRES "nvs_flash" "esp_wifi" "esp_netif" "esp_event" "freertos" "esp_ringbuf" "driver" "esp-sr")