#include "display_ui.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>
#include "lvgl.h"
#include "sys_manager.h"

static const char *TAG = "DISPLAY_UI";

//Hardware Pin Definitions GC9A01
#define DISP_SPI_HOST    SPI2_HOST
#define PIN_NUM_MISO     -1   
#define PIN_NUM_MOSI     11   // SDA
#define PIN_NUM_CLK      12   // SCL
#define PIN_NUM_CS       10
#define PIN_NUM_DC       9
#define PIN_NUM_RST      8

//Display Resolution
#define DISP_WIDTH       240
#define DISP_HEIGHT      240

//LVGL Memory & Buffer Config 
//
#define LVGL_BUFFER_SIZE (DISP_WIDTH * 10)

//LVGL Thread Safety
static SemaphoreHandle_t xGuiSemaphore = NULL;

// UI Elements
static lv_obj_t *main_screen;
static lv_obj_t *status_label;
static lv_obj_t *volume_bar;
static lv_obj_t *volume_label;
static lv_timer_t *vol_timer;

static spi_device_handle_t spi_dev;

// 
// Low-Level SPI
// 

static void lcd_cmd(spi_device_handle_t spi, const uint8_t cmd) {
    esp_err_t ret;
    spi_transaction_t t = {
        .length = 8,
        .tx_buffer = &cmd,
        .user = (void*)0 // 0 means Command (DC = Low)
    };
    ret = spi_device_polling_transmit(spi, &t);
    assert(ret == ESP_OK);
}

static void lcd_data(spi_device_handle_t spi, const uint8_t *data, int len) {
    if (len == 0) return;
    esp_err_t ret;
    spi_transaction_t t = {
        .length = len * 8,
        .tx_buffer = data,
        .user = (void*)1 // 1 means Data (DC = High)
    };
    ret = spi_device_polling_transmit(spi, &t);
    assert(ret == ESP_OK);
}

static void lcd_spi_pre_transfer_callback(spi_transaction_t *t) {
    int dc = (int)t->user;
    gpio_set_level(PIN_NUM_DC, dc);
}

// Standard initialization sequence for GC9A01
static void gc9a01_init_sequence(spi_device_handle_t spi) {
    ESP_LOGI(TAG, "Sending GC9A01 Full Initialization sequence...");

    // Hard reset
    gpio_set_level(PIN_NUM_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(100));
    gpio_set_level(PIN_NUM_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(150)); // Mandatory delay after reset

    lcd_cmd(spi, 0xEF);
    lcd_cmd(spi, 0xEB); lcd_data(spi, (uint8_t[]){0x14}, 1);
    lcd_cmd(spi, 0xFE);
    lcd_cmd(spi, 0xEF);
    lcd_cmd(spi, 0xEB); lcd_data(spi, (uint8_t[]){0x14}, 1);
    lcd_cmd(spi, 0x84); lcd_data(spi, (uint8_t[]){0x40}, 1);
    lcd_cmd(spi, 0x85); lcd_data(spi, (uint8_t[]){0xFF}, 1);
    lcd_cmd(spi, 0x86); lcd_data(spi, (uint8_t[]){0xFF}, 1);
    lcd_cmd(spi, 0x87); lcd_data(spi, (uint8_t[]){0xFF}, 1);
    lcd_cmd(spi, 0x88); lcd_data(spi, (uint8_t[]){0x0A}, 1);
    lcd_cmd(spi, 0x89); lcd_data(spi, (uint8_t[]){0x21}, 1);
    lcd_cmd(spi, 0x8A); lcd_data(spi, (uint8_t[]){0x00}, 1);
    lcd_cmd(spi, 0x8B); lcd_data(spi, (uint8_t[]){0x80}, 1);
    lcd_cmd(spi, 0x8C); lcd_data(spi, (uint8_t[]){0x01}, 1);
    lcd_cmd(spi, 0x8D); lcd_data(spi, (uint8_t[]){0x01}, 1);
    lcd_cmd(spi, 0x8E); lcd_data(spi, (uint8_t[]){0xFF}, 1);
    lcd_cmd(spi, 0x8F); lcd_data(spi, (uint8_t[]){0xFF}, 1);
    lcd_cmd(spi, 0xB6); lcd_data(spi, (uint8_t[]){0x00, 0x20}, 2); // Display Function Control
    lcd_cmd(spi, 0x36); lcd_data(spi, (uint8_t[]){0x08}, 1);       // MADCTL (Memory Access Control) - Drawing direction
    lcd_cmd(spi, 0x3A); lcd_data(spi, (uint8_t[]){0x05}, 1);       // COLMOD: 16-bit RGB565
    lcd_cmd(spi, 0x90); lcd_data(spi, (uint8_t[]){0x08, 0x08, 0x08, 0x08}, 4);
    lcd_cmd(spi, 0xBD); lcd_data(spi, (uint8_t[]){0x06}, 1);
    lcd_cmd(spi, 0xBC); lcd_data(spi, (uint8_t[]){0x00}, 1);
    lcd_cmd(spi, 0xFF); lcd_data(spi, (uint8_t[]){0x60, 0x01, 0x04}, 3);
    lcd_cmd(spi, 0xC3); lcd_data(spi, (uint8_t[]){0x13}, 1);
    lcd_cmd(spi, 0xC4); lcd_data(spi, (uint8_t[]){0x13}, 1);
    lcd_cmd(spi, 0xC9); lcd_data(spi, (uint8_t[]){0x22}, 1);
    lcd_cmd(spi, 0xBE); lcd_data(spi, (uint8_t[]){0x11}, 1);
    lcd_cmd(spi, 0xE1); lcd_data(spi, (uint8_t[]){0x10, 0x0E}, 2);
    lcd_cmd(spi, 0xDF); lcd_data(spi, (uint8_t[]){0x21, 0x0c, 0x02}, 3);
    lcd_cmd(spi, 0xF0); lcd_data(spi, (uint8_t[]){0x45, 0x09, 0x08, 0x08, 0x26, 0x2A}, 6);
    lcd_cmd(spi, 0xF1); lcd_data(spi, (uint8_t[]){0x43, 0x70, 0x72, 0x36, 0x37, 0x6F}, 6);
    lcd_cmd(spi, 0xF2); lcd_data(spi, (uint8_t[]){0x45, 0x09, 0x08, 0x08, 0x26, 0x2A}, 6);
    lcd_cmd(spi, 0xF3); lcd_data(spi, (uint8_t[]){0x43, 0x70, 0x72, 0x36, 0x37, 0x6F}, 6);
    lcd_cmd(spi, 0xED); lcd_data(spi, (uint8_t[]){0x1B, 0x0B}, 2);
    lcd_cmd(spi, 0xAE); lcd_data(spi, (uint8_t[]){0x77}, 1);
    lcd_cmd(spi, 0xCD); lcd_data(spi, (uint8_t[]){0x63}, 1);
    lcd_cmd(spi, 0x70); lcd_data(spi, (uint8_t[]){0x07, 0x07, 0x04, 0x0E, 0x0F, 0x09, 0x07, 0x08, 0x03}, 9);
    lcd_cmd(spi, 0xE8); lcd_data(spi, (uint8_t[]){0x34}, 1);
    lcd_cmd(spi, 0x62); lcd_data(spi, (uint8_t[]){0x18, 0x0D, 0x71, 0xED, 0x70, 0x70, 0x18, 0x0F, 0x71, 0xEF, 0x70, 0x70}, 12);
    lcd_cmd(spi, 0x63); lcd_data(spi, (uint8_t[]){0x18, 0x11, 0x71, 0xF1, 0x70, 0x70, 0x18, 0x13, 0x71, 0xF3, 0x70, 0x70}, 12);
    lcd_cmd(spi, 0x64); lcd_data(spi, (uint8_t[]){0x28, 0x29, 0xF1, 0x01, 0xF1, 0x00, 0x07}, 7);
    lcd_cmd(spi, 0x66); lcd_data(spi, (uint8_t[]){0x3C, 0x00, 0xCD, 0x67, 0x45, 0x45, 0x10, 0x00, 0x00, 0x00}, 10);
    lcd_cmd(spi, 0x67); lcd_data(spi, (uint8_t[]){0x00, 0x3C, 0x00, 0x00, 0x00, 0x01, 0x54, 0x10, 0x32, 0x98}, 10);
    lcd_cmd(spi, 0x74); lcd_data(spi, (uint8_t[]){0x10, 0x85, 0x80, 0x00, 0x00, 0x4E, 0x00}, 7);
    lcd_cmd(spi, 0x98); lcd_data(spi, (uint8_t[]){0x3e, 0x07}, 2);
    lcd_cmd(spi, 0x35); lcd_cmd(spi, 0x00);
    
    lcd_cmd(spi, 0x21); // Display Inversion ON 
    
    lcd_cmd(spi, 0x11); // Sleep Out
    vTaskDelay(pdMS_TO_TICKS(120));
    lcd_cmd(spi, 0x29); // Display On
    vTaskDelay(pdMS_TO_TICKS(20));
}

// 
// LVGL Porting Interfaces
// 

static void disp_flush(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map) {
    uint32_t w = (area->x2 - area->x1 + 1);
    uint32_t h = (area->y2 - area->y1 + 1);
    uint32_t len = w * h * 2; // 16bit color 2 bytes

    // set winfod address
    lcd_cmd(spi_dev, 0x2A);
    lcd_data(spi_dev, (uint8_t[]){(area->x1 >> 8) & 0xFF, area->x1 & 0xFF, (area->x2 >> 8) & 0xFF, area->x2 & 0xFF}, 4);
    lcd_cmd(spi_dev, 0x2B);
    lcd_data(spi_dev, (uint8_t[]){(area->y1 >> 8) & 0xFF, area->y1 & 0xFF, (area->y2 >> 8) & 0xFF, area->y2 & 0xFF}, 4);
    
    // Write RAM
    lcd_cmd(spi_dev, 0x2C);
    
    spi_transaction_t t = {
        .length = len * 8, // length in bits
        .tx_buffer = color_map,
        .user = (void*)1
    };
    spi_device_polling_transmit(spi_dev, &t);

    lv_disp_flush_ready(drv);
}

static void lv_tick_task(void *arg) {
    lv_tick_inc(1);
}

//
// User Interface
// 

static void vol_timeout_cb(lv_timer_t *timer) {
    if (xSemaphoreTake(xGuiSemaphore, portMAX_DELAY) == pdTRUE) {
        lv_obj_add_flag(volume_bar, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(volume_label, LV_OBJ_FLAG_HIDDEN);
        xSemaphoreGive(xGuiSemaphore);
    }
}

static void create_ui(void) {
    main_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(main_screen, lv_color_hex(0x000000), LV_PART_MAIN);

    status_label = lv_label_create(main_screen);
    lv_label_set_text(status_label, "Ready");
    lv_obj_set_style_text_color(status_label, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_center(status_label);

    volume_bar = lv_bar_create(main_screen);
    lv_obj_set_style_bg_color(volume_bar, lv_color_hex(0x333333), LV_PART_MAIN);
    lv_obj_set_size(volume_bar, 100, 10);
    lv_obj_align(volume_bar, LV_ALIGN_BOTTOM_MID, 0, -30);
    lv_bar_set_range(volume_bar, 0, 100);
    lv_bar_set_value(volume_bar, 50, LV_ANIM_OFF);
    lv_obj_add_flag(volume_bar, LV_OBJ_FLAG_HIDDEN);

    volume_label = lv_label_create(main_screen);
    lv_label_set_text(volume_label, "50%");
    lv_obj_set_style_text_color(volume_label, lv_color_hex(0x00FFFF), LV_PART_MAIN);
    lv_obj_align_to(volume_label, volume_bar, LV_ALIGN_OUT_TOP_MID, 0, -5);
    lv_obj_add_flag(volume_label, LV_OBJ_FLAG_HIDDEN);

    lv_disp_load_scr(main_screen);

    vol_timer = lv_timer_create(vol_timeout_cb, 2000, NULL);
    lv_timer_pause(vol_timer);
}

static void update_status_ui(const char *text, uint32_t color_hex) {
    if (xSemaphoreTake(xGuiSemaphore, portMAX_DELAY) == pdTRUE) {
        lv_label_set_text(status_label, text);
        lv_obj_set_style_text_color(status_label, lv_color_hex(color_hex), LV_PART_MAIN);
        xSemaphoreGive(xGuiSemaphore);
    }
}

//
// System Event Handlers
//

static void sys_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (event_base != SYS_EVENT_BASE) return;

    switch (event_id) {
        case SYS_EVENT_STATE_CHANGED: {
            sys_state_t new_state = *(sys_state_t*)event_data;
            switch(new_state) {
                case STATE_IDLE: update_status_ui("Wait", 0x888888); break;
                case STATE_WIFI_SETUP: update_status_ui("Wifi Settings", 0x00A0FF); break;
                case STATE_LISTENING: update_status_ui("Listening...", 0x00FF00); break;
                case STATE_THINKING: update_status_ui("Thinking...", 0xFFFF00); break;
                case STATE_SPEAKING: update_status_ui("Speaking...", 0x00FFFF); break;
                case STATE_ERROR: update_status_ui("Error!", 0xFF0000); break;
            }
            break;
        }
        case SYS_EVENT_BTN_VOL_UP:
        case SYS_EVENT_BTN_VOL_DOWN:
            if (xSemaphoreTake(xGuiSemaphore, portMAX_DELAY) == pdTRUE) {
                int current_vol = lv_bar_get_value(volume_bar);
                if (event_id == SYS_EVENT_BTN_VOL_UP) current_vol = (current_vol + 10 <= 100) ? current_vol + 10 : 100;
                if (event_id == SYS_EVENT_BTN_VOL_DOWN) current_vol = (current_vol - 10 >= 0) ? current_vol - 10 : 0;
                lv_bar_set_value(volume_bar, current_vol, LV_ANIM_ON);
                lv_label_set_text_fmt(volume_label, "%d%%", current_vol);
                lv_obj_clear_flag(volume_bar, LV_OBJ_FLAG_HIDDEN);
                lv_obj_clear_flag(volume_label, LV_OBJ_FLAG_HIDDEN);
                lv_timer_reset(vol_timer);
                lv_timer_resume(vol_timer);
                xSemaphoreGive(xGuiSemaphore);
            }
            break;
    }
}

//
// FreeRTOS Tasks
//

static void display_task(void *pvParameters) {
    ESP_LOGI(TAG, "Display GUI Task started on Core %d", xPortGetCoreID());
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10));
        if (xSemaphoreTake(xGuiSemaphore, portMAX_DELAY) == pdTRUE) {
            lv_timer_handler();
            xSemaphoreGive(xGuiSemaphore);
        }
    }
}

//
// Public API
// 

void display_ui_init(void) {
    ESP_LOGI(TAG, "Initializing Display & LVGL in LOW MEMORY mode...");

    xGuiSemaphore = xSemaphoreCreateMutex();

    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << PIN_NUM_DC) | (1ULL << PIN_NUM_RST),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = 0,
        .pull_down_en = 0,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);

    spi_bus_config_t buscfg = {
        .miso_io_num = PIN_NUM_MISO,
        .mosi_io_num = PIN_NUM_MOSI,
        .sclk_io_num = PIN_NUM_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        // Max transaction size for the new reduced buffer
        .max_transfer_sz = LVGL_BUFFER_SIZE * 2 + 8 
    };
    ESP_ERROR_CHECK(spi_bus_initialize(DISP_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO));

    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = 40 * 1000 * 1000, 
        .mode = 0, 
        .spics_io_num = PIN_NUM_CS,
        .queue_size = 7,
        .pre_cb = lcd_spi_pre_transfer_callback,
    };
    ESP_ERROR_CHECK(spi_bus_add_device(DISP_SPI_HOST, &devcfg, &spi_dev));

    gc9a01_init_sequence(spi_dev);
    lv_init();

    // Allocate memory. Use exactly as much as needed, no more.
    lv_color_t *buf1 = heap_caps_malloc(LVGL_BUFFER_SIZE * sizeof(lv_color_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    lv_color_t *buf2 = heap_caps_malloc(LVGL_BUFFER_SIZE * sizeof(lv_color_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    
    if (buf1 == NULL || buf2 == NULL) {
        ESP_LOGE(TAG, "CRITICAL ERROR: Not enough DMA memory for LVGL!");
        return;
    }

    static lv_disp_draw_buf_t draw_buf;
    lv_disp_draw_buf_init(&draw_buf, buf1, buf2, LVGL_BUFFER_SIZE);

    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = DISP_WIDTH;
    disp_drv.ver_res = DISP_HEIGHT;
    disp_drv.flush_cb = disp_flush;
    disp_drv.draw_buf = &draw_buf;
    lv_disp_drv_register(&disp_drv);

    const esp_timer_create_args_t tick_timer_args = {
        .callback = &lv_tick_task,
        .name = "lvgl_tick"
    };
    esp_timer_handle_t tick_timer;
    ESP_ERROR_CHECK(esp_timer_create(&tick_timer_args, &tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(tick_timer, 1000)); 

    create_ui();

    ESP_ERROR_CHECK(esp_event_handler_instance_register(SYS_EVENT_BASE, ESP_EVENT_ANY_ID, &sys_event_handler, NULL, NULL));

    xTaskCreatePinnedToCore(display_task, "display_task", 4096, NULL, 5, NULL, 0);

    ESP_LOGI(TAG, "Display UI initialized successfully.");
}