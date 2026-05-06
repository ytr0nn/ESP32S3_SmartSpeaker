#ifndef MAIN_H
#define MAIN_H

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/ringbuf.h" 

// --- I2S ---
#define MIC_I2S_PORT    I2S_NUM_0
#define MIC_BCLK_PIN    4
#define MIC_WS_PIN      5
#define MIC_DATA_PIN    6

#define DAC_I2S_PORT    I2S_NUM_1
#define DAC_BCLK_PIN    15
#define DAC_WS_PIN      16
#define DAC_DATA_PIN    17
#define DAC_XSMT_PIN    14

#define AUDIO_SAMPLE_RATE 16000

// 32kb audio ring buffer size
#define RINGBUF_SIZE (32 * 1024)

// --- CD4053BE ---
#define AMP_SWITCH_PIN  7

// --- SPI GC9A01) ---
#define DISP_SPI_HOST   SPI2_HOST
#define DISP_SCL_PIN    12
#define DISP_SDA_PIN    11
#define DISP_CS_PIN     10
#define DISP_DC_PIN     9
#define DISP_RST_PIN    8

// sensor buttons TOUCH
#define TOUCH_BTN_1_PIN 1
#define TOUCH_BTN_2_PIN 2
#define TOUCH_BTN_3_PIN 47

// --- LED light ---
#define LED_STRIP_PIN   48
#define LED_STRIP_COUNT 16 // led count in the ring

// --- Глобальные объекты FreeRTOS ---
extern RingbufHandle_t audio_rx_ringbuf; // ring buffer 

// extern QueueHandle_t audio_out_queue;
// extern SemaphoreHandle_t wake_word_sem;

#endif // MAIN_H