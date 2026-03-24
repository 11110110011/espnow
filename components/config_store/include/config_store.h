#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* -----------------------------------------------------------------------
 * Configuration types
 * --------------------------------------------------------------------- */

typedef struct {
    bool   dhcp;
    char   ip[16];
    char   mask[16];
    char   gw[16];
    char   dns[16];
} net_config_t;

typedef struct {
    char     host[64];
    uint16_t port;
    char     user[32];
    char     pass[64];
    char     topic[32];
} mqtt_config_t;

#define CFG_GPIO_MODE_DISABLED  0
#define CFG_GPIO_MODE_INPUT     1
#define CFG_GPIO_MODE_OUTPUT    2

typedef struct {
    uint8_t mode;           /* CFG_GPIO_MODE_* */
    bool    pull_up;
    bool    invert;
    bool    pulse_mode;
    uint8_t pulse_count;    /* 1–5 */
} gpio_cfg_t;

typedef struct {
    uint8_t mac[6];
    uint8_t node_id;
} node_entry_t;

#define CONFIG_STORE_MAX_NODES  12
#define CONFIG_STORE_GPIO_COUNT  8

/* -----------------------------------------------------------------------
 * API
 * --------------------------------------------------------------------- */

esp_err_t config_store_init(void);

esp_err_t config_store_get_net(net_config_t *out);
esp_err_t config_store_set_net(const net_config_t *cfg);

esp_err_t config_store_get_mqtt(mqtt_config_t *out);
esp_err_t config_store_set_mqtt(const mqtt_config_t *cfg);

esp_err_t config_store_get_gpio(int pin, gpio_cfg_t *out);
esp_err_t config_store_set_gpio(int pin, const gpio_cfg_t *cfg);

esp_err_t config_store_get_nodes(node_entry_t *entries, int *count);
esp_err_t config_store_save_nodes(const node_entry_t *entries, int count);

#ifdef __cplusplus
}
#endif
