idf_component_register(SRCS "buttons_hal.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_event driver freertos sys_manager esp_timer)