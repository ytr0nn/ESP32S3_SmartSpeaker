idf_component_register(SRCS "main.c"
                       INCLUDE_DIRS ""
                       REQUIRES nvs_flash 
                                esp_event 
                                sys_manager 
                                audio_hal 
                                wake_word 
                                ai_pipeline 
                                display_ui 
                                led_ctrl 
                                buttons_hal)