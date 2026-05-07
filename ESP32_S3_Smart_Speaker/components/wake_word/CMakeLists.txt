idf_component_register(SRCS "wake_word.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp-sr esp_event freertos esp_ringbuf sys_manager audio_hal)