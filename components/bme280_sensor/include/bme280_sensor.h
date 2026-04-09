#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialise I2C, configure BME280, and start the publish task.
 *        Publishes temperature and humidity to MQTT every 30 s.
 */
esp_err_t bme280_sensor_init(void);

#ifdef __cplusplus
}
#endif
