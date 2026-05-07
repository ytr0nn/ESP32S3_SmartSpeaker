#include <stdio.h>
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "sys_manager.h"

static const char *TAG = "MAIN";

void app_main(void)
{
    ESP_LOGI(TAG, "Starting Smart AI Speaker Initialization YURII OSYPENKO");
    ESP_LOGI(TAG, "Free heap: %" PRIu32 " bytes", esp_get_free_heap_size());

    // 1. NVS 
    // 
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS flash error");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // 2. (ESP Event Loop)
    // 
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // 3. State Machine
    // 
    // 
    sys_manager_init();

    ESP_LOGI(TAG, "Initialization complete. System Manager is running.");
    
}