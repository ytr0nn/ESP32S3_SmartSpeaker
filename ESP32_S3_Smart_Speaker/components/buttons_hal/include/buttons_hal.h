#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the Buttons HAL module.
 * Configures GPIOs 1, 2, and 47 as inputs with interrupts.
 * Starts a FreeRTOS task to process button events (debounce and duration),
 * which then posts SYS_EVENT_BTN_* to the global event loop.
 */
void buttons_hal_init(void);

#ifdef __cplusplus
}
#endif