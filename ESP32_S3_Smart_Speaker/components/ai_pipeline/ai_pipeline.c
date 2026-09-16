#include "ai_pipeline.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "cJSON.h"
#include <string.h>

#include "sys_manager.h"
#include "wake_word.h"
#include "audio_hal.h"

static const char *TAG = "AI_PIPELINE";

// API Keys
#define OPENAI_API_KEY "YOUR_OPENAI_API_KEY_HERE"

//Put here your keys
//Endpoints 
#define WHISPER_API_URL ""
#define GEMINI_API_URL ""
#define TTS_API_URL     ""

// Task Notification Handle
static TaskHandle_t ai_task_handle = NULL;

//
// Helper: WAV Header Generator
//
static void generate_wav_header(uint8_t *header, uint32_t sample_rate, uint16_t bits_per_sample, uint16_t channels, uint32_t data_size) {
    uint32_t byte_rate = sample_rate * channels * (bits_per_sample / 8);
    uint32_t file_size = 36 + data_size;

    // RIFF chunk
    memcpy(header, "RIFF", 4);
    memcpy(header + 4, &file_size, 4);
    memcpy(header + 8, "WAVE", 4);
    // fmt subchunk
    memcpy(header + 12, "fmt ", 4);
    uint32_t fmt_chunk_size = 16;
    memcpy(header + 16, &fmt_chunk_size, 4);
    uint16_t audio_format = 1; // PCM
    memcpy(header + 20, &audio_format, 2);
    memcpy(header + 22, &channels, 2);
    memcpy(header + 24, &sample_rate, 4);
    memcpy(header + 28, &byte_rate, 4);
    uint16_t block_align = channels * (bits_per_sample / 8);
    memcpy(header + 32, &block_align, 2);
    memcpy(header + 34, &bits_per_sample, 2);
    // data subchunk
    memcpy(header + 36, "data", 4);
    memcpy(header + 40, &data_size, 4);
}

//
// STT (Speech-to-Text): OpenAI Whisper
//
static char* perform_stt() {
    ESP_LOGI(TAG, "Starting STT (OpenAI Whisper)...");
    
    RingbufHandle_t record_buf = wake_word_get_record_ringbuf();
    if (!record_buf) return NULL;
    //size of buffer
    size_t max_audio_size = 1024 * 1024;
    uint8_t *audio_data = malloc(max_audio_size + 44);
    if (!audio_data) {
        ESP_LOGE(TAG, "Failed to allocate memory for audio data");
        return NULL;
    }

    size_t total_pcm_size = 0;
    size_t item_size;
    void *item;

    while ((item = xRingbufferReceive(record_buf, &item_size, 0)) != NULL) {
        if (total_pcm_size + item_size <= max_audio_size) {
            memcpy(audio_data + 44 + total_pcm_size, item, item_size);
            total_pcm_size += item_size;
        }
        vRingbufferReturnItem(record_buf, item);
    }

    if (total_pcm_size == 0) {
        free(audio_data);
        return NULL;
    }

    generate_wav_header(audio_data, 16000, 16, 1, total_pcm_size);
    size_t total_payload_size = 44 + total_pcm_size;

    // 
    const char *boundary = "----Esp32Boundary";
    char header_prefix[512];
    int header_len = snprintf(header_prefix, sizeof(header_prefix), 
             "--%s\r\n"
             "Content-Disposition: form-data; name=\"model\"\r\n\r\nwhisper-1\r\n"
             "--%s\r\n"
             "Content-Disposition: form-data; name=\"file\"; filename=\"audio.wav\"\r\n"
             "Content-Type: audio/wav\r\n\r\n", boundary, boundary);
             
    char footer[128];
    // Calculate footer length for closing boundary
    int footer_len = snprintf(footer, sizeof(footer), "\r\n--%s--\r\n", boundary);
    
    esp_http_client_config_t config = {
        .url = WHISPER_API_URL,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .method = HTTP_METHOD_POST,
        .buffer_size = 4096,
        .buffer_size_tx = 4096,
        .timeout_ms = 15000 // Даем время на загрузку аудио
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    
    char content_type[128];
    snprintf(content_type, sizeof(content_type), "multipart/form-data; boundary=%s", boundary);
    esp_http_client_set_header(client, "Content-Type", content_type);
    
    char auth_header[256];
    snprintf(auth_header, sizeof(auth_header), "Bearer %s", OPENAI_API_KEY);
    esp_http_client_set_header(client, "Authorization", auth_header);

    //
    esp_err_t err = esp_http_client_open(client, header_len + total_payload_size + footer_len);
    if (err == ESP_OK) {
        esp_http_client_write(client, header_prefix, header_len);
        esp_http_client_write(client, (const char *)audio_data, total_payload_size);
        esp_http_client_write(client, footer, footer_len);
        
        esp_http_client_fetch_headers(client);
        int status_code = esp_http_client_get_status_code(client);
        ESP_LOGI(TAG, "OpenAI STT HTTP Status Code: %d", status_code);

        char *response_buffer = calloc(1, 2048);
        int read_len = esp_http_client_read(client, response_buffer, 2047);
        
        if (read_len > 0) {
            ESP_LOGI(TAG, "OpenAI Raw Response: %s", response_buffer);
            
            // Парсинг JSON
            char *recognized_text = NULL;
            cJSON *json = cJSON_Parse(response_buffer);
            if (json) {
                cJSON *text_item = cJSON_GetObjectItem(json, "text");
                if (cJSON_IsString(text_item) && (text_item->valuestring != NULL)) {
                    recognized_text = strdup(text_item->valuestring);
                }
                cJSON_Delete(json);
            }
            free(response_buffer);
            free(audio_data);
            esp_http_client_cleanup(client);
            
            ESP_LOGI(TAG, "STT Result: %s", recognized_text ? recognized_text : "NULL");
            return recognized_text;
        }
        free(response_buffer);
    } else {
        ESP_LOGE(TAG, "Failed to open HTTP connection: %s", esp_err_to_name(err));
    }

    free(audio_data);
    esp_http_client_cleanup(client);
    return NULL;
}

//
// Gemini API: Process text and extract intent / commands
// 
static gemini_response_t* perform_gemini(const char *user_text) {
    ESP_LOGI(TAG, "Sending to Gemini API...");

    // create JSON
    cJSON *root = cJSON_CreateObject();
    cJSON *contents = cJSON_AddArrayToObject(root, "contents");
    cJSON *content_obj = cJSON_CreateObject();
    cJSON *parts = cJSON_AddArrayToObject(content_obj, "parts");
    cJSON *part_obj = cJSON_CreateObject();
    
    // only json promt and names
    char combined_prompt[1024];
    snprintf(combined_prompt, sizeof(combined_prompt), 
             "You are a smart speaker, NAME JARVIS. Respond ONLY with JSON: {\"speech\": \"text\", \"command\": \"none\"}. User: %s", 
             user_text);
             
    cJSON_AddStringToObject(part_obj, "text", combined_prompt);
    cJSON_AddItemToArray(parts, part_obj);
    cJSON_AddItemToArray(contents, content_obj);
    
    char *post_data = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    esp_http_client_config_t config = {
        .url = GEMINI_API_URL,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .method = HTTP_METHOD_POST,
        .buffer_size = 8192,
        .buffer_size_tx = 4096,
        .timeout_ms = 60000,
        .is_async = false,
        .skip_cert_common_name_check = true // 
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_http_client_set_header(client, "Content-Type", "application/json");

    gemini_response_t *gemini_res = NULL;

    esp_err_t err = esp_http_client_open(client, strlen(post_data));
    if (err == ESP_OK) {
        esp_http_client_write(client, post_data, strlen(post_data));
        int content_length = esp_http_client_fetch_headers(client);
        int status_code = esp_http_client_get_status_code(client);
        
        ESP_LOGI(TAG, "Gemini HTTP Status Code: %d", status_code);

        if (status_code == 200) {
            char *response_buffer = calloc(1, 4096);
            int read_len = esp_http_client_read(client, response_buffer, 4095);
            if (read_len > 0) {
                ESP_LOGI(TAG, "Gemini Raw Response: %s", response_buffer);
                
                //parsing JSON and extracting "speech" and "command"
                cJSON *res_json = cJSON_Parse(response_buffer);
                if (res_json) {
                    cJSON *candidates = cJSON_GetObjectItem(res_json, "candidates");
                    if (cJSON_IsArray(candidates)) {
                        cJSON *candidate = cJSON_GetArrayItem(candidates, 0);
                        cJSON *content = cJSON_GetObjectItem(candidate, "content");
                        cJSON *res_parts = cJSON_GetObjectItem(content, "parts");
                        cJSON *res_part = cJSON_GetArrayItem(res_parts, 0);
                        cJSON *text_item = cJSON_GetObjectItem(res_part, "text");

                        if (cJSON_IsString(text_item)) {
                            char *json_start = strchr(text_item->valuestring, '{');
                            char *json_end = strrchr(text_item->valuestring, '}');
                            if (json_start && json_end && (json_end > json_start)) {
                                *(json_end + 1) = '\0'; 
                                cJSON *action_json = cJSON_Parse(json_start);
                                if (action_json) {
                                    gemini_res = malloc(sizeof(gemini_response_t));
                                    cJSON *speech = cJSON_GetObjectItem(action_json, "speech");
                                    cJSON *command = cJSON_GetObjectItem(action_json, "command");
                                    gemini_res->response_text = strdup(speech ? speech->valuestring : "Ошибка формата");
                                    gemini_res->tech_command = strdup(command ? command->valuestring : "none");
                                    cJSON_Delete(action_json);
                                }
                            }
                        }
                    }
                    cJSON_Delete(res_json);
                }
            }
            free(response_buffer);
        } else {
            // if status is not 200, try to read error message from body
            char *error_buffer = calloc(1, 1024);
            esp_http_client_read(client, error_buffer, 1023);
            ESP_LOGE(TAG, "Gemini Error Body: %s", error_buffer);
            free(error_buffer);
        }
    } else {
        ESP_LOGE(TAG, "Failed to open connection to Gemini: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
    free(post_data);
    return gemini_res;
}

static void perform_tts(const char *text) {
    ESP_LOGI(TAG, "Starting TTS Streaming...");
    esp_event_post(SYS_EVENT_BASE, SYS_EVENT_TTS_START, NULL, 0, portMAX_DELAY);

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "model", "tts-1");
    cJSON_AddStringToObject(root, "input", text);
    cJSON_AddStringToObject(root, "voice", "alloy");
    cJSON_AddStringToObject(root, "response_format", "pcm"); 
    
    char *post_data = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    esp_http_client_config_t config = {
        .url = TTS_API_URL,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .method = HTTP_METHOD_POST,
        .buffer_size = 4096,
        .timeout_ms = 15000
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    
    char auth_header[256];
    snprintf(auth_header, sizeof(auth_header), "Bearer %s", OPENAI_API_KEY);
    esp_http_client_set_header(client, "Authorization", auth_header);
    
    esp_err_t err = esp_http_client_open(client, strlen(post_data));
    if (err == ESP_OK) {
        esp_http_client_write(client, post_data, strlen(post_data));
        esp_http_client_fetch_headers(client);
        
        int status_code = esp_http_client_get_status_code(client);
        if (status_code == 200) {
            RingbufHandle_t tx_buf = audio_hal_get_tx_ringbuf();
            uint8_t *chunk_buffer = malloc(2048);
            int read_len;
            
            uint8_t leftover_byte = 0;
            bool has_leftover = false;
            bool first_chunk = true;

            while ((read_len = esp_http_client_read(client, (char*)chunk_buffer, 2048)) > 0) {
                // debug logs for audio
                if (first_chunk && read_len >= 6) {
                    ESP_LOGI("DEBUG_RAW", "First 6 bytes from OpenAI: %02x %02x %02x %02x %02x %02x", 
                             chunk_buffer[0], chunk_buffer[1], chunk_buffer[2], 
                             chunk_buffer[3], chunk_buffer[4], chunk_buffer[5]);
                    first_chunk = false;
                }

                uint8_t *data_to_send = chunk_buffer;
                int actual_send_len = read_len;
                uint8_t *aligned_buf = NULL;

                // fix bit from OpenAI if chunk is odd - we need to align to 16-bit samples for I2S
                if (has_leftover) {
                    aligned_buf = malloc(read_len + 1);
                    aligned_buf[0] = leftover_byte;
                    memcpy(aligned_buf + 1, chunk_buffer, read_len);
                    data_to_send = aligned_buf;
                    actual_send_len = read_len + 1;
                    has_leftover = false;
                }

                // if we have odd number of bytes, save the last byte for the next chunk
                if (actual_send_len % 2 != 0) {
                    leftover_byte = data_to_send[actual_send_len - 1];
                    has_leftover = true;
                    actual_send_len--; 
                }

                if (actual_send_len > 0) {
                    xRingbufferSend(tx_buf, data_to_send, actual_send_len, pdMS_TO_TICKS(500));
                }

                if (aligned_buf) free(aligned_buf);
            }
            free(chunk_buffer);
        } else {
            ESP_LOGE(TAG, "Server returned status %d", status_code);
        }
    }
    free(post_data);
    esp_http_client_cleanup(client);
    vTaskDelay(pdMS_TO_TICKS(300)); // time to finish audio playback
    esp_event_post(SYS_EVENT_BASE, SYS_EVENT_TTS_END, NULL, 0, portMAX_DELAY);
}
//
// Task & Event Handlers
//

static void ai_pipeline_task(void *pvParameters) {
    ESP_LOGI(TAG, "AI Pipeline Task running on Core %d", xPortGetCoreID());

    while (1) {
        // Wait for SYS_EVENT_SPEECH_END_DETECTED notification
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        // 1. STT
        char *recognized_text = perform_stt();
        if (!recognized_text) {
            ESP_LOGE(TAG, "STT Failed");
            // Inform system to go back to IDLE
            esp_event_post(SYS_EVENT_BASE, SYS_EVENT_TTS_END, NULL, 0, portMAX_DELAY);
            continue;
        }
        
        // Notify System for LED UI
        esp_event_post(SYS_EVENT_BASE, SYS_EVENT_STT_DONE, recognized_text, strlen(recognized_text) + 1, portMAX_DELAY);

        // 2. Gemini
        gemini_response_t *gemini_res = perform_gemini(recognized_text);
        free(recognized_text);

        if (!gemini_res) {
            ESP_LOGE(TAG, "Gemini Request Failed");
            esp_event_post(SYS_EVENT_BASE, SYS_EVENT_TTS_END, NULL, 0, portMAX_DELAY);
            continue;
        }

        // Notify System to execute commands 
        esp_event_post(SYS_EVENT_BASE, SYS_EVENT_GEMINI_DONE, gemini_res, sizeof(gemini_response_t), portMAX_DELAY);

        // 3. TTS
        if (gemini_res->response_text) {
            perform_tts(gemini_res->response_text);
        }

        // Cleanup
        free(gemini_res->response_text);
        free(gemini_res->tech_command);
        free(gemini_res);
    }
}

// Event loop callback
static void sys_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (event_base == SYS_EVENT_BASE && event_id == SYS_EVENT_SPEECH_END_DETECTED) {
        // Wake up the AI Pipeline Task
        if (ai_task_handle != NULL) {
            xTaskNotifyGive(ai_task_handle);
        }
    }
}

//
// Public API
// 

void ai_pipeline_init(void) {
    ESP_LOGI(TAG, "Initializing AI Pipeline Module...");

    // Register event handler
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        SYS_EVENT_BASE, 
        SYS_EVENT_SPEECH_END_DETECTED, 
        &sys_event_handler, 
        NULL, 
        NULL
    ));

    // Create Network Processing Task pinned to Core 0
    xTaskCreatePinnedToCore(
        ai_pipeline_task, 
        "ai_pipeline_task", 
        8192, 
        NULL, 
        5, // priority
        &ai_task_handle, 
        0 // Core 0
    );
}