#include "wake_word.h"
#include "esp_log.h"
#include "esp_err.h"
#include "freertos/task.h"
#include <string.h>

#include "esp_wn_iface.h"
#include "esp_wn_models.h"
#include "esp_afe_sr_models.h"
#include "esp_afe_sr_iface.h"
#include "esp_afe_config.h"

#include "sys_manager.h"
#include "audio_hal.h"

static const char *TAG = "WAKE_WORD";

#define RECORD_RINGBUF_SIZE         (512 * 1024)
#define VAD_SILENCE_TIMEOUT_FRAMES  60 // silence timeout frames

//audio gain multiplier for better wake word detection, can be adjusted based on testing
#define AUDIO_GAIN_MULTIPLIER       1.5f 

static RingbufHandle_t record_ringbuf = NULL;
static bool is_recording = false;
static int silence_frame_counter = 0;

static esp_afe_sr_data_t *afe_data = NULL;
static const esp_afe_sr_iface_t *afe_handle = NULL;
static srmodel_list_t *models = NULL;

static void audio_feed_task(void *arg) {
    ESP_LOGI(TAG, "Feed Task: Priority 5 Core 1");
    RingbufHandle_t rx_buf = NULL;
    
    // wait for i2s buffer
    while ((rx_buf = audio_hal_get_rx_ringbuf()) == NULL) {
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    int chunk_size = afe_handle->get_feed_chunksize(afe_data);
    int feed_channels = afe_handle->get_feed_channel_num(afe_data);
    int feed_bytes = chunk_size * feed_channels * sizeof(int16_t);
    
    int16_t *feed_buffer = heap_caps_malloc(feed_bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    int filled_bytes = 0;

    while (1) {
        size_t bytes_received = 0;
        uint8_t *rx_data = xRingbufferReceive(rx_buf, &bytes_received, portMAX_DELAY);
        
        if (rx_data != NULL) {
            int offset = 0;
            while (offset < bytes_received) {
                int space_left = feed_bytes - filled_bytes;
                int to_copy = (bytes_received - offset > space_left) ? space_left : (bytes_received - offset);
                
                memcpy((uint8_t *)feed_buffer + filled_bytes, rx_data + offset, to_copy);
                filled_bytes += to_copy;
                offset += to_copy;

                // when 1 frame is filled, feed to AFE
                if (filled_bytes == feed_bytes) {
                    
                    // linear amplification 
                    if (AUDIO_GAIN_MULTIPLIER > 1.0f) {
                        int16_t *samples = (int16_t *)feed_buffer;
                        for (int i = 0; i < chunk_size * feed_channels; i++) {
                            int32_t s = (int32_t)(samples[i] * AUDIO_GAIN_MULTIPLIER);
                            if (s > 32767) s = 32767;
                            else if (s < -32768) s = -32768;
                            samples[i] = (int16_t)s;
                        }
                    }

                    afe_handle->feed(afe_data, feed_buffer);
                    filled_bytes = 0; 
                }
            }
            vRingbufferReturnItem(rx_buf, rx_data);
        }
    }
}


static void audio_fetch_task(void *arg) {
    ESP_LOGI(TAG, "Fetch Task: Priority 4 Core 1");

    while (1) {
        // blocking function, returns as soon as a new result is available
        afe_fetch_result_t* res = afe_handle->fetch(afe_data);
        
        if (res && res->ret_value != ESP_FAIL) {
            
            // wake word check
            if (res->wakeup_state > 0) {
                ESP_LOGI(TAG, ">>> WAKE_WORD_TRIGGER (Word ID: %d) <<<", res->wakeup_state);
                is_recording = true;
                silence_frame_counter = 0;
                esp_event_post(SYS_EVENT_BASE, SYS_EVENT_WAKE_WORD_DETECTED, NULL, 0, portMAX_DELAY);
            }

            // record audio if wake word was triggered
            if (is_recording) {
                // 
                if (res->vad_cache_size > 0) {
                    xRingbufferSend(record_ringbuf, res->vad_cache, res->vad_cache_size * sizeof(int16_t), 0);
                }

                // clear audio frame to system
                size_t fetch_bytes = afe_handle->get_fetch_chunksize(afe_data) * sizeof(int16_t);
                xRingbufferSend(record_ringbuf, res->data, fetch_bytes, pdMS_TO_TICKS(5));

                // VAD stop detection
                if ((int)res->vad_state == AFE_VAD_SILENCE) {
                    if (++silence_frame_counter >= VAD_SILENCE_TIMEOUT_FRAMES) {
                        ESP_LOGI(TAG, ">>> VAD_END_DETECTION (Silence) <<<");
                        is_recording = false;
                        silence_frame_counter = 0;
                        esp_event_post(SYS_EVENT_BASE, SYS_EVENT_SPEECH_END_DETECTED, NULL, 0, portMAX_DELAY);
                    }
                } else {
                    silence_frame_counter = 0; 
                }
            }
        }
    }
}

void wake_word_init(void) {
    ESP_LOGI(TAG, "Initializing SR Engine...");

    record_ringbuf = xRingbufferCreateWithCaps(RECORD_RINGBUF_SIZE, RINGBUF_TYPE_BYTEBUF, MALLOC_CAP_SPIRAM);
    
    models = esp_srmodel_init("model"); 
    if (models == NULL || models->num == 0) {
        ESP_LOGE(TAG, "Failed to load models!");
        return;
    }

    ESP_LOGI(TAG, "Available WakeNet models:");
    for (int i = 0; i < models->num; i++) {
        ESP_LOGI(TAG, "- [%d] %s", i, models->model_name[i]);
    }

    afe_config_t *afe_config = afe_config_init("M", models, AFE_TYPE_SR, AFE_MODE_HIGH_PERF);
    
    afe_config->aec_init = false;
    afe_config->se_init = false; 
    afe_config->vad_init = true;
    afe_config->wakenet_init = true;
    
    // VAD modes of noise suppression: VAD_MODE_0 (Normal), VAD_MODE_1 (Aggressive), VAD_MODE_2 (Very Aggressive), VAD_MODE_3 (Very Very Aggressive), VAD_MODE_4 (Very Very Very Aggressive)
    afe_config->vad_mode = VAD_MODE_1; 
    
    afe_config->wakenet_model_name = models->model_name[0];
    ESP_LOGI(TAG, "Selected Model: %s", afe_config->wakenet_model_name);
    
    afe_handle = esp_afe_handle_from_config(afe_config);
    afe_data = afe_handle->create_from_config(afe_config);
    afe_config_free(afe_config);

    xTaskCreatePinnedToCore(audio_feed_task, "feed_task", 8192, NULL, 21, NULL, 1);   
    xTaskCreatePinnedToCore(audio_fetch_task, "fetch_task", 8192, NULL, 20, NULL, 1);

    ESP_LOGI(TAG, "SR Engine Ready!");
}

RingbufHandle_t wake_word_get_record_ringbuf(void) {
    return record_ringbuf;
}