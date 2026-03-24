#pragma once

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Publish Home Assistant MQTT discovery messages for all known nodes
 *        and enabled local GPIOs.  Safe to call multiple times (idempotent).
 */
esp_err_t ha_discovery_publish_all(void);

/** @brief Publish discovery for a single node (called when a new node registers). */
esp_err_t ha_discovery_publish_node(uint8_t node_id);

/** @brief Publish discovery for a single GPIO pin. */
esp_err_t ha_discovery_publish_gpio(int pin);

#ifdef __cplusplus
}
#endif
