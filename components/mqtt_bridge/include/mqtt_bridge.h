#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*mqtt_callback_t)(const char *topic, const char *payload, int payload_len);

/**
 * @brief Start the MQTT client using credentials from config_store.
 *        Must be called after eth_mgr is connected.
 */
esp_err_t mqtt_bridge_init(void);

/**
 * @brief Generic publish.
 * @param retain  Whether broker should retain the message.
 */
esp_err_t mqtt_bridge_publish(const char *topic, const char *payload,
                              int qos, bool retain);

/** @brief Subscribe to a topic; cb is invoked on every matching message. */
esp_err_t mqtt_bridge_subscribe(const char *topic, mqtt_callback_t cb);

/* ---- Convenience helpers ---- */

/** Publish "{base}/node/{node_id}/state" → "ON" or "OFF" (retained, QoS 1). */
esp_err_t mqtt_bridge_publish_node_state(uint8_t node_id, bool state);

/** Publish "{base}/node/{node_id}/availability" → "online" / "offline" (retained). */
esp_err_t mqtt_bridge_publish_node_avail(uint8_t node_id, bool online);

/** Publish "{base}/gpio/{pin}/state" → "ON" or "OFF" (retained, QoS 1). */
esp_err_t mqtt_bridge_publish_gpio_state(int pin, bool state);

/** @brief Return true when MQTT is connected to the broker. */
bool mqtt_bridge_is_connected(void);

/**
 * @brief Register a callback invoked on every successful MQTT connect
 *        (including reconnects).  Use this to re-publish discovery or
 *        initial state after the broker comes back.
 */
typedef void (*mqtt_connect_cb_t)(void);
esp_err_t mqtt_bridge_register_connect_cb(mqtt_connect_cb_t cb);

#ifdef __cplusplus
}
#endif
