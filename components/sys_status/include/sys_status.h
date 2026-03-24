#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Start periodic system status reporting.
 *        Publishes JSON to "{base}/status" every CONFIG_SYS_STATUS_INTERVAL_S seconds.
 *        JSON fields: uptime_s, free_heap, ip, mqtt_connected, node_count.
 */
esp_err_t sys_status_init(void);

#ifdef __cplusplus
}
#endif
