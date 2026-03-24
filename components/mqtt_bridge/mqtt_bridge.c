#include "mqtt_bridge.h"
#include "config_store.h"
#include "esp_log.h"
#include "esp_check.h"
#include "mqtt_client.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "mqtt_bridge";

#define MAX_SUBSCRIPTIONS  32
#define MAX_CONNECT_CBS     8

typedef struct {
    char            topic[128];
    mqtt_callback_t cb;
} subscription_t;

static esp_mqtt_client_handle_t s_client         = NULL;
static bool                     s_connected      = false;
static mqtt_config_t            s_cfg;
static subscription_t           s_subs[MAX_SUBSCRIPTIONS];
static int                      s_sub_count      = 0;
static mqtt_connect_cb_t        s_connect_cbs[MAX_CONNECT_CBS];
static int                      s_connect_cb_cnt = 0;

/* -----------------------------------------------------------------------
 * Event handler
 * --------------------------------------------------------------------- */

static void mqtt_event_handler(void *arg, esp_event_base_t base,
                                int32_t event_id, void *data)
{
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)data;

    switch (event->event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "Connected to broker");
        s_connected = true;
        /* Re-subscribe to all topics */
        for (int i = 0; i < s_sub_count; i++) {
            esp_mqtt_client_subscribe(s_client, s_subs[i].topic, 1);
        }
        /* Fire connect callbacks (e.g. HA discovery re-publish) */
        for (int i = 0; i < s_connect_cb_cnt; i++) {
            s_connect_cbs[i]();
        }
        break;

    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "Disconnected from broker");
        s_connected = false;
        break;

    case MQTT_EVENT_DATA:
        /* Dispatch to matching subscriber */
        for (int i = 0; i < s_sub_count; i++) {
            if (strncmp(s_subs[i].topic, event->topic, event->topic_len) == 0) {
                /* Null-terminate payload for convenience */
                char payload[256] = {0};
                int plen = event->data_len < (int)sizeof(payload) - 1
                           ? event->data_len : (int)sizeof(payload) - 1;
                memcpy(payload, event->data, plen);
                s_subs[i].cb(s_subs[i].topic, payload, event->data_len);
            }
        }
        break;

    case MQTT_EVENT_ERROR:
        ESP_LOGE(TAG, "MQTT error");
        break;

    default:
        break;
    }
}

/* -----------------------------------------------------------------------
 * Public API
 * --------------------------------------------------------------------- */

esp_err_t mqtt_bridge_init(void)
{
    config_store_get_mqtt(&s_cfg);

    ESP_LOGI(TAG, "Connecting to MQTT broker %s:%u", s_cfg.host, s_cfg.port);

    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.hostname  = s_cfg.host,
        .broker.address.port      = s_cfg.port,
        .broker.address.transport = MQTT_TRANSPORT_OVER_TCP,
        .credentials.username       = s_cfg.user[0] ? s_cfg.user : NULL,
        .credentials.authentication.password = s_cfg.pass[0] ? s_cfg.pass : NULL,
        .session.keepalive          = 60,
        .session.last_will.topic    = "espnow/status",
        .session.last_will.msg      = "{\"status\":\"offline\"}",
        .session.last_will.retain   = true,
        .session.last_will.qos      = 1,
    };

    s_client = esp_mqtt_client_init(&mqtt_cfg);
    ESP_RETURN_ON_FALSE(s_client, ESP_ERR_NO_MEM, TAG, "client init");

    esp_mqtt_client_register_event(s_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    return esp_mqtt_client_start(s_client);
}

esp_err_t mqtt_bridge_publish(const char *topic, const char *payload,
                               int qos, bool retain)
{
    if (!s_client) return ESP_ERR_INVALID_STATE;
    int msg_id = esp_mqtt_client_publish(s_client, topic, payload, 0, qos, retain ? 1 : 0);
    return msg_id < 0 ? ESP_FAIL : ESP_OK;
}

esp_err_t mqtt_bridge_subscribe(const char *topic, mqtt_callback_t cb)
{
    if (s_sub_count >= MAX_SUBSCRIPTIONS) return ESP_ERR_NO_MEM;
    strlcpy(s_subs[s_sub_count].topic, topic, sizeof(s_subs[0].topic));
    s_subs[s_sub_count].cb = cb;
    s_sub_count++;
    if (s_connected) {
        esp_mqtt_client_subscribe(s_client, topic, 1);
    }
    return ESP_OK;
}

esp_err_t mqtt_bridge_publish_node_state(uint8_t node_id, bool state)
{
    char topic[64];
    snprintf(topic, sizeof(topic), "%s/node/%u/state", s_cfg.topic, node_id);
    return mqtt_bridge_publish(topic, state ? "ON" : "OFF", 1, true);
}

esp_err_t mqtt_bridge_publish_node_avail(uint8_t node_id, bool online)
{
    char topic[64];
    snprintf(topic, sizeof(topic), "%s/node/%u/availability", s_cfg.topic, node_id);
    return mqtt_bridge_publish(topic, online ? "online" : "offline", 1, true);
}

esp_err_t mqtt_bridge_publish_gpio_state(int pin, bool state)
{
    char topic[64];
    snprintf(topic, sizeof(topic), "%s/gpio/%d/state", s_cfg.topic, pin);
    return mqtt_bridge_publish(topic, state ? "ON" : "OFF", 1, true);
}

bool mqtt_bridge_is_connected(void)
{
    return s_connected;
}

esp_err_t mqtt_bridge_register_connect_cb(mqtt_connect_cb_t cb)
{
    if (s_connect_cb_cnt >= MAX_CONNECT_CBS) return ESP_ERR_NO_MEM;
    s_connect_cbs[s_connect_cb_cnt++] = cb;
    return ESP_OK;
}
