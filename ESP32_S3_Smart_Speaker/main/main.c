#include "main.h"
#include "esp_log.h"
#include "audio_i2s.h"
#include "ui_hardware.h"
#include "network_api.h"

static const char *TAG = "APP_MAIN";

void app_main(void) {
    ESP_LOGI(TAG, "Starting Smart Speaker initialization...");

    // 1. Memory and network init
    network_system_init();

    // 2. UI init
    ui_hardware_init();

    // 3. audio init 
    audio_i2s_init();

    ESP_LOGI(TAG, "Hardware initialization complete!");

    // Later i will create tasks for wake word detection, UI handling, and audio processing
    // xTaskCreatePinnedToCore(wake_word_task, ... , 0); // 0 core
    // xTaskCreatePinnedToCore(ui_task, ... , 1);        // 1 core
}