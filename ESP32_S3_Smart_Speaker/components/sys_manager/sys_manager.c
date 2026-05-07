#include "wifi_web_setup.h"
#include "nvs.h"
#include "sys_manager.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "SYS_MANAGER";

// Event base definition
ESP_EVENT_DEFINE_BASE(SYS_EVENT_BASE);

static int wifi_retry_count = 0;
#define MAX_WIFI_RETRIES 5
// Current FSM state
static sys_state_t current_state = STATE_IDLE;

// --- Extern stubs for other module initializations ---
// In a real project, these are linked via their respective header files
extern void audio_hal_init(void);
extern void wake_word_init(void);
extern void ai_pipeline_init(void);
extern void display_ui_init(void);
extern void led_ctrl_init(void);
extern void buttons_hal_init(void);
extern void audio_hal_set_volume(int volume); // Function from audio_hal

// Helper function for state transitions
static void change_state(sys_state_t new_state) {
    if (current_state != new_state) {
        ESP_LOGI(TAG, "State transition: %d -> %d", current_state, new_state);
        current_state = new_state;
        
        // Notify other modules (UI, LEDs) about the state change
        esp_event_post(SYS_EVENT_BASE, SYS_EVENT_STATE_CHANGED, &current_state, sizeof(current_state), portMAX_DELAY);
    }
}

// Technical command handler from Gemini
static void process_gemini_command(const char *cmd) {
    if (cmd == NULL || strlen(cmd) == 0) return;
    
    ESP_LOGI(TAG, "Executing Tech Command: %s", cmd);

    // Example of parsing a volume command: "volume:XX"
    if (strncmp(cmd, "volume:", 7) == 0) {
        int vol = atoi(cmd + 7);
        if (vol >= 0 && vol <= 100) {
            ESP_LOGI(TAG, "Setting volume to %d%%", vol);
            audio_hal_set_volume(vol); // Pass to audio HAL
        }
    }
    // Additional commands can be added here: "mode:sleep", "led:off", "play:music"
}

// Main system event handler (Event Loop FSM)
static void sys_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (event_base != SYS_EVENT_BASE) return;

    switch (event_id) {
        case SYS_EVENT_WAKE_WORD_DETECTED:
            ESP_LOGI(TAG, "Event: WAKE WORD DETECTED");
            change_state(STATE_LISTENING);
            break;

        case SYS_EVENT_SPEECH_END_DETECTED:
            ESP_LOGI(TAG, "Event: SPEECH END DETECTED. Starting STT...");
            change_state(STATE_THINKING);
            break;

        case SYS_EVENT_STT_DONE:
            if (event_data) {
                ESP_LOGI(TAG, "Event: STT DONE. Text: %s", (char*)event_data);
                // At this point, ai_pipeline starts the request to Gemini
            }
            break;

        case SYS_EVENT_GEMINI_DONE:
            if (event_data) {
                gemini_response_t *gemini_data = (gemini_response_t*)event_data;
                ESP_LOGI(TAG, "Event: GEMINI DONE");
                
                // Process technical commands
                process_gemini_command(gemini_data->tech_command);
                
                // Text will be sent to TTS (triggered within ai_pipeline),
                // sys_manager simply waits for the SYS_EVENT_TTS_START event
            }
            break;

        case SYS_EVENT_TTS_START:
            ESP_LOGI(TAG, "Event: TTS START");
            change_state(STATE_SPEAKING);
            break;

        case SYS_EVENT_TTS_END:
            ESP_LOGI(TAG, "Event: TTS END");
            change_state(STATE_IDLE); // Return to idle state
            break;

        case SYS_EVENT_BTN_VOL_UP:
            ESP_LOGI(TAG, "Event: BTN VOL UP");
            // Volume increment logic
            break;

        case SYS_EVENT_BTN_VOL_DOWN:
            ESP_LOGI(TAG, "Event: BTN VOL DOWN");
            // Volume decrement logic
            break;

        case SYS_EVENT_BTN_ACTION:
            ESP_LOGI(TAG, "Event: BTN ACTION (Manual Wake)");
            // If in IDLE, force recording start
            if (current_state == STATE_IDLE) {
                change_state(STATE_LISTENING);
                // Emulation: send command to mic module to begin recording
            } else if (current_state == STATE_SPEAKING) {
                // If the speaker is talking, the button acts as "PAUSE/STOP"
                ESP_LOGI(TAG, "Stopping TTS playback...");
                change_state(STATE_IDLE);
                // Call to stop the audio stream should go here
            }
            break;

        default:
            break;
    }
}

// Wi-Fi system event handler
static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (wifi_retry_count < MAX_WIFI_RETRIES) {
            wifi_retry_count++;
            ESP_LOGW(TAG, "Wi-Fi disconnected. Retrying %d/%d...", wifi_retry_count, MAX_WIFI_RETRIES);
            change_state(STATE_ERROR);
            esp_wifi_connect();
        } else {
            // If retries are exhausted - enable setup mode!
            ESP_LOGE(TAG, "Failed to connect after %d retries. Starting Web Setup.", MAX_WIFI_RETRIES);
            
            // Stop station mode
            esp_wifi_stop(); 
            
            // Switch system to setup mode and start AP
            change_state(STATE_WIFI_SETUP);
            wifi_web_setup_start();
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Wi-Fi connected. IP: " IPSTR, IP2STR(&event->ip_info.ip));
        
        wifi_retry_count = 0; 
        change_state(STATE_IDLE); 
    }
}

// Basic Wi-Fi Station initialization
static void wifi_init(void) {
    ESP_ERROR_CHECK(esp_netif_init());
    
    // Attempt to read SSID from NVS
    nvs_handle_t nvs_handle;
    char ssid[32] = {0};
    char password[64] = {0};
    size_t ssid_len = sizeof(ssid);
    size_t pass_len = sizeof(password);
    bool configured = false;

    if (nvs_open("wifi_cfg", NVS_READONLY, &nvs_handle) == ESP_OK) {
        if (nvs_get_str(nvs_handle, "ssid", ssid, &ssid_len) == ESP_OK) {
            nvs_get_str(nvs_handle, "password", password, &pass_len);
            configured = true;
        }
        nvs_close(nvs_handle);
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    if (configured) {
        // --- STATION MODE (Normal operation) ---
        ESP_LOGI(TAG, "Found Wi-Fi config. Connecting to %s...", ssid);
        esp_netif_create_default_wifi_sta();
        
        esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL);
        esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL);

        wifi_config_t wifi_config = {0};
        strncpy((char*)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
        strncpy((char*)wifi_config.sta.password, password, sizeof(wifi_config.sta.password) - 1);
        wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
        ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE)); 
        ESP_ERROR_CHECK(esp_wifi_start());
        
        // Change state to ERROR/INIT until IP is received
        change_state(STATE_ERROR); 
    } else {
        // --- SETUP MODE (AP + Web Server) ---
        ESP_LOGW(TAG, "No Wi-Fi config found. Entering SETUP mode.");
        change_state(STATE_WIFI_SETUP);
        wifi_web_setup_start();
    }
}

void sys_manager_init(void) {
    ESP_LOGI(TAG, "Initializing System Manager...");

    // 1. Register main FSM handler
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        SYS_EVENT_BASE, 
        ESP_EVENT_ANY_ID, 
        &sys_event_handler, 
        NULL, 
        NULL
    ));

    audio_hal_init();
    display_ui_init();
    led_ctrl_init();
    buttons_hal_init();

    wake_word_init();
    ai_pipeline_init();

    // 4. Connect to network
    wifi_init();

    // 5. Set initial state
    current_state = STATE_ERROR; // Remain in initialization/error status until IP is received
    ESP_LOGI(TAG, "System Manager initialized. Waiting for Wi-Fi...");
}


sys_state_t sys_manager_get_state(void) {
    return current_state;
}