#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/ringbuf.h"

#ifdef __cplusplus
extern "C" {
#endif

// API for Audio HAL
void audio_hal_init(void);

// Returns the RX ringbuffer handle for feeding audio data to the wake word engine
RingbufHandle_t audio_hal_get_rx_ringbuf(void);

// Returns the TX ringbuffer handle for sending audio data from the AI pipeline to the DAC
RingbufHandle_t audio_hal_get_tx_ringbuf(void);
// Sets the output volume (0-100%)
void audio_hal_set_volume(uint8_t volume);

// Switches the output channel between primary and secondary (if applicable)
void audio_hal_switch_channel(bool use_secondary);

#ifdef __cplusplus
}
#endif