idf_component_register(SRCS "ai_pipeline.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_http_client esp-tls esp_event freertos cjson sys_manager wake_word audio_hal)