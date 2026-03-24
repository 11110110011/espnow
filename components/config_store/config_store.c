#include "config_store.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "config_store";

/* -----------------------------------------------------------------------
 * Helpers
 * --------------------------------------------------------------------- */

static esp_err_t open_nvs(const char *ns, nvs_open_mode_t mode, nvs_handle_t *h)
{
    return nvs_open(ns, mode, h);
}

/* -----------------------------------------------------------------------
 * Public API
 * --------------------------------------------------------------------- */

esp_err_t config_store_init(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition wiped and re-initialised");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_LOGI(TAG, "NVS initialised");
    return ret;
}

/* --- Network ---------------------------------------------------------- */

esp_err_t config_store_get_net(net_config_t *out)
{
    nvs_handle_t h;
    esp_err_t ret = open_nvs("net", NVS_READONLY, &h);
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        /* Return defaults */
        memset(out, 0, sizeof(*out));
        out->dhcp = true;
        return ESP_OK;
    }
    if (ret != ESP_OK) return ret;

    uint8_t dhcp = 1;
    nvs_get_u8(h, "dhcp", &dhcp);
    out->dhcp = (bool)dhcp;

    size_t len = sizeof(out->ip);
    if (nvs_get_str(h, "ip",   out->ip,   &len) != ESP_OK) strcpy(out->ip,   "192.168.1.100");
    len = sizeof(out->mask);
    if (nvs_get_str(h, "mask", out->mask, &len) != ESP_OK) strcpy(out->mask, "255.255.255.0");
    len = sizeof(out->gw);
    if (nvs_get_str(h, "gw",   out->gw,   &len) != ESP_OK) strcpy(out->gw,   "192.168.1.1");
    len = sizeof(out->dns);
    if (nvs_get_str(h, "dns",  out->dns,  &len) != ESP_OK) strcpy(out->dns,  "8.8.8.8");

    nvs_close(h);
    return ESP_OK;
}

esp_err_t config_store_set_net(const net_config_t *cfg)
{
    nvs_handle_t h;
    ESP_RETURN_ON_ERROR(open_nvs("net", NVS_READWRITE, &h), TAG, "open nvs");
    nvs_set_u8(h,  "dhcp", (uint8_t)cfg->dhcp);
    nvs_set_str(h, "ip",   cfg->ip);
    nvs_set_str(h, "mask", cfg->mask);
    nvs_set_str(h, "gw",   cfg->gw);
    nvs_set_str(h, "dns",  cfg->dns);
    esp_err_t ret = nvs_commit(h);
    nvs_close(h);
    return ret;
}

/* --- MQTT ------------------------------------------------------------- */

esp_err_t config_store_get_mqtt(mqtt_config_t *out)
{
    nvs_handle_t h;
    esp_err_t ret = open_nvs("mqtt", NVS_READONLY, &h);
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        memset(out, 0, sizeof(*out));
        strcpy(out->host,  "192.168.1.1");
        out->port = 1883;
        strcpy(out->topic, "espnow");
        return ESP_OK;
    }
    if (ret != ESP_OK) return ret;

    size_t len;
    uint16_t port = 1883;

    len = sizeof(out->host);  nvs_get_str(h, "host",  out->host,  &len);
    nvs_get_u16(h, "port", &port); out->port = port;
    len = sizeof(out->user);  nvs_get_str(h, "user",  out->user,  &len);
    len = sizeof(out->pass);  nvs_get_str(h, "pass",  out->pass,  &len);
    len = sizeof(out->topic); nvs_get_str(h, "topic", out->topic, &len);

    nvs_close(h);
    return ESP_OK;
}

esp_err_t config_store_set_mqtt(const mqtt_config_t *cfg)
{
    nvs_handle_t h;
    ESP_RETURN_ON_ERROR(open_nvs("mqtt", NVS_READWRITE, &h), TAG, "open nvs");
    nvs_set_str(h,  "host",  cfg->host);
    nvs_set_u16(h,  "port",  cfg->port);
    nvs_set_str(h,  "user",  cfg->user);
    nvs_set_str(h,  "pass",  cfg->pass);
    nvs_set_str(h,  "topic", cfg->topic);
    esp_err_t ret = nvs_commit(h);
    nvs_close(h);
    return ret;
}

/* --- GPIO ------------------------------------------------------------- */

static void gpio_ns_name(int pin, char *buf, size_t len)
{
    snprintf(buf, len, "gpio%d", pin);
}

esp_err_t config_store_get_gpio(int pin, gpio_cfg_t *out)
{
    char ns[16];
    gpio_ns_name(pin, ns, sizeof(ns));

    nvs_handle_t h;
    esp_err_t ret = open_nvs(ns, NVS_READONLY, &h);
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        memset(out, 0, sizeof(*out));
        out->mode        = GPIO_MODE_DISABLED;
        out->pulse_count = 1;
        return ESP_OK;
    }
    if (ret != ESP_OK) return ret;

    uint8_t v;
    nvs_get_u8(h, "mode",    &out->mode);
    if (nvs_get_u8(h, "pull",    &v) == ESP_OK) out->pull_up    = (bool)v;
    if (nvs_get_u8(h, "invert",  &v) == ESP_OK) out->invert     = (bool)v;
    if (nvs_get_u8(h, "pulse",   &v) == ESP_OK) out->pulse_mode = (bool)v;
    if (nvs_get_u8(h, "pcnt",    &v) == ESP_OK) out->pulse_count = v;

    nvs_close(h);
    return ESP_OK;
}

esp_err_t config_store_set_gpio(int pin, const gpio_cfg_t *cfg)
{
    char ns[16];
    gpio_ns_name(pin, ns, sizeof(ns));

    nvs_handle_t h;
    ESP_RETURN_ON_ERROR(open_nvs(ns, NVS_READWRITE, &h), TAG, "open nvs");
    nvs_set_u8(h, "mode",   cfg->mode);
    nvs_set_u8(h, "pull",   (uint8_t)cfg->pull_up);
    nvs_set_u8(h, "invert", (uint8_t)cfg->invert);
    nvs_set_u8(h, "pulse",  (uint8_t)cfg->pulse_mode);
    nvs_set_u8(h, "pcnt",   cfg->pulse_count);
    esp_err_t ret = nvs_commit(h);
    nvs_close(h);
    return ret;
}

/* --- Node registry ---------------------------------------------------- */

esp_err_t config_store_get_nodes(node_entry_t *entries, int *count)
{
    nvs_handle_t h;
    esp_err_t ret = open_nvs("nodes", NVS_READONLY, &h);
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        *count = 0;
        return ESP_OK;
    }
    if (ret != ESP_OK) return ret;

    uint8_t n = 0;
    nvs_get_u8(h, "count", &n);
    if (n > CONFIG_STORE_MAX_NODES) n = CONFIG_STORE_MAX_NODES;

    for (int i = 0; i < n; i++) {
        char key[16];
        snprintf(key, sizeof(key), "mac%d", i);
        size_t mac_len = 6;
        nvs_get_blob(h, key, entries[i].mac, &mac_len);
        snprintf(key, sizeof(key), "id%d", i);
        nvs_get_u8(h, key, &entries[i].node_id);
    }
    *count = n;
    nvs_close(h);
    return ESP_OK;
}

esp_err_t config_store_save_nodes(const node_entry_t *entries, int count)
{
    nvs_handle_t h;
    ESP_RETURN_ON_ERROR(open_nvs("nodes", NVS_READWRITE, &h), TAG, "open nvs");
    nvs_set_u8(h, "count", (uint8_t)count);
    for (int i = 0; i < count; i++) {
        char key[16];
        snprintf(key, sizeof(key), "mac%d", i);
        nvs_set_blob(h, key, entries[i].mac, 6);
        snprintf(key, sizeof(key), "id%d", i);
        nvs_set_u8(h, key, entries[i].node_id);
    }
    esp_err_t ret = nvs_commit(h);
    nvs_close(h);
    return ret;
}
