#include "sys_status.h"
#include "config_store.h"
#include "mqtt_bridge.h"
#include "node_table.h"
#include "eth_mgr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>

/* Reboot if status task stalls for longer than this */
#define GATEWAY_WDT_TIMEOUT_MS  120000

static const char *TAG = "sys_status";

#ifndef CONFIG_SYS_STATUS_INTERVAL_S
#define CONFIG_SYS_STATUS_INTERVAL_S  60
#endif

static mqtt_config_t s_mqtt_cfg;

static void status_task(void *arg)
{
    config_store_get_mqtt(&s_mqtt_cfg);

    char topic[64];
    snprintf(topic, sizeof(topic), "%s/status", s_mqtt_cfg.topic);

    esp_task_wdt_add(NULL);  /* subscribe this task to the TWDT */

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(CONFIG_SYS_STATUS_INTERVAL_S * 1000));
        esp_task_wdt_reset();

        uint32_t uptime_s = (uint32_t)(esp_timer_get_time() / 1000000ULL);
        uint32_t free_heap = heap_caps_get_free_size(MALLOC_CAP_DEFAULT);
        char ip[16] = "unknown";
        eth_mgr_get_ip(ip, sizeof(ip));

        char payload[256];
        snprintf(payload, sizeof(payload),
            "{\"status\":\"online\",\"uptime_s\":%lu,\"free_heap\":%lu,"
            "\"ip\":\"%s\",\"mqtt_connected\":%s,\"node_count\":%d}",
            (unsigned long)uptime_s,
            (unsigned long)free_heap,
            ip,
            mqtt_bridge_is_connected() ? "true" : "false",
            node_table_count());

        mqtt_bridge_publish(topic, payload, 0, false);
        ESP_LOGD(TAG, "Status: %s", payload);
    }
}

esp_err_t sys_status_init(void)
{
    esp_task_wdt_config_t wdt_cfg = {
        .timeout_ms     = GATEWAY_WDT_TIMEOUT_MS,
        .idle_core_mask = 0,
        .trigger_panic  = true,
    };
    /* Reconfigure if already initialised by sdkconfig, otherwise init fresh */
    if (esp_task_wdt_reconfigure(&wdt_cfg) == ESP_ERR_INVALID_STATE) {
        esp_task_wdt_init(&wdt_cfg);
    }

    xTaskCreate(status_task, "sys_status", 3072, NULL, 3, NULL);
    ESP_LOGI(TAG, "System status + watchdog started (interval: %ds, wdt: %ds)",
             CONFIG_SYS_STATUS_INTERVAL_S, GATEWAY_WDT_TIMEOUT_MS / 1000);
    return ESP_OK;
}
