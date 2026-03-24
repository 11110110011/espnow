#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Start the HTTP configuration server on port 80.
 *        Routes:
 *          GET  /          — dashboard
 *          GET  /config    — configuration form
 *          POST /config    — save configuration
 *          GET  /nodes     — node registry (read-only)
 *          GET  /gpio      — local GPIO status
 *          POST /restart   — trigger system restart
 */
esp_err_t web_cfg_start(void);

/** @brief Stop the HTTP server. */
esp_err_t web_cfg_stop(void);

#ifdef __cplusplus
}
#endif
