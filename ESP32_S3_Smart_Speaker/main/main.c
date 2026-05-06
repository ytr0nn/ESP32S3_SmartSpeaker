#include "main.h"
#include "esp_log.h"
#include "audio_i2s.h"
#include "ui_hardware.h"
#include "network_api.h"
#include "wake_word.h" 

static const char *TAG = "APP_MAIN";

// global ring buffer handle for audio data
RingbufHandle_t audio_rx_ringbuf = NULL;

void app_main(void) {
    ESP_LOGI(TAG, "Starting Smart Speaker initialization...");

    // 1. memory and network init
    network_system_init();

    // 2. UI init
    ui_hardware_init();

    // 3. create ring buffer for audio data
    audio_rx_ringbuf = xRingbufferCreate(RINGBUF_SIZE, RINGBUF_TYPE_BYTEBUF);
    if (audio_rx_ringbuf == NULL) {
        ESP_LOGE(TAG, "Failed to create audio ring buffer!");
        return;
    }
    ESP_LOGI(TAG, "Audio RingBuffer created successfully.");

    // 4. audio init
    audio_i2s_init();

    // 5. Esp-sr init
    wake_word_init();

    ESP_LOGI(TAG, "Hardware initialization complete!");

    // 6. mic read task
    // feed the buffer with sound
    xTaskCreatePinnedToCore(audio_rx_task, "audio_rx_task", 4096, NULL, 5, NULL, 0);

    // 
    // space for ui
}