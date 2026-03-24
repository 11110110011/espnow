#include "web_cfg.h"
#include "config_store.h"
#include "node_table.h"
#include "local_io.h"
#include "sys_status.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_http_server.h"
#include "esp_system.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "web_cfg";

static httpd_handle_t s_server = NULL;

/* -----------------------------------------------------------------------
 * Helpers
 * --------------------------------------------------------------------- */

static void send_html(httpd_req_t *req, const char *html)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, html, HTTPD_RESP_USE_STRLEN);
}

/* Decode a percent-encoded query/body into key=value pairs */
static int get_field(const char *body, const char *key, char *out, size_t out_len)
{
    char search[64];
    snprintf(search, sizeof(search), "%s=", key);
    const char *p = strstr(body, search);
    if (!p) { out[0] = '\0'; return 0; }
    p += strlen(search);
    size_t i = 0;
    while (*p && *p != '&' && i < out_len - 1) {
        if (*p == '+') { out[i++] = ' '; p++; }
        else if (*p == '%' && p[1] && p[2]) {
            char hex[3] = {p[1], p[2], 0};
            out[i++] = (char)strtol(hex, NULL, 16);
            p += 3;
        } else {
            out[i++] = *p++;
        }
    }
    out[i] = '\0';
    return i;
}

/* -----------------------------------------------------------------------
 * GET /  — Dashboard
 * --------------------------------------------------------------------- */

static esp_err_t handle_root(httpd_req_t *req)
{
    char ip[16] = "unknown";
    /* eth_mgr_get_ip not directly imported here; show placeholder */

    char html[2048];
    snprintf(html, sizeof(html),
        "<!DOCTYPE html><html><head><meta charset='utf-8'>"
        "<title>ESP-NOW Gateway</title></head><body>"
        "<h1>smart-home-espnow Gateway</h1>"
        "<p>Nodes registered: <b>%d</b></p>"
        "<ul>"
        "  <li><a href='/config'>Configuration</a></li>"
        "  <li><a href='/nodes'>Node Registry</a></li>"
        "  <li><a href='/gpio'>Local GPIO</a></li>"
        "</ul>"
        "<form method='POST' action='/restart'>"
        "  <button type='submit'>Restart Gateway</button>"
        "</form>"
        "</body></html>",
        node_table_count());

    send_html(req, html);
    return ESP_OK;
}

/* -----------------------------------------------------------------------
 * GET /nodes
 * --------------------------------------------------------------------- */

static esp_err_t handle_nodes(httpd_req_t *req)
{
    char html[2048] = "<!DOCTYPE html><html><head><meta charset='utf-8'>"
                      "<title>Nodes</title></head><body>"
                      "<h1>Node Registry</h1>"
                      "<table border='1'><tr><th>ID</th><th>MAC</th>"
                      "<th>State</th><th>Online</th></tr>";

    for (uint8_t id = 1; id <= 12; id++) {
        node_record_t *rec = node_table_find_by_id(id);
        if (!rec) continue;
        char row[256];
        snprintf(row, sizeof(row),
            "<tr><td>%u</td><td>%02x:%02x:%02x:%02x:%02x:%02x</td>"
            "<td>%s</td><td>%s</td></tr>",
            rec->node_id,
            rec->mac[0], rec->mac[1], rec->mac[2],
            rec->mac[3], rec->mac[4], rec->mac[5],
            rec->state  ? "ON" : "OFF",
            rec->online ? "yes" : "no");
        strlcat(html, row, sizeof(html));
    }
    strlcat(html, "</table><a href='/'>Back</a></body></html>", sizeof(html));
    send_html(req, html);
    return ESP_OK;
}

/* -----------------------------------------------------------------------
 * GET /gpio
 * --------------------------------------------------------------------- */

static esp_err_t handle_gpio(httpd_req_t *req)
{
    char html[2048] = "<!DOCTYPE html><html><head><meta charset='utf-8'>"
                      "<title>GPIO</title></head><body>"
                      "<h1>Local GPIO Status</h1>"
                      "<table border='1'><tr><th>Pin</th><th>Mode</th>"
                      "<th>State</th></tr>";

    for (int i = 0; i < CONFIG_STORE_GPIO_COUNT; i++) {
        gpio_cfg_t cfg;
        config_store_get_gpio(i, &cfg);
        const char *mode_str[] = {"disabled", "input", "output"};
        char row[128];
        snprintf(row, sizeof(row),
            "<tr><td>%d</td><td>%s</td><td>%s</td></tr>",
            i,
            mode_str[cfg.mode < 3 ? cfg.mode : 0],
            cfg.mode == CFG_GPIO_MODE_INPUT
                ? (local_io_get_input(i) ? "ON" : "OFF")
                : "-");
        strlcat(html, row, sizeof(html));
    }
    strlcat(html, "</table><a href='/'>Back</a></body></html>", sizeof(html));
    send_html(req, html);
    return ESP_OK;
}

/* -----------------------------------------------------------------------
 * GET /config  — Configuration form
 * --------------------------------------------------------------------- */

static esp_err_t handle_config_get(httpd_req_t *req)
{
    net_config_t  net;
    mqtt_config_t mqtt;
    config_store_get_net(&net);
    config_store_get_mqtt(&mqtt);

    char html[4096];
    int  n = 0;

    n += snprintf(html + n, sizeof(html) - n,
        "<!DOCTYPE html><html><head><meta charset='utf-8'>"
        "<title>Config</title></head><body>"
        "<h1>Gateway Configuration</h1>"
        "<form method='POST' action='/config'>"
        "<h2>Network</h2>"
        "DHCP: <input type='checkbox' name='dhcp' value='1'%s><br>"
        "IP: <input name='ip' value='%s'><br>"
        "Mask: <input name='mask' value='%s'><br>"
        "GW: <input name='gw' value='%s'><br>"
        "DNS: <input name='dns' value='%s'><br>",
        net.dhcp ? " checked" : "",
        net.ip, net.mask, net.gw, net.dns);

    n += snprintf(html + n, sizeof(html) - n,
        "<h2>MQTT</h2>"
        "Host: <input name='mqtt_host' value='%s'><br>"
        "Port: <input name='mqtt_port' value='%u'><br>"
        "User: <input name='mqtt_user' value='%s'><br>"
        "Password: <input type='password' name='mqtt_pass'><br>"
        "Base topic: <input name='mqtt_topic' value='%s'><br>",
        mqtt.host, mqtt.port, mqtt.user, mqtt.topic);

    n += snprintf(html + n, sizeof(html) - n,
        "<h2>Local GPIO</h2>"
        "<table border='1'><tr><th>Pin</th><th>Mode</th>"
        "<th>Pull-up</th><th>Invert</th><th>Pulse</th><th>Pulse count</th></tr>");

    for (int i = 0; i < CONFIG_STORE_GPIO_COUNT; i++) {
        gpio_cfg_t cfg;
        config_store_get_gpio(i, &cfg);
        n += snprintf(html + n, sizeof(html) - n,
            "<tr><td>%d</td>"
            "<td><select name='gmode%d'>"
            "<option value='0'%s>disabled</option>"
            "<option value='1'%s>input</option>"
            "<option value='2'%s>output</option>"
            "</select></td>"
            "<td><input type='checkbox' name='gpull%d' value='1'%s></td>"
            "<td><input type='checkbox' name='ginv%d' value='1'%s></td>"
            "<td><input type='checkbox' name='gpulse%d' value='1'%s></td>"
            "<td><input name='gpcnt%d' size='2' value='%d'></td>"
            "</tr>",
            i,
            i,
            cfg.mode == 0 ? " selected" : "",
            cfg.mode == 1 ? " selected" : "",
            cfg.mode == 2 ? " selected" : "",
            i, cfg.pull_up    ? " checked" : "",
            i, cfg.invert     ? " checked" : "",
            i, cfg.pulse_mode ? " checked" : "",
            i, cfg.pulse_count);
    }

    n += snprintf(html + n, sizeof(html) - n,
        "</table>"
        "<br><input type='submit' value='Save'>"
        "</form><a href='/'>Back</a></body></html>");

    send_html(req, html);
    return ESP_OK;
}

/* -----------------------------------------------------------------------
 * POST /config  — Save configuration
 * --------------------------------------------------------------------- */

static esp_err_t handle_config_post(httpd_req_t *req)
{
    int  total = req->content_len;
    if (total <= 0 || total > 2048) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Bad body");
        return ESP_FAIL;
    }
    char *body = malloc(total + 1);
    if (!body) { httpd_resp_send_500(req); return ESP_FAIL; }

    int received = httpd_req_recv(req, body, total);
    body[received > 0 ? received : 0] = '\0';

    char tmp[64];

    /* Network */
    net_config_t net;
    get_field(body, "dhcp",  tmp, sizeof(tmp));
    net.dhcp = (tmp[0] == '1');
    get_field(body, "ip",   net.ip,   sizeof(net.ip));
    get_field(body, "mask", net.mask, sizeof(net.mask));
    get_field(body, "gw",   net.gw,   sizeof(net.gw));
    get_field(body, "dns",  net.dns,  sizeof(net.dns));
    config_store_set_net(&net);

    /* MQTT */
    mqtt_config_t mqtt;
    config_store_get_mqtt(&mqtt);  /* keep existing password if blank */
    get_field(body, "mqtt_host",  mqtt.host,  sizeof(mqtt.host));
    get_field(body, "mqtt_topic", mqtt.topic, sizeof(mqtt.topic));
    get_field(body, "mqtt_user",  mqtt.user,  sizeof(mqtt.user));
    if (get_field(body, "mqtt_pass", tmp, sizeof(tmp)) > 0) {
        strlcpy(mqtt.pass, tmp, sizeof(mqtt.pass));
    }
    get_field(body, "mqtt_port", tmp, sizeof(tmp));
    if (tmp[0]) mqtt.port = (uint16_t)atoi(tmp);
    config_store_set_mqtt(&mqtt);

    /* GPIO */
    for (int i = 0; i < CONFIG_STORE_GPIO_COUNT; i++) {
        gpio_cfg_t cfg = {0};
        char key[16];

        snprintf(key, sizeof(key), "gmode%d", i);
        get_field(body, key, tmp, sizeof(tmp));
        cfg.mode = (uint8_t)atoi(tmp);

        snprintf(key, sizeof(key), "gpull%d", i);
        get_field(body, key, tmp, sizeof(tmp));
        cfg.pull_up = (tmp[0] == '1');

        snprintf(key, sizeof(key), "ginv%d", i);
        get_field(body, key, tmp, sizeof(tmp));
        cfg.invert = (tmp[0] == '1');

        snprintf(key, sizeof(key), "gpulse%d", i);
        get_field(body, key, tmp, sizeof(tmp));
        cfg.pulse_mode = (tmp[0] == '1');

        snprintf(key, sizeof(key), "gpcnt%d", i);
        get_field(body, key, tmp, sizeof(tmp));
        cfg.pulse_count = tmp[0] ? (uint8_t)atoi(tmp) : 1;
        if (cfg.pulse_count < 1) cfg.pulse_count = 1;
        if (cfg.pulse_count > 5) cfg.pulse_count = 5;

        local_io_reconfigure(i, &cfg);
    }

    free(body);

    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req,
        "<html><body><p>Saved. "
        "<a href='/'>Back</a></p></body></html>",
        HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

/* -----------------------------------------------------------------------
 * POST /restart
 * --------------------------------------------------------------------- */

static esp_err_t handle_restart(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req,
        "<html><body><p>Restarting...</p></body></html>",
        HTTPD_RESP_USE_STRLEN);
    /* Small delay so response is sent before reset */
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
    return ESP_OK;
}

/* -----------------------------------------------------------------------
 * Public API
 * --------------------------------------------------------------------- */

esp_err_t web_cfg_start(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;

    ESP_RETURN_ON_ERROR(httpd_start(&s_server, &config), TAG, "start server");

    static const httpd_uri_t routes[] = {
        { .uri = "/",        .method = HTTP_GET,  .handler = handle_root },
        { .uri = "/nodes",   .method = HTTP_GET,  .handler = handle_nodes },
        { .uri = "/gpio",    .method = HTTP_GET,  .handler = handle_gpio },
        { .uri = "/config",  .method = HTTP_GET,  .handler = handle_config_get },
        { .uri = "/config",  .method = HTTP_POST, .handler = handle_config_post },
        { .uri = "/restart", .method = HTTP_POST, .handler = handle_restart },
    };

    for (int i = 0; i < (int)(sizeof(routes)/sizeof(routes[0])); i++) {
        httpd_register_uri_handler(s_server, &routes[i]);
    }

    ESP_LOGI(TAG, "Web config server started on port 80");
    return ESP_OK;
}

esp_err_t web_cfg_stop(void)
{
    if (!s_server) return ESP_OK;
    httpd_stop(s_server);
    s_server = NULL;
    return ESP_OK;
}
