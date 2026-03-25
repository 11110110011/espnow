#include "espnow_master.h"
#include "node_table.h"
#include "mqtt_bridge.h"
#include "config_store.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_now.h"
#include "esp_wifi.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "espnow_master";

#define KEEPALIVE_INTERVAL_US  (30 * 1000 * 1000)  /* 30 s */
#define PONG_TIMEOUT_MS        5000
#define MAX_MISSED_PONGS       3

static QueueHandle_t    s_recv_queue;
static esp_timer_handle_t s_keepalive_timer;
static uint8_t          s_seq = 0;
static uint8_t          s_missed_pings[CONFIG_STORE_MAX_NODES + 1]; /* indexed by node_id */

typedef struct {
    uint8_t      mac[6];
    espnow_msg_t msg;
} recv_event_t;

/* -----------------------------------------------------------------------
 * Helpers
 * --------------------------------------------------------------------- */

/* -----------------------------------------------------------------------
 * MQTT command subscription
 * --------------------------------------------------------------------- */

static void mqtt_cmd_cb(const char *topic, const char *payload, int len)
{
    /* topic format: {base}/node/{id}/command */
    const char *p = strrchr(topic, '/');          /* points to "/command" */
    if (!p) return;
    /* step back to find node id: scan left from the slash before "command" */
    const char *end = p;                          /* '/' before "command" */
    while (end > topic && *(end-1) != '/') end--;  /* end now points at id */
    uint8_t node_id = (uint8_t)atoi(end);
    if (node_id == 0) return;

    uint8_t action;
    if      (len >= 2 && strncmp(payload, "ON",     2) == 0) action = ESPNOW_ACTION_ON;
    else if (len >= 3 && strncmp(payload, "OFF",    3) == 0) action = ESPNOW_ACTION_OFF;
    else if (len >= 6 && strncmp(payload, "TOGGLE", 6) == 0) action = ESPNOW_ACTION_TOGGLE;
    else { ESP_LOGW(TAG, "Unknown cmd payload: %.*s", len, payload); return; }

    ESP_LOGI(TAG, "MQTT CMD node %d action %d", node_id, action);
    espnow_master_send_cmd(node_id, action);
}

static void subscribe_node_cmd(uint8_t node_id)
{
    mqtt_config_t mcfg;
    config_store_get_mqtt(&mcfg);
    char topic[80];
    snprintf(topic, sizeof(topic), "%s/node/%u/command", mcfg.topic, node_id);
    mqtt_bridge_subscribe(topic, mqtt_cmd_cb);
}

static void send_msg(const uint8_t *mac, espnow_msg_t *msg)
{
    msg->seq = s_seq++;
    esp_now_send(mac, (const uint8_t *)msg, sizeof(espnow_msg_t));
}

/* -----------------------------------------------------------------------
 * Receive callback (called from Wi-Fi task — post to queue)
 * --------------------------------------------------------------------- */

static void recv_cb(const esp_now_recv_info_t *info, const uint8_t *data, int len)
{
    if (len < (int)sizeof(espnow_msg_t)) return;
    recv_event_t evt;
    memcpy(evt.mac, info->src_addr, 6);
    memcpy(&evt.msg, data, sizeof(espnow_msg_t));
    xQueueSendFromISR(s_recv_queue, &evt, NULL);
}

/* -----------------------------------------------------------------------
 * Receive processing task
 * --------------------------------------------------------------------- */

static void espnow_recv_task(void *arg)
{
    recv_event_t evt;
    for (;;) {
        if (xQueueReceive(s_recv_queue, &evt, portMAX_DELAY) != pdTRUE) continue;

        espnow_msg_t *msg = &evt.msg;

        switch (msg->msg_type) {
        case ESPNOW_MSG_REGISTER: {
            uint8_t  node_id = msg->payload[0];
            uint8_t  caps    = msg->payload[1];
            ESP_LOGI(TAG, "REGISTER from node %d", node_id);

            node_table_register(evt.mac, node_id, caps);

            /* Add as ESP-NOW peer if not already registered */
            if (!esp_now_is_peer_exist(evt.mac)) {
                esp_now_peer_info_t peer = {
                    .channel = 0,
                    .encrypt = false,
                };
                memcpy(peer.peer_addr, evt.mac, 6);
                esp_now_add_peer(&peer);
            }

            /* Send ACK */
            espnow_msg_t ack = {
                .msg_type = ESPNOW_MSG_ACK,
                .node_id  = node_id,
            };
            send_msg(evt.mac, &ack);

            /* Publish availability */
            mqtt_bridge_publish_node_avail(node_id, true);
            subscribe_node_cmd(node_id);
            if (node_id <= CONFIG_STORE_MAX_NODES) s_missed_pings[node_id] = 0;
            break;
        }

        case ESPNOW_MSG_STATE_REPORT: {
            uint8_t relay_state = msg->payload[0];
            ESP_LOGI(TAG, "STATE from node %d: %s", msg->node_id, relay_state ? "ON" : "OFF");
            node_table_update_state(msg->node_id, (bool)relay_state);
            mqtt_bridge_publish_node_state(msg->node_id, (bool)relay_state);
            if (msg->node_id <= CONFIG_STORE_MAX_NODES)
                s_missed_pings[msg->node_id] = 0;
            break;
        }

        case ESPNOW_MSG_PONG: {
            ESP_LOGD(TAG, "PONG from node %d", msg->node_id);
            node_table_set_online(msg->node_id, true);
            if (msg->node_id <= CONFIG_STORE_MAX_NODES)
                s_missed_pings[msg->node_id] = 0;
            break;
        }

        default:
            ESP_LOGW(TAG, "Unknown msg type 0x%02x", msg->msg_type);
            break;
        }
    }
}

/* -----------------------------------------------------------------------
 * Keepalive timer
 * --------------------------------------------------------------------- */

static void keepalive_cb(void *arg)
{
    int count = node_table_count();
    for (uint8_t id = 1; id <= (uint8_t)count; id++) {
        node_record_t *rec = node_table_find_by_id(id);
        if (!rec || !rec->online) continue;

        /* Check if previous PING went unanswered */
        if (id <= CONFIG_STORE_MAX_NODES && s_missed_pings[id] >= MAX_MISSED_PONGS) {
            ESP_LOGW(TAG, "Node %d offline — %d missed PINGs", id, s_missed_pings[id]);
            node_table_set_online(id, false);
            mqtt_bridge_publish_node_avail(id, false);
            s_missed_pings[id] = 0;
            continue;
        }

        espnow_msg_t ping = { .msg_type = ESPNOW_MSG_PING, .node_id = id };
        send_msg(rec->mac, &ping);
        if (id <= CONFIG_STORE_MAX_NODES) s_missed_pings[id]++;
    }
}

/* -----------------------------------------------------------------------
 * Public API
 * --------------------------------------------------------------------- */

esp_err_t espnow_master_init(void)
{
    /* ESP-NOW requires Wi-Fi to be initialised (no AP/STA connection needed) */
    wifi_init_config_t wifi_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&wifi_cfg), TAG, "wifi init");
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "wifi mode");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "wifi start");
    ESP_RETURN_ON_ERROR(esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE), TAG, "wifi channel");

    ESP_RETURN_ON_ERROR(esp_now_init(), TAG, "esp_now init");
    ESP_RETURN_ON_ERROR(esp_now_set_pmk((uint8_t *)"pmk1234567890123"), TAG, "pmk");

    s_recv_queue = xQueueCreate(16, sizeof(recv_event_t));
    ESP_RETURN_ON_FALSE(s_recv_queue, ESP_ERR_NO_MEM, TAG, "recv queue");

    ESP_RETURN_ON_ERROR(
        esp_now_register_recv_cb(recv_cb), TAG, "recv cb");

    xTaskCreate(espnow_recv_task, "espnow_recv", 4096, NULL, 5, NULL);

    /* Start keepalive timer */
    esp_timer_create_args_t timer_args = {
        .callback = keepalive_cb,
        .name     = "espnow_ka",
    };
    ESP_RETURN_ON_ERROR(
        esp_timer_create(&timer_args, &s_keepalive_timer), TAG, "timer");
    ESP_RETURN_ON_ERROR(
        esp_timer_start_periodic(s_keepalive_timer, KEEPALIVE_INTERVAL_US), TAG, "timer start");

    /* For nodes already in NVS: add as ESP-NOW peers and subscribe to commands */
    int count = node_table_count();
    for (uint8_t id = 1; id <= (uint8_t)count; id++) {
        node_record_t *rec = node_table_find_by_id(id);
        if (!rec) continue;
        if (!esp_now_is_peer_exist(rec->mac)) {
            esp_now_peer_info_t peer = { .channel = 0, .encrypt = false };
            memcpy(peer.peer_addr, rec->mac, 6);
            esp_now_add_peer(&peer);
            ESP_LOGI(TAG, "Added peer node %d from NVS", id);
        }
        subscribe_node_cmd(rec->node_id);
    }

    ESP_LOGI(TAG, "ESP-NOW master initialised");
    return ESP_OK;
}

esp_err_t espnow_master_send_cmd(uint8_t node_id, uint8_t action)
{
    node_record_t *rec = node_table_find_by_id(node_id);
    if (!rec) {
        ESP_LOGE(TAG, "Node %d not found", node_id);
        return ESP_ERR_NOT_FOUND;
    }
    espnow_msg_t msg = {
        .msg_type    = ESPNOW_MSG_CMD,
        .node_id     = node_id,
        .payload[0]  = action,
    };
    send_msg(rec->mac, &msg);
    return ESP_OK;
}

esp_err_t espnow_master_ping_all(void)
{
    int count = node_table_count();
    for (uint8_t id = 1; id <= (uint8_t)count; id++) {
        node_record_t *rec = node_table_find_by_id(id);
        if (!rec || !rec->online) continue;
        espnow_msg_t ping = {
            .msg_type = ESPNOW_MSG_PING,
            .node_id  = id,
        };
        send_msg(rec->mac, &ping);
    }
    return ESP_OK;
}
