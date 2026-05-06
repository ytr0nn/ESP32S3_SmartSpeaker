#include "audio_i2s.h"
#include "main.h"
#include "driver/i2s_std.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/ringbuf.h"

static const char *TAG = "AUDIO_I2S";

// Ring buffer handle for passing audio data from I2S RX task to AFE processing task
extern RingbufHandle_t audio_rx_ringbuf;

// Channel handles for I2S
i2s_chan_handle_t rx_chan; // Microphone
i2s_chan_handle_t tx_chan; // DAC

void audio_i2s_init(void) {
    // 1. soft mute init
    gpio_reset_pin(DAC_XSMT_PIN);
    gpio_set_direction(DAC_XSMT_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(DAC_XSMT_PIN, 0); // ЦАП выключен (Muted)

    // 2. microphone setting
    i2s_chan_config_t rx_chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(MIC_I2S_PORT, I2S_ROLE_MASTER);
    ESP_ERROR_CHECK(i2s_new_channel(&rx_chan_cfg, NULL, &rx_chan));

    i2s_std_config_t rx_std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_SAMPLE_RATE),
        // ICS43434 outputs 24-bit left-justified data in a 32-bit word, so we configure Philips mode with 32-bit slots and mono
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = MIC_BCLK_PIN,
            .ws   = MIC_WS_PIN,
            .dout = I2S_GPIO_UNUSED,
            .din  = MIC_DATA_PIN,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv   = false,
            },
        },
    };
    rx_std_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT;
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(rx_chan, &rx_std_cfg));

    // 3. DAC setting 
    i2s_chan_config_t tx_chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(DAC_I2S_PORT, I2S_ROLE_MASTER);
    ESP_ERROR_CHECK(i2s_new_channel(&tx_chan_cfg, &tx_chan, NULL));

    i2s_std_config_t tx_std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = DAC_BCLK_PIN,
            .ws   = DAC_WS_PIN,
            .dout = DAC_DATA_PIN,
            .din  = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv   = false,
            },
        },
    };
    tx_std_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_BOTH;
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx_chan, &tx_std_cfg));

    // 4. i2s enable
    ESP_ERROR_CHECK(i2s_channel_enable(rx_chan));
    ESP_ERROR_CHECK(i2s_channel_enable(tx_chan));

    // 5. soft mute off
    vTaskDelay(pdMS_TO_TICKS(100)); 
    gpio_set_level(DAC_XSMT_PIN, 1); 
    ESP_LOGI(TAG, "I2S initialized successfully.");
}

// read buffer task and send to AFE engine
void audio_rx_task(void *pvParameters) {
    // buffer for raw I2S data 32bit
    size_t dma_buf_len = 1024;
    int32_t *raw_buffer = malloc(dma_buf_len * sizeof(int32_t));
    
    // 16bit buffer for esp-sr
    int16_t *afe_buffer = malloc(dma_buf_len * sizeof(int16_t));
    
    if (!raw_buffer || !afe_buffer) {
        ESP_LOGE(TAG, "Failed to allocate audio buffers");
        vTaskDelete(NULL);
    }

    size_t bytes_read = 0;
    ESP_LOGI(TAG, "Audio RX Task started.");

    while (1) {
        // read raw audio data from I2S
        if (i2s_channel_read(rx_chan, raw_buffer, dma_buf_len * sizeof(int32_t), &bytes_read, portMAX_DELAY) == ESP_OK) {
            
            int samples_read = bytes_read / sizeof(int32_t);
            
            // Convert 32-bit (24-bit left-justified) to 16-bit PCM
            for (int i = 0; i < samples_read; i++) {
                // Сдвигаем вправо на 16 бит, чтобы получить старшие 16 бит из 32-битного слова
                afe_buffer[i] = (int16_t)(raw_buffer[i] >> 16); 
                
                // micro amplification for better wake word detection
                afe_buffer[i] = afe_buffer[i] << 2; 
            }

            // Push 16-bit PCM data to RingBuffer for Wake Word processing
            if (audio_rx_ringbuf != NULL) {
                xRingbufferSend(audio_rx_ringbuf, afe_buffer, samples_read * sizeof(int16_t), pdMS_TO_TICKS(100));
            }
        }
    }
}