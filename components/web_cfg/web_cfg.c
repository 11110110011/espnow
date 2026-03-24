#include "web_cfg.h"
#include "config_store.h"
#include "node_table.h"
#include "local_io.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_http_server.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static const char *TAG = "web_cfg";
static httpd_handle_t s_server = NULL;

/* -----------------------------------------------------------------------
 * Helpers
 * --------------------------------------------------------------------- */

/* Percent-decode a single form field from an urlencoded body */
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
        } else { out[i++] = *p++; }
    }
    out[i] = '\0';
    return (int)i;
}

/* Send one small formatted chunk (fits in 256 bytes) */
static void chunk_fmt(httpd_req_t *req, const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    httpd_resp_sendstr_chunk(req, buf);
}

/* -----------------------------------------------------------------------
 * GET /  — full single-page UI
 * --------------------------------------------------------------------- */

static esp_err_t handle_root(httpd_req_t *req)
{
    net_config_t  net;
    mqtt_config_t mqtt;
    config_store_get_net(&net);
    config_store_get_mqtt(&mqtt);

    httpd_resp_set_type(req, "text/html");

    /* Head + minimal CSS */
    httpd_resp_sendstr_chunk(req,
        "<!DOCTYPE html><html><head>"
        "<meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>ESP-NOW Gateway</title>"
        "<style>"
        "body{font-family:sans-serif;max-width:640px;margin:16px auto;padding:0 8px}"
        "h2{border-bottom:1px solid #ccc;margin-top:20px}"
        "label{display:inline-block;width:90px}"
        "input:not([type=checkbox]):not([type=number]):not([type=submit])"
        "{width:160px;margin-bottom:4px}"
        "input[type=number]{width:50px}"
        "table{border-collapse:collapse;width:100%}"
        "td,th{border:1px solid #ccc;padding:3px 6px}"
        "button,input[type=submit]{margin-top:10px;padding:6px 18px;cursor:pointer}"
        "</style></head><body>");

    httpd_resp_sendstr_chunk(req, "<h1>ESP-NOW Gateway</h1>");

    /* ---- Nodes ---- */
    httpd_resp_sendstr_chunk(req, "<h2>Nodes</h2>");
    int cnt = node_table_count();
    if (cnt == 0) {
        httpd_resp_sendstr_chunk(req, "<p>No nodes registered.</p>");
    } else {
        httpd_resp_sendstr_chunk(req,
            "<table><tr><th>ID</th><th>MAC</th><th>State</th><th>Online</th></tr>");
        for (uint8_t id = 1; id <= CONFIG_STORE_MAX_NODES; id++) {
            node_record_t *r = node_table_find_by_id(id);
            if (!r) continue;
            chunk_fmt(req,
                "<tr><td>%u</td>"
                "<td>%02x:%02x:%02x:%02x:%02x:%02x</td>"
                "<td>%s</td><td>%s</td></tr>",
                r->node_id,
                r->mac[0], r->mac[1], r->mac[2],
                r->mac[3], r->mac[4], r->mac[5],
                r->state  ? "ON"  : "OFF",
                r->online ? "yes" : "no");
        }
        httpd_resp_sendstr_chunk(req, "</table>");
    }

    /* ---- Configuration form ---- */
    httpd_resp_sendstr_chunk(req, "<form method='POST' action='/config'>");

    /* Network */
    httpd_resp_sendstr_chunk(req, "<h2>Network</h2>");
    chunk_fmt(req, "<label>DHCP</label>"
        "<input type='checkbox' name='dhcp' value='1'%s><br>",
        net.dhcp ? " checked" : "");
    chunk_fmt(req, "<label>IP</label>"
        "<input name='ip' value='%s'><br>", net.ip);
    chunk_fmt(req, "<label>Mask</label>"
        "<input name='mask' value='%s'><br>", net.mask);
    chunk_fmt(req, "<label>Gateway</label>"
        "<input name='gw' value='%s'><br>", net.gw);
    chunk_fmt(req, "<label>DNS</label>"
        "<input name='dns' value='%s'><br>", net.dns);

    /* MQTT */
    httpd_resp_sendstr_chunk(req, "<h2>MQTT</h2>");
    chunk_fmt(req, "<label>Host</label>"
        "<input name='mqtt_host' value='%s'><br>", mqtt.host);
    chunk_fmt(req, "<label>Port</label>"
        "<input name='mqtt_port' type='number' min='1' max='65535' value='%u'><br>",
        mqtt.port);
    chunk_fmt(req, "<label>User</label>"
        "<input name='mqtt_user' value='%s'><br>", mqtt.user);
    httpd_resp_sendstr_chunk(req, "<label>Password</label>"
        "<input type='password' name='mqtt_pass'"
        " placeholder='(unchanged)'><br>");
    chunk_fmt(req, "<label>Topic</label>"
        "<input name='mqtt_topic' value='%s'><br>", mqtt.topic);

    /* GPIO */
    httpd_resp_sendstr_chunk(req, "<h2>Local GPIO</h2>"
        "<table><tr>"
        "<th>Pin</th><th>Mode</th>"
        "<th>Pull-up</th><th>Invert</th>"
        "<th>Pulse</th><th>Count</th>"
        "</tr>");

    for (int i = 0; i < CONFIG_STORE_GPIO_COUNT; i++) {
        gpio_cfg_t cfg;
        config_store_get_gpio(i, &cfg);

        /* pin + mode select */
        chunk_fmt(req,
            "<tr><td>%d</td>"
            "<td><select name='gmode%d'>"
            "<option value='0'%s>off</option>"
            "<option value='1'%s>in</option>"
            "<option value='2'%s>out</option>"
            "</select></td>",
            i, i,
            cfg.mode == CFG_GPIO_MODE_DISABLED ? " selected" : "",
            cfg.mode == CFG_GPIO_MODE_INPUT    ? " selected" : "",
            cfg.mode == CFG_GPIO_MODE_OUTPUT   ? " selected" : "");

        /* checkboxes */
        chunk_fmt(req,
            "<td><input type='checkbox' name='gpull%d' value='1'%s></td>"
            "<td><input type='checkbox' name='ginv%d'  value='1'%s></td>"
            "<td><input type='checkbox' name='gpulse%d' value='1'%s></td>",
            i, cfg.pull_up    ? " checked" : "",
            i, cfg.invert     ? " checked" : "",
            i, cfg.pulse_mode ? " checked" : "");

        /* pulse count */
        chunk_fmt(req,
            "<td><input name='gpcnt%d' type='number' min='1' max='5' value='%d'></td>"
            "</tr>",
            i, cfg.pulse_count ? cfg.pulse_count : 1);
    }

    httpd_resp_sendstr_chunk(req, "</table>");
    httpd_resp_sendstr_chunk(req,
        "<br><input type='submit' value='Save'></form>");

    /* Restart */
    httpd_resp_sendstr_chunk(req,
        "<form method='POST' action='/restart' style='margin-top:8px'>"
        "<button type='submit'>Restart</button></form>"
        "</body></html>");

    /* Terminate chunked transfer */
    httpd_resp_sendstr_chunk(req, NULL);
    return ESP_OK;
}

/* -----------------------------------------------------------------------
 * POST /config  — save settings, redirect back
 * --------------------------------------------------------------------- */

static esp_err_t handle_config_post(httpd_req_t *req)
{
    int total = req->content_len;
    if (total <= 0 || total > 4096) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad body");
        return ESP_FAIL;
    }
    char *body = malloc(total + 1);
    if (!body) { httpd_resp_send_500(req); return ESP_FAIL; }
    int got = httpd_req_recv(req, body, total);
    body[got > 0 ? got : 0] = '\0';

    char tmp[64];

    /* Network */
    net_config_t net;
    memset(&net, 0, sizeof(net));
    get_field(body, "dhcp", tmp, sizeof(tmp));
    net.dhcp = (tmp[0] == '1');
    get_field(body, "ip",   net.ip,   sizeof(net.ip));
    get_field(body, "mask", net.mask, sizeof(net.mask));
    get_field(body, "gw",   net.gw,   sizeof(net.gw));
    get_field(body, "dns",  net.dns,  sizeof(net.dns));
    config_store_set_net(&net);

    /* MQTT */
    mqtt_config_t mqtt;
    config_store_get_mqtt(&mqtt);
    get_field(body, "mqtt_host",  mqtt.host,  sizeof(mqtt.host));
    get_field(body, "mqtt_topic", mqtt.topic, sizeof(mqtt.topic));
    get_field(body, "mqtt_user",  mqtt.user,  sizeof(mqtt.user));
    if (get_field(body, "mqtt_pass", tmp, sizeof(tmp)) > 0)
        strlcpy(mqtt.pass, tmp, sizeof(mqtt.pass));
    get_field(body, "mqtt_port", tmp, sizeof(tmp));
    if (tmp[0]) mqtt.port = (uint16_t)atoi(tmp);
    config_store_set_mqtt(&mqtt);

    /* GPIO */
    for (int i = 0; i < CONFIG_STORE_GPIO_COUNT; i++) {
        gpio_cfg_t cfg;
        memset(&cfg, 0, sizeof(cfg));
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

    /* Redirect back to main page */
    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

/* -----------------------------------------------------------------------
 * POST /restart
 * --------------------------------------------------------------------- */

static esp_err_t handle_restart(httpd_req_t *req)
{
    httpd_resp_sendstr(req,
        "<!DOCTYPE html><html><body><p>Restarting...</p></body></html>");
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
    config.server_port    = 80;
    config.stack_size     = 8192;   /* prevent stack overflow on page render */
    config.max_uri_handlers = 8;

    ESP_RETURN_ON_ERROR(httpd_start(&s_server, &config), TAG, "start");

    static const httpd_uri_t routes[] = {
        { .uri = "/",        .method = HTTP_GET,  .handler = handle_root },
        { .uri = "/config",  .method = HTTP_POST, .handler = handle_config_post },
        { .uri = "/restart", .method = HTTP_POST, .handler = handle_restart },
    };
    for (int i = 0; i < (int)(sizeof(routes)/sizeof(routes[0])); i++)
        httpd_register_uri_handler(s_server, &routes[i]);

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
