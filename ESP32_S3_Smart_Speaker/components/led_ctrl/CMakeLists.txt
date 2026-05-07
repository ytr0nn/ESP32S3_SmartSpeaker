idf_component_register(SRCS "led_ctrl.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_event freertos led_strip sys_manager)