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
 *        count is clamped to 1–5.
 */
esp_err_t local_io_trigger_pulse(int pin, int count);

/** @brief Read the debounced logical state of an input pin. */
bool      local_io_get_input(int pin);

/** @brief Reconfigure a single pin at runtime (takes effect immediately). */
esp_err_t local_io_reconfigure(int pin, const gpio_cfg_t *cfg);

/** @brief Returns true for pins reserved by hardware (e.g. W5500 RST). */
bool local_io_pin_is_reserved(int pin);

/** @brief Returns true for input-only pins (no pull, cannot be output). */
bool local_io_pin_is_input_only(int pin);

#ifdef __cplusplus
}
#endif
