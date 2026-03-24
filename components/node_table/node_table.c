#include "node_table.h"
#include "config_store.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_timer.h"
#include <string.h>

static const char *TAG = "node_table";

static node_record_t s_nodes[CONFIG_STORE_MAX_NODES];
static int           s_count = 0;

esp_err_t node_table_init(void)
{
    memset(s_nodes, 0, sizeof(s_nodes));

    node_entry_t entries[CONFIG_STORE_MAX_NODES];
    int count = 0;
    ESP_RETURN_ON_ERROR(config_store_get_nodes(entries, &count), TAG, "load nodes");

    for (int i = 0; i < count; i++) {
        memcpy(s_nodes[i].mac, entries[i].mac, 6);
        s_nodes[i].node_id = entries[i].node_id;
        s_nodes[i].online  = false;
    }
    s_count = count;
    ESP_LOGI(TAG, "Loaded %d nodes from NVS", s_count);
    return ESP_OK;
}

esp_err_t node_table_register(const uint8_t *mac, uint8_t node_id, uint8_t caps)
{
    /* Update existing record if MAC already known */
    for (int i = 0; i < s_count; i++) {
        if (memcmp(s_nodes[i].mac, mac, 6) == 0) {
            s_nodes[i].node_id      = node_id;
            s_nodes[i].capabilities = caps;
            s_nodes[i].online       = true;
            s_nodes[i].last_seen_ms = (uint32_t)(esp_timer_get_time() / 1000);
            ESP_LOGI(TAG, "Node %d re-registered", node_id);
            return ESP_OK;
        }
    }

    if (s_count >= CONFIG_STORE_MAX_NODES) {
        ESP_LOGE(TAG, "Node table full");
        return ESP_ERR_NO_MEM;
    }

    memcpy(s_nodes[s_count].mac, mac, 6);
    s_nodes[s_count].node_id      = node_id;
    s_nodes[s_count].capabilities = caps;
    s_nodes[s_count].state        = false;
    s_nodes[s_count].online       = true;
    s_nodes[s_count].last_seen_ms = (uint32_t)(esp_timer_get_time() / 1000);
    s_count++;

    ESP_LOGI(TAG, "New node %d registered (total: %d)", node_id, s_count);

    /* Persist to NVS */
    node_entry_t entries[CONFIG_STORE_MAX_NODES];
    for (int i = 0; i < s_count; i++) {
        memcpy(entries[i].mac, s_nodes[i].mac, 6);
        entries[i].node_id = s_nodes[i].node_id;
    }
    return config_store_save_nodes(entries, s_count);
}

node_record_t *node_table_find_by_id(uint8_t node_id)
{
    for (int i = 0; i < s_count; i++) {
        if (s_nodes[i].node_id == node_id) return &s_nodes[i];
    }
    return NULL;
}

node_record_t *node_table_find_by_mac(const uint8_t *mac)
{
    for (int i = 0; i < s_count; i++) {
        if (memcmp(s_nodes[i].mac, mac, 6) == 0) return &s_nodes[i];
    }
    return NULL;
}

esp_err_t node_table_update_state(uint8_t node_id, bool relay_state)
{
    node_record_t *rec = node_table_find_by_id(node_id);
    if (!rec) return ESP_ERR_NOT_FOUND;
    rec->state        = relay_state;
    rec->last_seen_ms = (uint32_t)(esp_timer_get_time() / 1000);
    return ESP_OK;
}

esp_err_t node_table_set_online(uint8_t node_id, bool online)
{
    node_record_t *rec = node_table_find_by_id(node_id);
    if (!rec) return ESP_ERR_NOT_FOUND;
    rec->online = online;
    if (online) rec->last_seen_ms = (uint32_t)(esp_timer_get_time() / 1000);
    return ESP_OK;
}

int node_table_count(void)
{
    return s_count;
}
