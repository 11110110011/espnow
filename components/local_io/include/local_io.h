#pragma once

#include <stdbool.h>
#include "esp_err.h"
#include "config_store.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Apply GPIO configuration from config_store and start input polling.
 */
esp_err_t local_io_init(void);

/** @brief Drive an output pin HIGH (true) or LOW (false). */
esp_err_t local_io_set_output(int pin, bool state);

/**
 * @brief Emit a pulse train on an output pin configured for pulse mode.
 *        Pulse count and period come from gpio_cfg_t in config_store.
 */
esp_err_t local_io_trigger_pulse(int pin);

/** @brief Read the debounced logical state of an input pin. */
bool      local_io_get_input(int pin);

/** @brief Reconfigure a single pin at runtime (takes effect immediately). */
esp_err_t local_io_reconfigure(int pin, const gpio_cfg_t *cfg);

#ifdef __cplusplus
}
#endif
