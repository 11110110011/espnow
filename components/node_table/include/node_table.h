#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "config_store.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t  mac[6];
    uint8_t  node_id;
    uint8_t  capabilities;  /* bit0=relay, bit1=button, bit2=latch_sw */
    bool     state;
    bool     online;
    uint32_t last_seen_ms;
} node_record_t;

esp_err_t      node_table_init(void);
esp_err_t      node_table_register(const uint8_t *mac, uint8_t node_id, uint8_t caps);
node_record_t *node_table_find_by_id(uint8_t node_id);
node_record_t *node_table_find_by_mac(const uint8_t *mac);
esp_err_t      node_table_update_state(uint8_t node_id, bool relay_state);
esp_err_t      node_table_set_online(uint8_t node_id, bool online);
int            node_table_count(void);

#ifdef __cplusplus
}
#endif
