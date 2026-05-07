idf_component_register(SRCS "display_ui.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_event freertos driver esp_timer lvgl sys_manager)