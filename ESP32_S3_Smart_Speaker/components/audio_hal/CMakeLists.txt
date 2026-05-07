idf_component_register(SRCS "audio_hal.c"
                       INCLUDE_DIRS "include"
                       REQUIRES driver freertos esp_ringbuf esp_timer)