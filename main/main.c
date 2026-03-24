#include "config_store.h"
#include "eth_mgr.h"
#include "mqtt_bridge.h"
#include "espnow_master.h"
#include "node_table.h"
#include "local_io.h"
#include "ha_discovery.h"
#include "web_cfg.h"
#include "sys_status.h"
#include "esp_log.h"
#include "nvs_flash.h"

static const char *TAG = "main";

void app_main(void)
{
    ESP_LOGI(TAG, "smart-home-espnow gateway starting");

    /* 1. Configuration storage */
    ESP_ERROR_CHECK(config_store_init());

    /* 2. Ethernet — blocks until IP is obtained */
    ESP_ERROR_CHECK(eth_mgr_init());

    /* 3. MQTT client */
    ESP_ERROR_CHECK(mqtt_bridge_init());

    /* 4. Local GPIO */
    ESP_ERROR_CHECK(local_io_init());

    /* 5. Node registry (loads known nodes from NVS) */
    ESP_ERROR_CHECK(node_table_init());

    /* 6. ESP-NOW master */
    ESP_ERROR_CHECK(espnow_master_init());

    /* 7. Home Assistant discovery — re-published on every MQTT connect */
    ESP_ERROR_CHECK(ha_discovery_init());

    /* 8. Web configuration UI */
    ESP_ERROR_CHECK(web_cfg_start());

    /* 9. Periodic system status */
    ESP_ERROR_CHECK(sys_status_init());

    ESP_LOGI(TAG, "Gateway ready");
}
