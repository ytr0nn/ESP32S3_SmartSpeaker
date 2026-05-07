#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/ringbuf.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the Wake Word and VAD module.
 * Allocates the Record RingBuffer in PSRAM, initializes the ESP-SR Audio Frontend (AFE),
 * and starts the FreeRTOS DSP task pinned to Core 1.
 */
void wake_word_init(void);

/**
 * @brief Get the handle to the Record RingBuffer.
 * The ai_pipeline (STT) should consume raw PCM data from this buffer
 * after receiving the SYS_EVENT_WAKE_WORD_DETECTED event.
 * 
 * @return RingbufHandle_t Handle to the FreeRTOS RingBuffer
 */
RingbufHandle_t wake_word_get_record_ringbuf(void);

#ifdef __cplusplus
}
#endif