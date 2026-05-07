#include "audio_hal.h"
#include "esp_log.h"
#include "driver/i2s_std.h"
#include "driver/gpio.h"
#include "freertos/task.h"
#include "freertos/ringbuf.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "AUDIO_HAL";

//Pins 
#define I2S0_RX_BCLK_PIN  4
#define I2S0_RX_LRCK_PIN  5
#define I2S0_RX_DIN_PIN   6

#define I2S1_TX_BCLK_PIN  15
#define I2S1_TX_LRCK_PIN  16
#define I2S1_TX_DOUT_PIN  17
#define I2S1_TX_XSMT_PIN  14 

#define MUX_CTRL_PIN      7

//Configuration 
#define SAMPLE_RATE_RX    16000 
#define SAMPLE_RATE_TX    24000 // 
#define I2S_DMA_BUFFERS   32    // Increased for stability under high CPU load
#define I2S1_DMA_BUF_LEN  512   

#define RX_RINGBUF_SIZE   (128 * 1024) // Large buffer to prevent Wi-Fi interference under load
#define TX_RINGBUF_SIZE   (64 * 1024)

static i2s_chan_handle_t rx_chan = NULL;
static i2s_chan_handle_t tx_chan = NULL;
static RingbufHandle_t rx_ringbuf = NULL;
static RingbufHandle_t tx_ringbuf = NULL;
static uint8_t current_volume = 100;

// 
// Background FreeRTOS Tasks
// 

static void audio_rx_task(void *args) {
    size_t chunk_size = 1024;
    int16_t *rx_buf = (int16_t *)malloc(chunk_size);
    size_t bytes_read = 0;
    while (1) {
        if (i2s_channel_read(rx_chan, rx_buf, chunk_size, &bytes_read, portMAX_DELAY) == ESP_OK) {
            xRingbufferSend(rx_ringbuf, rx_buf, bytes_read, pdMS_TO_TICKS(10));
        }
    }
}

static void audio_tx_task(void *args) {
    ESP_LOGI(TAG, "Audio TX Task (Mono Mode) started on Core %d", xPortGetCoreID());
    size_t item_size;
    size_t bytes_written = 0;
    
    while (1) {
        // Receive 16-bit MONO data directly from the ringbuffer
        int16_t *item = (int16_t *)xRingbufferReceive(tx_ringbuf, &item_size, portMAX_DELAY);
        
        if (item != NULL) {
            // Apply volume directly to mono data
            if (current_volume != 100) {
                for (size_t i = 0; i < item_size / 2; i++) {
                    item[i] = (int16_t)((int32_t)item[i] * current_volume / 100);
                }
            }

            // Send mono data
            i2s_channel_write(tx_chan, item, item_size, &bytes_written, portMAX_DELAY);
            
            vRingbufferReturnItem(tx_ringbuf, (void *)item);
        }
    }
}

// 
// Hardware Initialization
// 

static void init_dac_hw(void) {
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << I2S1_TX_XSMT_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE
    };
    gpio_config(&io_conf);
    gpio_set_level(I2S1_TX_XSMT_PIN, 1); // Unmute audio
}

static void init_mux(void) {
    gpio_config_t io_conf = {.pin_bit_mask = (1ULL << MUX_CTRL_PIN), .mode = GPIO_MODE_OUTPUT};
    gpio_config(&io_conf);
    gpio_set_level(MUX_CTRL_PIN, 0);
}

static void init_i2s_rx(void) {
    i2s_chan_config_t rx_chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    ESP_ERROR_CHECK(i2s_new_channel(&rx_chan_cfg, NULL, &rx_chan));

    i2s_std_config_t rx_std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE_RX),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {.bclk = I2S0_RX_BCLK_PIN, .ws = I2S0_RX_LRCK_PIN, .din = I2S0_RX_DIN_PIN},
    };


     
    rx_std_cfg.slot_cfg.slot_bit_width = I2S_SLOT_BIT_WIDTH_32BIT;
    rx_std_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT; 

    ESP_ERROR_CHECK(i2s_channel_init_std_mode(rx_chan, &rx_std_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(rx_chan));
}

static void init_i2s_tx(void) {
    i2s_chan_config_t tx_chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
    tx_chan_cfg.dma_desc_num = I2S_DMA_BUFFERS;
    tx_chan_cfg.dma_frame_num = I2S1_DMA_BUF_LEN;
    tx_chan_cfg.auto_clear = true; 

    ESP_ERROR_CHECK(i2s_new_channel(&tx_chan_cfg, &tx_chan, NULL));

    i2s_std_config_t tx_std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE_TX),
        // Specify MONO mode for incoming data from ringbuffer
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED, 
            .bclk = I2S1_TX_BCLK_PIN,
            .ws   = I2S1_TX_LRCK_PIN,
            .dout = I2S1_TX_DOUT_PIN,
        },
    };

     
    // 1. Keep physical slot at 32-bit 
    tx_std_cfg.slot_cfg.slot_bit_width = I2S_SLOT_BIT_WIDTH_32BIT;
    // 2. Enable Hardware Mirroring
    tx_std_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_BOTH; 

    ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx_chan, &tx_std_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(tx_chan));
    ESP_LOGI(TAG, "I2S1 (TX) HW Mono Mirroring init at %d Hz", SAMPLE_RATE_TX);
}

void audio_hal_init(void) {
    ESP_LOGI(TAG, "Initializing Audio HAL...");
    init_mux();
    init_dac_hw();

    // create ringbuffers
    rx_ringbuf = xRingbufferCreate(RX_RINGBUF_SIZE, RINGBUF_TYPE_BYTEBUF);
    tx_ringbuf = xRingbufferCreate(TX_RINGBUF_SIZE, RINGBUF_TYPE_BYTEBUF);
    
    init_i2s_rx();
    init_i2s_tx();

    // Microphone critical task priority set to 20 to ensure stable audio capture under high CPU load
    xTaskCreatePinnedToCore(audio_rx_task, "audio_rx_task", 4096, NULL, 20, NULL, 1);
    
    // Playback task priority set to 15 to ensure smooth audio output.
    xTaskCreatePinnedToCore(audio_tx_task, "audio_tx_task", 4096, NULL, 15, NULL, 1);
}

RingbufHandle_t audio_hal_get_rx_ringbuf(void) { return rx_ringbuf; }
RingbufHandle_t audio_hal_get_tx_ringbuf(void) { return tx_ringbuf; }

void audio_hal_set_volume(uint8_t volume) { current_volume = (volume > 100) ? 100 : volume; }
void audio_hal_switch_channel(bool use_secondary) { gpio_set_level(MUX_CTRL_PIN, use_secondary ? 1 : 0); }