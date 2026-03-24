#include "ha_discovery.h"
#include "config_store.h"
#include "node_table.h"
#include "mqtt_bridge.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "ha_discovery";

static mqtt_config_t s_mqtt_cfg;

static void load_cfg(void)
{
    config_store_get_mqtt(&s_mqtt_cfg);
}

/* -----------------------------------------------------------------------
 * Node switch entity
 * --------------------------------------------------------------------- */

esp_err_t ha_discovery_publish_node(uint8_t node_id)
{
    load_cfg();
    const char *base = s_mqtt_cfg.topic;

    char topic[128];
    snprintf(topic, sizeof(topic),
             "homeassistant/switch/espnow_node_%u/config", node_id);

    char payload[512];
    snprintf(payload, sizeof(payload),
        "{"
        "\"name\":\"Switch %u\","
        "\"unique_id\":\"espnow_node_%u\","
        "\"state_topic\":\"%s/node/%u/state\","
        "\"command_topic\":\"%s/node/%u/command\","
        "\"availability_topic\":\"%s/node/%u/availability\","
        "\"payload_on\":\"ON\","
        "\"payload_off\":\"OFF\","
        "\"retain\":true,"
        "\"device\":{"
            "\"identifiers\":[\"espnow_node_%u\"],"
            "\"name\":\"ESP-NOW Node %u\","
            "\"manufacturer\":\"smart-home-espnow\""
        "}"
        "}",
        node_id, node_id,
        base, node_id,
        base, node_id,
        base, node_id,
        node_id, node_id);

    ESP_LOGD(TAG, "Publishing HA discovery for node %u", node_id);
    return mqtt_bridge_publish(topic, payload, 1, true);
}

/* -----------------------------------------------------------------------
 * GPIO entities
 * --------------------------------------------------------------------- */

esp_err_t ha_discovery_publish_gpio(int pin)
{
    load_cfg();
    const char *base = s_mqtt_cfg.topic;

    gpio_cfg_t cfg;
    config_store_get_gpio(pin, &cfg);
    if (cfg.mode == CFG_GPIO_MODE_DISABLED) return ESP_OK;

    char topic[128];
    char payload[512];

    if (cfg.mode == CFG_GPIO_MODE_OUTPUT) {
        snprintf(topic, sizeof(topic),
                 "homeassistant/switch/espnow_gpio_%d/config", pin);
        snprintf(payload, sizeof(payload),
            "{"
            "\"name\":\"Gateway GPIO %d\","
            "\"unique_id\":\"espnow_gpio_%d\","
            "\"state_topic\":\"%s/gpio/%d/state\","
            "\"command_topic\":\"%s/gpio/%d/command\","
            "\"payload_on\":\"ON\","
            "\"payload_off\":\"OFF\","
            "\"retain\":true"
            "}",
            pin, pin,
            base, pin,
            base, pin);
    } else {
        /* input → binary_sensor */
        snprintf(topic, sizeof(topic),
                 "homeassistant/binary_sensor/espnow_gpio_%d/config", pin);
        snprintf(payload, sizeof(payload),
            "{"
            "\"name\":\"Gateway GPIO %d\","
            "\"unique_id\":\"espnow_gpio_%d\","
            "\"state_topic\":\"%s/gpio/%d/state\","
            "\"payload_on\":\"ON\","
            "\"payload_off\":\"OFF\""
            "}",
            pin, pin,
            base, pin);
    }

    ESP_LOGD(TAG, "Publishing HA discovery for GPIO %d", pin);
    return mqtt_bridge_publish(topic, payload, 1, true);
}

/* -----------------------------------------------------------------------
 * Publish all
 * --------------------------------------------------------------------- */

esp_err_t ha_discovery_publish_all(void)
{
    int count = node_table_count();
    for (int i = 1; i <= count; i++) {
        node_record_t *rec = node_table_find_by_id((uint8_t)i);
        if (rec) ha_discovery_publish_node(rec->node_id);
    }
    for (int pin = 0; pin < CONFIG_STORE_GPIO_COUNT; pin++) {
        ha_discovery_publish_gpio(pin);
    }
    ESP_LOGI(TAG, "HA discovery published");
    return ESP_OK;
}

/* -----------------------------------------------------------------------
 * Init — register with mqtt_bridge so discovery fires on every connect
 * --------------------------------------------------------------------- */

static void on_mqtt_connect(void)
{
    ha_discovery_publish_all();
}

esp_err_t ha_discovery_init(void)
{
    return mqtt_bridge_register_connect_cb(on_mqtt_connect);
}
