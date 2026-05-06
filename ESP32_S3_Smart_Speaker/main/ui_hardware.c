#include "ui_hardware.h"
#include "main.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "led_strip.h"
#include "esp_log.h"

static const char *TAG = "HARDWARE";
led_strip_handle_t led_strip;

// CD4053BE commutator control
void set_amplifier_source(int source) {
    gpio_set_level(AMP_SWITCH_PIN, source);
}

void ui_hardware_init(void) {
    // commutator CD4053BE init
    gpio_reset_pin(AMP_SWITCH_PIN);
    gpio_set_direction(AMP_SWITCH_PIN, GPIO_MODE_OUTPUT);
    set_amplifier_source(0); // Default is ESP32 

    // Sensor buttons TOUCH init 
    gpio_config_t btn_config = {
        .pin_bit_mask = (1ULL<<TOUCH_BTN_1_PIN) | (1ULL<<TOUCH_BTN_2_PIN) | (1ULL<<TOUCH_BTN_3_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE // pinstriggering will be handled in software
    };
    gpio_config(&btn_config);

    // 3 spi for display (GC9A01)
    spi_bus_config_t buscfg = {
        .sclk_io_num = DISP_SCL_PIN,
        .mosi_io_num = DISP_SDA_PIN,
        .miso_io_num = -1, // MISO dosent used for display
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 240 * 240 * 2 + 8 // bufer size for full screen
    };
    ESP_ERROR_CHECK(spi_bus_initialize(DISP_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO));
    ESP_LOGI(TAG, "SPI bus for display initialized.");

    // 4. Инициализация LED кольца (через RMT драйвер)
    led_strip_config_t strip_config = {
        .strip_gpio_num = LED_STRIP_PIN,
        .max_leds = LED_STRIP_COUNT, 
    };
    led_strip_rmt_config_t rmt_config = {
        .resolution_hz = 10 * 1000 * 1000, // 10MHz
    };
    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip));
    led_strip_clear(led_strip); // all led off by default
    ESP_LOGI(TAG, "LED ring initialized.");
}