#include "buttons_hal.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_timer.h"

#include "sys_manager.h"

static const char *TAG = "BUTTONS_HAL";

// --- Hardware Pins ---
#define BTN_PIN_VOL_UP   1
#define BTN_PIN_VOL_DOWN 2
#define BTN_PIN_ACTION   47

// --- Configuration ---
#define DEBOUNCE_TIME_MS 50
#define LONG_PRESS_MS    1000

// Structure to pass data from ISR to Task
typedef struct {
    uint32_t gpio_num;
    uint32_t level;
} btn_isr_event_t;

static QueueHandle_t gpio_evt_queue = NULL;

// ==============================================================================
// Interrupt Service Routine (ISR)
// ==============================================================================
// Note: Keep ISR as short as possible. IRAM_ATTR places it in RAM for speed.
static void IRAM_ATTR gpio_isr_handler(void* arg) {
    uint32_t gpio_num = (uint32_t) arg;
    btn_isr_event_t evt;
    evt.gpio_num = gpio_num;
    evt.level = gpio_get_level(gpio_num);
    
    // Send to queue from ISR without blocking
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xQueueSendFromISR(gpio_evt_queue, &evt, &xHigherPriorityTaskWoken);
    if (xHigherPriorityTaskWoken) {
        portYIELD_FROM_ISR();
    }
}

// ==============================================================================
// Button Processing Task
// ==============================================================================
static void buttons_task(void* arg) {
    ESP_LOGI(TAG, "Buttons Processing Task started on Core %d", xPortGetCoreID());

    btn_isr_event_t evt;
    
    // State tracking arrays for the 3 buttons
    // Indices: 0 -> VOL_UP, 1 -> VOL_DOWN, 2 -> ACTION
    uint32_t pins[3] = {BTN_PIN_VOL_UP, BTN_PIN_VOL_DOWN, BTN_PIN_ACTION};
    uint64_t press_time[3] = {0, 0, 0};
    bool is_pressed[3] = {false, false, false};

    while (1) {
        // Wait for an event from the ISR
        if (xQueueReceive(gpio_evt_queue, &evt, portMAX_DELAY)) {
            
            // Find which array index corresponds to the triggered GPIO
            int idx = -1;
            for (int i = 0; i < 3; i++) {
                if (pins[i] == evt.gpio_num) {
                    idx = i;
                    break;
                }
            }
            if (idx == -1) continue; // Unknown pin

            uint64_t now_ms = esp_timer_get_time() / 1000;

            // TTP223 typically outputs HIGH (1) when touched
            if (evt.level == 1 && !is_pressed[idx]) {
                // Button Pressed
                is_pressed[idx] = true;
                press_time[idx] = now_ms;
            } 
            else if (evt.level == 0 && is_pressed[idx]) {
                // Button Released
                is_pressed[idx] = false;
                uint64_t duration = now_ms - press_time[idx];

                // Software Debounce Check
                if (duration > DEBOUNCE_TIME_MS) {
                    bool is_long_press = (duration >= LONG_PRESS_MS);
                    
                    ESP_LOGI(TAG, "Button %" PRIu32 " released. Duration: %" PRIu64 " ms (%s)", 
                             evt.gpio_num, duration, is_long_press ? "LONG" : "SHORT");

                    // Determine which event to post
                    sys_event_id_t sys_evt_id = 0;
                    if (evt.gpio_num == BTN_PIN_VOL_UP) {
                        sys_evt_id = SYS_EVENT_BTN_VOL_UP;
                    } else if (evt.gpio_num == BTN_PIN_VOL_DOWN) {
                        sys_evt_id = SYS_EVENT_BTN_VOL_DOWN;
                    } else if (evt.gpio_num == BTN_PIN_ACTION) {
                        sys_evt_id = SYS_EVENT_BTN_ACTION;
                    }

                    // Post to system manager event loop
                    // We can pass the is_long_press flag as event_data if needed by sys_manager
                    esp_event_post(SYS_EVENT_BASE, sys_evt_id, &is_long_press, sizeof(bool), portMAX_DELAY);
                }
            }
        }
    }
}

// ==============================================================================
// Public API
// ==============================================================================

void buttons_hal_init(void) {
    ESP_LOGI(TAG, "Initializing Buttons HAL...");

    // Create a queue to handle up to 10 simultaneous edge
    gpio_evt_queue = xQueueCreate(10, sizeof(btn_isr_event_t));

    // Configure GPIO
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_ANYEDGE, // Trigger on both press and release
        .pin_bit_mask = (1ULL << BTN_PIN_VOL_UP) | (1ULL << BTN_PIN_VOL_DOWN) | (1ULL << BTN_PIN_ACTION),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = 0,
        .pull_down_en = 0 // no int pull up
    };
    gpio_config(&io_conf);

    // Install global GPIO ISR service
    // ESP_INTR_FLAG_IRAM allows the interrupt to fire even if cache is disabled
    gpio_install_isr_service(ESP_INTR_FLAG_IRAM);

    // Hook ISR handlers for specific pins
    gpio_isr_handler_add(BTN_PIN_VOL_UP, gpio_isr_handler, (void*) BTN_PIN_VOL_UP);
    gpio_isr_handler_add(BTN_PIN_VOL_DOWN, gpio_isr_handler, (void*) BTN_PIN_VOL_DOWN);
    gpio_isr_handler_add(BTN_PIN_ACTION, gpio_isr_handler, (void*) BTN_PIN_ACTION);

    // Start processing task
    //
    //xTaskCreatePinnedToCore(buttons_task, "buttons_task", 2048, NULL, 10, NULL, 1);
    //increased because stack overflow
    xTaskCreatePinnedToCore(buttons_task, "buttons_task", 4096, NULL, 10, NULL, 1);
    ESP_LOGI(TAG, "Buttons HAL initialized successfully.");
}