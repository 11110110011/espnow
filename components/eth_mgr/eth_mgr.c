#include "eth_mgr.h"
#include "config_store.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_eth.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "lwip/inet.h"
#include <string.h>

static const char *TAG = "eth_mgr";

#define ETH_CONNECTED_BIT  BIT0

static EventGroupHandle_t  s_eth_event_group;
static esp_netif_t        *s_netif = NULL;
static bool                s_connected = false;
static char                s_ip_str[16] = {0};

/* -----------------------------------------------------------------------
 * Event handlers
 * --------------------------------------------------------------------- */

static void eth_event_handler(void *arg, esp_event_base_t base,
                               int32_t event_id, void *data)
{
    switch (event_id) {
    case ETHERNET_EVENT_CONNECTED:
        ESP_LOGI(TAG, "Ethernet link up");
        break;
    case ETHERNET_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "Ethernet link down");
        s_connected = false;
        break;
    case ETHERNET_EVENT_START:
        ESP_LOGI(TAG, "Ethernet started");
        break;
    default:
        break;
    }
}

static void ip_event_handler(void *arg, esp_event_base_t base,
                              int32_t event_id, void *data)
{
    if (event_id == IP_EVENT_ETH_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)data;
        snprintf(s_ip_str, sizeof(s_ip_str), IPSTR, IP2STR(&event->ip_info.ip));
        ESP_LOGI(TAG, "Got IP: %s", s_ip_str);
        s_connected = true;
        xEventGroupSetBits(s_eth_event_group, ETH_CONNECTED_BIT);
    }
}

/* -----------------------------------------------------------------------
 * Public API
 * --------------------------------------------------------------------- */

esp_err_t eth_mgr_init(void)
{
    s_eth_event_group = xEventGroupCreate();

    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "netif init");
    ESP_RETURN_ON_ERROR(esp_event_loop_create_default(), TAG, "event loop");

    /* Register event handlers */
    ESP_RETURN_ON_ERROR(
        esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID, eth_event_handler, NULL),
        TAG, "eth event register");
    ESP_RETURN_ON_ERROR(
        esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, ip_event_handler, NULL),
        TAG, "ip event register");

    /* Create netif for W5500 */
    esp_netif_config_t netif_cfg = ESP_NETIF_DEFAULT_ETH();
    s_netif = esp_netif_new(&netif_cfg);
    esp_netif_set_hostname(s_netif, "espnow");

    /* Required for W5500 interrupt GPIO handler */
    gpio_install_isr_service(0);

    /* SPI bus init */
    spi_bus_config_t buscfg = {
        .miso_io_num   = ETH_W5500_MISO_GPIO,
        .mosi_io_num   = ETH_W5500_MOSI_GPIO,
        .sclk_io_num   = ETH_W5500_SCLK_GPIO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
    };
    ESP_RETURN_ON_ERROR(
        spi_bus_initialize(ETH_W5500_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO),
        TAG, "SPI bus init");

    /* W5500 SPI device */
    spi_device_interface_config_t devcfg = {
        .command_bits     = 16,
        .address_bits     = 8,
        .mode             = 0,
        .clock_speed_hz   = 20 * 1000 * 1000,  /* 20 MHz */
        .spics_io_num     = ETH_W5500_CS_GPIO,
        .queue_size       = 20,
    };

    eth_w5500_config_t w5500_cfg = ETH_W5500_DEFAULT_CONFIG(ETH_W5500_SPI_HOST, &devcfg);
    w5500_cfg.int_gpio_num = ETH_W5500_INT_GPIO;

    eth_mac_config_t mac_cfg = ETH_MAC_DEFAULT_CONFIG();
    eth_phy_config_t phy_cfg = ETH_PHY_DEFAULT_CONFIG();
    phy_cfg.reset_gpio_num = ETH_W5500_RST_GPIO;

    esp_eth_mac_t *mac = esp_eth_mac_new_w5500(&w5500_cfg, &mac_cfg);
    esp_eth_phy_t *phy = esp_eth_phy_new_w5500(&phy_cfg);

    esp_eth_config_t eth_cfg = ETH_DEFAULT_CONFIG(mac, phy);
    esp_eth_handle_t eth_handle = NULL;
    ESP_RETURN_ON_ERROR(esp_eth_driver_install(&eth_cfg, &eth_handle), TAG, "eth driver");

    /* Attach netif to ethernet driver */
    esp_eth_netif_glue_handle_t glue = esp_eth_new_netif_glue(eth_handle);
    esp_netif_attach(s_netif, glue);

    /* Apply network config */
    net_config_t net;
    config_store_get_net(&net);
    if (!net.dhcp) {
        esp_netif_ip_info_t ip_info;
        ip_info.ip.addr      = inet_addr(net.ip);
        ip_info.netmask.addr = inet_addr(net.mask);
        ip_info.gw.addr      = inet_addr(net.gw);
        esp_netif_dhcpc_stop(s_netif);
        esp_netif_set_ip_info(s_netif, &ip_info);
        ESP_LOGI(TAG, "Static IP configured: %s (waiting for link)", net.ip);
        /* Do NOT set ETH_CONNECTED_BIT here — wait for physical link + IP event */
    }

    ESP_RETURN_ON_ERROR(esp_eth_start(eth_handle), TAG, "eth start");

    /* Wait for IP (30 s timeout) */
    EventBits_t bits = xEventGroupWaitBits(s_eth_event_group, ETH_CONNECTED_BIT,
                                            pdFALSE, pdTRUE, pdMS_TO_TICKS(30000));
    if (!(bits & ETH_CONNECTED_BIT)) {
        ESP_LOGE(TAG, "Ethernet: timed out waiting for IP");
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

bool eth_mgr_is_connected(void)
{
    return s_connected;
}

esp_err_t eth_mgr_get_ip(char *buf, size_t len)
{
    if (!s_connected) return ESP_ERR_INVALID_STATE;
    strlcpy(buf, s_ip_str, len);
    return ESP_OK;
}
