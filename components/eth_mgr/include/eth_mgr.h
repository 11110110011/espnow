#pragma once

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* W5500 SPI pin defaults — override via sdkconfig / Kconfig */
#ifndef ETH_W5500_SPI_HOST
#define ETH_W5500_SPI_HOST    SPI2_HOST
#endif
#ifndef ETH_W5500_MISO_GPIO
#define ETH_W5500_MISO_GPIO   19
#endif
#ifndef ETH_W5500_MOSI_GPIO
#define ETH_W5500_MOSI_GPIO   23
#endif
#ifndef ETH_W5500_SCLK_GPIO
#define ETH_W5500_SCLK_GPIO   18
#endif
#ifndef ETH_W5500_CS_GPIO
#define ETH_W5500_CS_GPIO     5
#endif
#ifndef ETH_W5500_RST_GPIO
#define ETH_W5500_RST_GPIO    14
#endif

/**
 * @brief Initialise the W5500 Ethernet interface.
 *        Applies net_config from config_store (DHCP or static).
 *        Blocks until an IP address is obtained (timeout ~30 s).
 */
esp_err_t eth_mgr_init(void);

/** @brief Return true when the interface has a valid IP address. */
bool      eth_mgr_is_connected(void);

/** @brief Copy current IP string into buf (at least 16 bytes). */
esp_err_t eth_mgr_get_ip(char *buf, size_t len);

#ifdef __cplusplus
}
#endif
