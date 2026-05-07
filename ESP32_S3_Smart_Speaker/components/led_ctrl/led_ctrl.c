#include "led_ctrl.h"
#include "esp_log.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led_strip.h"
#include "sys_manager.h"
#include <math.h>

static const char *TAG = "LED_CTRL";

// --- Hardware Configuration ---
#define LED_STRIP_BLINK_GPIO  48
#define LED_STRIP_LED_NUMBERS 32
#define LED_FRAME_RATE_MS     30  // ~33 FPS for smooth animations

static led_strip_handle_t led_strip;
static sys_state_t current_state = STATE_IDLE;

// ==============================================================================
// System Event Handler
// ==============================================================================
static void sys_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (event_base == SYS_EVENT_BASE && event_id == SYS_EVENT_STATE_CHANGED) {
        if (event_data) {
            sys_state_t new_state = *(sys_state_t*)event_data;
            if (current_state != new_state) {
                ESP_LOGI(TAG, "LED state transitioning to: %d", new_state);
                current_state = new_state;
            }
        }
    }
}

// ==============================================================================
// Animation Patterns
// ==============================================================================

// Helper to convert HSV to RGB for smoother color transitions
static void hsv2rgb(uint32_t h, uint32_t s, uint32_t v, uint32_t *r, uint32_t *g, uint32_t *b) {
    h %= 360; 
    uint32_t rgb_max = v * 2.55f;
    uint32_t rgb_min = rgb_max * (100 - s) / 100.0f;
    uint32_t i = h / 60;
    uint32_t diff = h % 60;
    uint32_t rgb_adj = (rgb_max - rgb_min) * diff / 60;

    switch (i) {
        case 0: *r = rgb_max; *g = rgb_min + rgb_adj; *b = rgb_min; break;
        case 1: *r = rgb_max - rgb_adj; *g = rgb_max; *b = rgb_min; break;
        case 2: *r = rgb_min; *g = rgb_max; *b = rgb_min + rgb_adj; break;
        case 3: *r = rgb_min; *g = rgb_max - rgb_adj; *b = rgb_max; break;
        case 4: *r = rgb_min + rgb_adj; *g = rgb_min; *b = rgb_max; break;
        default: *r = rgb_max; *g = rgb_min; *b = rgb_max - rgb_adj; break;
    }
}

static void led_animation_task(void *pvParameters) {
    ESP_LOGI(TAG, "LED Animation Task started on Core %d", xPortGetCoreID());
    
    uint32_t tick = 0;
    uint32_t r, g, b;

    while (1) {
        led_strip_clear(led_strip);

        switch (current_state) {
            case STATE_WIFI_SETUP: {
    // slow blue for Wi-Fi setup
                float pulse = (sin((float)tick * 0.05f) + 1.0f) / 2.0f;
                uint8_t brightness = (uint8_t)(10 + pulse * 100);
                for (int i = 0; i < LED_STRIP_LED_NUMBERS; i++) {
                led_strip_set_pixel(led_strip, i, 0, 0, brightness); // Синий
                }
                break;
}
            case STATE_IDLE: {
                // Breathing effect (Cyan color)
                // Sine wave
                float breathe = (sin((float)tick * 0.05f) + 1.0f) / 2.0f; 
                uint8_t brightness = (uint8_t)(5 + (breathe * 35));
                for (int i = 0; i < LED_STRIP_LED_NUMBERS; i++) {
                    led_strip_set_pixel(led_strip, i, 0, brightness, brightness); // Cyan
                }
                break;
            }

            case STATE_LISTENING: {
                // Rotating Ring effectreen
                // Creates a comet tail that spins around the 32 LED
                int head_pos = (tick / 2) % LED_STRIP_LED_NUMBERS;
                for (int i = 0; i < 8; i++) { // Tail length = 8
                    int pixel = (head_pos - i + LED_STRIP_LED_NUMBERS) % LED_STRIP_LED_NUMBERS;
                    uint8_t fade = 255 - (i * 30); // Fade out tail
                    led_strip_set_pixel(led_strip, pixel, 0, fade, 0); // Pure Green
                }
                break;
            }

            case STATE_THINKING: {
                // Pulsing effect Purple
                // Simulates processing data with API
                for (int i = 0; i < LED_STRIP_LED_NUMBERS; i++) {
                    float pulse = (sin((float)(tick + i * 2) * 0.2f) + 1.0f) / 2.0f;
                    uint8_t intensity = (uint8_t)(20 + pulse * 100);
                    led_strip_set_pixel(led_strip, i, intensity / 2, 0, intensity); // Purple
                }
                break;
            }

            case STATE_SPEAKING: {
                // random sparkles with rainbow colors
                // 
                uint32_t hue = (tick * 5) % 360;
                for (int i = 0; i < LED_STRIP_LED_NUMBERS; i++) {
                    // Two moving waves colliding
                    float wave = sin((float)i * 0.5f + (float)tick * 0.2f) + sin((float)i * 0.5f - (float)tick * 0.15f);
                    if (wave > 0.5f) {
                        hsv2rgb(hue + (i * 10), 100, 100, &r, &g, &b);
                        led_strip_set_pixel(led_strip, i, r, g, b);
                    }
                }
                break;
            }

            case STATE_ERROR: {
                // Fast Flashing Red
                if ((tick / 10) % 2 == 0) {
                    for (int i = 0; i < LED_STRIP_LED_NUMBERS; i++) {
                        led_strip_set_pixel(led_strip, i, 150, 0, 0); // Red
                    }
                }
                break;
            }

            default:
                break;
        }

        // Push pixels to the hardware
        led_strip_refresh(led_strip);

        // Frame delay and tick increment
        vTaskDelay(pdMS_TO_TICKS(LED_FRAME_RATE_MS));
        tick++;
    }
}

// ==============================================================================
// Public API
// ==============================================================================

void led_ctrl_init(void) {
    ESP_LOGI(TAG, "Initializing LED Strip WS2812...");

    // 1. Configure the LED strip hardware (RMT peripheral)
    led_strip_config_t strip_config = {
        .strip_gpio_num = LED_STRIP_BLINK_GPIO,
        .max_leds = LED_STRIP_LED_NUMBERS,
        .led_pixel_format = LED_PIXEL_FORMAT_GRB, // Standard WS2812 format
        .led_model = LED_MODEL_WS2812,
        .flags.invert_out = false,
    };

    // RMT backend. It's perfectly suited for driving LED efficiently
    led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000, // 10MHz resolution for RMT
        .flags.with_dma = false,
    };

    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip));
    
    // Clear strip completely on startup
    led_strip_clear(led_strip);

    // 2. Register System Event listener
    // So the LED module knows when to change animations
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        SYS_EVENT_BASE, 
        SYS_EVENT_STATE_CHANGED, 
        &sys_event_handler, 
        NULL, 
        NULL
    ));

    // 3. Start the Animation Task
    xTaskCreatePinnedToCore(
        led_animation_task, 
        "led_anim_task", 
        4096, 
        NULL, 
        2,      // low priority since it's just visual feedback
        NULL, 
        0       // core 0
    );

    ESP_LOGI(TAG, "LED Control module initialized successfully.");
}