#include "wake_word.h"
#include "main.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/ringbuf.h"
#include "esp_heap_caps.h"
#include <string.h>

#include "esp_afe_sr_models.h"
#include "esp_afe_sr_iface.h"
#include "model_path.h"

static const char *TAG = "WAKE_WORD";

extern RingbufHandle_t audio_rx_ringbuf;

static const esp_afe_sr_iface_t *afe_handle = NULL;
static esp_afe_sr_data_t *afe_data = NULL;

#define MAX_RECORDING_SECONDS 4
#define COMMAND_BUFFER_SIZE (16000 * 1 * 2 * MAX_RECORDING_SECONDS) 

static int16_t *command_buffer = NULL;
static uint32_t buffer_index = 0;
static bool is_recording = false;

static void audio_feed_task(void *arg)
{
    ESP_LOGI(TAG, "Audio Feed Task started.");
    
    // Use get_feed_chunksize, which is common across versions
    int audio_chunksize = afe_handle->get_feed_chunksize(afe_data);
    int channel_num = afe_handle->get_fetch_channel_num(afe_data);
    size_t chunk_bytes = audio_chunksize * channel_num * sizeof(int16_t);
    
    int16_t *feed_buffer = malloc(chunk_bytes);
    if (!feed_buffer) {
        ESP_LOGE(TAG, "Failed to allocate memory for AFE feed_buffer");
        vTaskDelete(NULL);
    }

    while (1) {
        if (audio_rx_ringbuf != NULL) {
            size_t bytes_received = 0;
            
            while (bytes_received < chunk_bytes) {
                size_t item_size = 0;
                void *item = xRingbufferReceiveUpTo(audio_rx_ringbuf, &item_size, portMAX_DELAY, chunk_bytes - bytes_received);
                
                if (item != NULL) {
                    memcpy((uint8_t*)feed_buffer + bytes_received, item, item_size);
                    vRingbufferReturnItem(audio_rx_ringbuf, item);
                    bytes_received += item_size;
                }
            }
            
            afe_handle->feed(afe_data, feed_buffer);
        } else {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
}

static void audio_fetch_task(void *arg)
{
    ESP_LOGI(TAG, "Audio Fetch Task started. Listening for Wake Word...");

    while (1) {
        afe_fetch_result_t *res = afe_handle->fetch(afe_data);
        
        if (!res || res->ret_value == ESP_FAIL) {
            continue;
        }

        // 1. WAKE WORD DETECTION
        if (res->wakeup_state == WAKENET_DETECTED) {
            ESP_LOGW(TAG, ">>> WAKE WORD DETECTED! <<<");
            
            is_recording = true;
            buffer_index = 0;
            ESP_LOGI(TAG, "Started recording voice command to PSRAM...");
        }

        // 2. BUFFERING THE COMMAND
        if (is_recording) {
            int bytes_to_copy = res->data_size;
            
            if (buffer_index + (bytes_to_copy / sizeof(int16_t)) < (COMMAND_BUFFER_SIZE / sizeof(int16_t))) {
                memcpy(&command_buffer[buffer_index], res->data, bytes_to_copy);
                buffer_index += (bytes_to_copy / sizeof(int16_t));
                
                static int log_counter = 0;
                if (++log_counter >= 10) {
                    float fill_percent = ((float)(buffer_index * sizeof(int16_t)) / COMMAND_BUFFER_SIZE) * 100.0f;
                    ESP_LOGI(TAG, "Recording... Buffer: %lu / %d bytes (%.1f%%)", 
                             (unsigned long)(buffer_index * sizeof(int16_t)), 
                             COMMAND_BUFFER_SIZE, 
                             fill_percent);
                    log_counter = 0;
                }
            } else {
                is_recording = false;
                ESP_LOGI(TAG, ">>> RECORDING COMPLETE. Buffer is full (100%%). <<<");
                buffer_index = 0;
            }
        }
    }
}

void wake_word_init(void)
{
    ESP_LOGI(TAG, "Initializing Wake Word Engine...");

    // 1. Initialize srmodel from partition
    srmodel_list_t *models = esp_srmodel_init("model");
    if (!models) {
        ESP_LOGE(TAG, "Failed to initialize srmodel. Check your partition table and flash size!");
        return;
    }

    // 2. PSRAM allocation for command buffer
    command_buffer = (int16_t *)heap_caps_malloc(COMMAND_BUFFER_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!command_buffer) {
        ESP_LOGE(TAG, "Failed to allocate PSRAM!");
        return;
    }

    // 
    //
    afe_config_t *afe_config = afe_config_init("M", models, AFE_TYPE_SR, AFE_MODE_HIGH_PERF);
    
    afe_config->wakenet_init = true;
    afe_config->aec_init = false; // 

    // 4. Create AFE handle and data instance
    afe_handle = esp_afe_handle_from_config(afe_config);
    
    afe_data = afe_handle->create_from_config(afe_config);
    if (!afe_data) {
        ESP_LOGE(TAG, "Failed to create AFE data instance!");
        return;
    }

    // 5. tasks
    xTaskCreatePinnedToCore(audio_feed_task, "afe_feed", 4096, NULL, 5, NULL, 1);
    xTaskCreatePinnedToCore(audio_fetch_task, "afe_fetch", 8192, NULL, 5, NULL, 1);
    
    ESP_LOGI(TAG, "Wake Word Engine initialized successfully.");
}