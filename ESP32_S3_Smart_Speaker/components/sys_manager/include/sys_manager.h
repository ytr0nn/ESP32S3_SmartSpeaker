#pragma once

#include "esp_event.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Объявление базового семейства событий системы
ESP_EVENT_DECLARE_BASE(SYS_EVENT_BASE);

// FSM
typedef enum {
    STATE_WIFI_SETUP,
    STATE_IDLE,         // 
    STATE_LISTENING,    // 
    STATE_THINKING,     // 
    STATE_SPEAKING,     // 
    STATE_ERROR         // 
} sys_state_t;

// System Events
typedef enum {
    SYS_EVENT_WAKE_WORD_DETECTED,   // ESP-SR triggers on wake word
    SYS_EVENT_SPEECH_END_DETECTED,  // VAD detects end of speech
    SYS_EVENT_STT_DONE,             // STT returned recognized text
    SYS_EVENT_GEMINI_DONE,          // Gemini returned response 
    SYS_EVENT_TTS_START,            // Start of playing audio 
    SYS_EVENT_TTS_END,              // Кон
    SYS_EVENT_BTN_VOL_UP,           // Нажата кнопка громкости +
    SYS_EVENT_BTN_VOL_DOWN,         // Нажата кнопка громкости -
    SYS_EVENT_BTN_ACTION,           // Нажата кнопка действия (вызов Алисы / пауза)
    SYS_EVENT_STATE_CHANGED         // Состояние системы изменилось (для UI и LED)
} sys_event_id_t;

// SYS_EVENT_GEMINI_DONE
typedef struct {
    char *response_text;            // Text for TTS
    char *tech_command;             // technical command for internal processing
} gemini_response_t;

//sys manager init
void sys_manager_init(void);

//current state getter
sys_state_t sys_manager_get_state(void);

#ifdef __cplusplus
}
#endif