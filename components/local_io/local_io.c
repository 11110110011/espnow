#include "local_io.h"
#include "config_store.h"
#include "mqtt_bridge.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "local_io";
static void publish_gpio_map(void);
static void clear_retained_cmds(void);

#define DEBOUNCE_MS       50
#define PULSE_PERIOD_MS   120
#define POLL_INTERVAL_MS  20

/* Map logical pin indices 0–7 to actual GPIO numbers.
   Adjust these for the target hardware. */
static const int s_gpio_map[CONFIG_STORE_GPIO_COUNT] = {
    32, 33, 34, 35, 25, 26, 27, 14, 36
};

static gpio_cfg_t  s_cfg[CONFIG_STORE_GPIO_COUNT];
static bool        s_last_input[CONFIG_STORE_GPIO_COUNT];
static bool        s_output_state[CONFIG_STORE_GPIO_COUNT];

/* -----------------------------------------------------------------------
 * Input polling task
 * --------------------------------------------------------------------- */

static void input_poll_task(void *arg)
{
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(POLL_INTERVAL_MS));
        for (int i = 0; i < CONFIG_STORE_GPIO_COUNT; i++) {
            if (s_cfg[i].mode != CFG_GPIO_MODE_INPUT) continue;
            int raw   = gpio_get_level(s_gpio_map[i]);
            bool level = s_cfg[i].invert ? !raw : !!raw;
            if (level != s_last_input[i]) {
                s_last_input[i] = level;
                mqtt_bridge_publish_gpio_state(i, level);
                ESP_LOGD(TAG, "GPIO %d (pin %d) -> %s", i, s_gpio_map[i], level ? "ON" : "OFF");
            }
        }
    }
}

/* -----------------------------------------------------------------------
 * Pin initialisation
 * --------------------------------------------------------------------- */

static void configure_pin(int idx)
{
    int gpio_num = s_gpio_map[idx];
    if (s_cfg[idx].mode == CFG_GPIO_MODE_DISABLED) {
        gpio_reset_pin(gpio_num);
        return;
    }

    /* GPIO34, 35, 36, 39 are input-only and do not support pull resistors */
    bool pull_capable = !(gpio_num >= 34 && gpio_num <= 39);
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << gpio_num),
        .intr_type    = GPIO_INTR_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en   = (s_cfg[idx].pull_up && pull_capable) ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
    };

    if (s_cfg[idx].mode == CFG_GPIO_MODE_INPUT) {
        io_conf.mode = CFG_GPIO_MODE_INPUT;
        s_last_input[idx] = false;
    } else {
        io_conf.mode = CFG_GPIO_MODE_OUTPUT;
    }
    gpio_config(&io_conf);

    if (s_cfg[idx].mode == CFG_GPIO_MODE_OUTPUT) {
        int off_level = s_cfg[idx].invert ? 1 : 0;
        gpio_set_level(gpio_num, off_level);
        s_output_state[idx] = false;
    }
}

/* -----------------------------------------------------------------------
 * MQTT ON/OFF command  topic: {base}/gpio/X/command  payload: ON | OFF
 * --------------------------------------------------------------------- */

static void mqtt_cmd_cb(const char *topic, const char *payload, int len)
{
    (void)len;
    /* extract pin index from last path segment: ".../gpio/N/command" */
    const char *p = strrchr(topic, '/');          /* → "/command" */
    if (!p) return;
    /* step back one more segment to find the pin number */
    const char *q = p - 1;
    while (q > topic && *q != '/') q--;
    int pin = atoi(q + 1);

    esp_err_t err;
    if (strncmp(payload, "ON", 2) == 0) {
        err = local_io_set_output(pin, true);
    } else if (strncmp(payload, "OFF", 3) == 0) {
        err = local_io_set_output(pin, false);
    } else if (strncmp(payload, "TOGGLE", 6) == 0) {
        err = local_io_set_output(pin, !s_output_state[pin]);
    } else {
        ESP_LOGW(TAG, "gpio cmd pin %d: unknown payload '%s'", pin, payload);
        return;
    }
    if (err != ESP_OK)
        ESP_LOGW(TAG, "gpio cmd pin %d failed: %s", pin, esp_err_to_name(err));
}

/* -----------------------------------------------------------------------
 * MQTT pulse command  {"pin":X,"pulses":N}
 * --------------------------------------------------------------------- */

static void mqtt_pulse_cb(const char *topic, const char *payload, int len)
{
    (void)topic; (void)len;
    ESP_LOGI(TAG, "pulse cmd received: '%s'", payload);
    int pin = -1, pulses = 1;
    const char *p;
    if ((p = strstr(payload, "\"pin\":")))    pin    = atoi(p + 6);
    if ((p = strstr(payload, "\"pulses\":"))) pulses = atoi(p + 9);
    if (pin < 0) {
        ESP_LOGW(TAG, "pulse cmd: missing pin");
        return;
    }
    esp_err_t err = local_io_trigger_pulse(pin, pulses);
    if (err != ESP_OK)
        ESP_LOGW(TAG, "pulse cmd pin %d failed: %s", pin, esp_err_to_name(err));
}

/* -----------------------------------------------------------------------
 * Public API
 * --------------------------------------------------------------------- */

esp_err_t local_io_init(void)
{
    for (int i = 0; i < CONFIG_STORE_GPIO_COUNT; i++) {
        config_store_get_gpio(i, &s_cfg[i]);
        configure_pin(i);
    }
    xTaskCreate(input_poll_task, "local_io_poll", 2048, NULL, 4, NULL);

    mqtt_config_t mcfg;
    config_store_get_mqtt(&mcfg);
    char topic[80];

    /* Subscribe command topic for each output pin (skip reserved) */
    for (int i = 0; i < CONFIG_STORE_GPIO_COUNT; i++) {
        if (local_io_pin_is_reserved(i)) continue;
        if (s_cfg[i].mode == CFG_GPIO_MODE_OUTPUT) {
            snprintf(topic, sizeof(topic), "%s/gpio/%d/command", mcfg.topic, i);
            mqtt_bridge_subscribe(topic, mqtt_cmd_cb);
        }
    }

    /* Publish GPIO map on every MQTT connect */
    mqtt_bridge_register_connect_cb(publish_gpio_map);

    /* Pulse control */
    snprintf(topic, sizeof(topic), "%s/gpio/control", mcfg.topic);
    mqtt_bridge_subscribe(topic, mqtt_pulse_cb);

    /* Clear retained messages on every MQTT connect */
    mqtt_bridge_register_connect_cb(clear_retained_cmds);

    ESP_LOGI(TAG, "Local I/O initialised, pulse control on %s", topic);
    return ESP_OK;
}

esp_err_t local_io_set_output(int pin, bool state)
{
    if (pin < 0 || pin >= CONFIG_STORE_GPIO_COUNT) return ESP_ERR_INVALID_ARG;
    if (s_cfg[pin].mode != CFG_GPIO_MODE_OUTPUT)   return ESP_ERR_INVALID_STATE;

    /* Interlock: before turning this pin ON, turn off any linked partner */
    if (state) {
        for (int y = 0; y < CONFIG_STORE_GPIO_COUNT; y++) {
            if (y == pin) continue;
            bool linked = (s_cfg[pin].linked_pin == (uint8_t)y) ||
                          (s_cfg[y].linked_pin   == (uint8_t)pin);
            if (linked && s_output_state[y] && s_cfg[y].mode == CFG_GPIO_MODE_OUTPUT) {
                bool off_level = s_cfg[y].invert ? 1 : 0;
                gpio_set_level(s_gpio_map[y], off_level);
                s_output_state[y] = false;
                mqtt_bridge_publish_gpio_state(y, false);
                ESP_LOGI(TAG, "Interlock: pin %d OFF before pin %d ON", y, pin);
                vTaskDelay(pdMS_TO_TICKS(100));  /* relay settle */
            }
        }
    }

    bool level = s_cfg[pin].invert ? !state : state;
    gpio_set_level(s_gpio_map[pin], level ? 1 : 0);
    s_output_state[pin] = state;
    mqtt_bridge_publish_gpio_state(pin, state);
    return ESP_OK;
}

esp_err_t local_io_trigger_pulse(int pin, int count)
{
    if (pin < 0 || pin >= CONFIG_STORE_GPIO_COUNT) return ESP_ERR_INVALID_ARG;
    if (s_cfg[pin].mode != CFG_GPIO_MODE_OUTPUT)   return ESP_ERR_INVALID_STATE;
    if (!s_cfg[pin].pulse_mode)                    return ESP_ERR_INVALID_STATE;

    if (count < 1) count = 1;
    if (count > 5) count = 5;

    int on_level  = s_cfg[pin].invert ? 0 : 1;
    int off_level = s_cfg[pin].invert ? 1 : 0;
    for (int i = 0; i < count; i++) {
        gpio_set_level(s_gpio_map[pin], on_level);
        vTaskDelay(pdMS_TO_TICKS(PULSE_PERIOD_MS / 2));
        gpio_set_level(s_gpio_map[pin], off_level);
        vTaskDelay(pdMS_TO_TICKS(PULSE_PERIOD_MS / 2));
    }
    return ESP_OK;
}

bool local_io_get_input(int pin)
{
    if (pin < 0 || pin >= CONFIG_STORE_GPIO_COUNT) return false;
    if (s_cfg[pin].mode != CFG_GPIO_MODE_INPUT)        return false;
    return s_last_input[pin];
}

esp_err_t local_io_reconfigure(int pin, const gpio_cfg_t *cfg)
{
    if (pin < 0 || pin >= CONFIG_STORE_GPIO_COUNT) return ESP_ERR_INVALID_ARG;
    memcpy(&s_cfg[pin], cfg, sizeof(gpio_cfg_t));
    configure_pin(pin);
    config_store_set_gpio(pin, cfg);
    return ESP_OK;
}

bool local_io_pin_is_reserved(int pin)
{
    /* Pin 7 = GPIO14 is used as W5500 RST */
    return (pin == 7);
}

bool local_io_pin_is_input_only(int pin)
{
    if (pin < 0 || pin >= CONFIG_STORE_GPIO_COUNT) return false;
    int gpio_num = s_gpio_map[pin];
    return (gpio_num >= 34 && gpio_num <= 39);
}

/* -----------------------------------------------------------------------
 * Clear retained command topics — fires on every MQTT connect.
 * Publishing an empty retained message removes the broker's stored copy.
 * --------------------------------------------------------------------- */

static void clear_retained_cmds(void)
{
    mqtt_config_t mcfg;
    config_store_get_mqtt(&mcfg);
    char topic[80];

    /* Clear pulse control topic */
    snprintf(topic, sizeof(topic), "%s/gpio/control", mcfg.topic);
    mqtt_bridge_publish(topic, "", 0, true);

    /* Clear per-pin command topics for all output pins */
    for (int i = 0; i < CONFIG_STORE_GPIO_COUNT; i++) {
        if (local_io_pin_is_reserved(i)) continue;
        if (s_cfg[i].mode == CFG_GPIO_MODE_OUTPUT) {
            snprintf(topic, sizeof(topic), "%s/gpio/%d/command", mcfg.topic, i);
            mqtt_bridge_publish(topic, "", 0, true);
        }
    }
}

/* -----------------------------------------------------------------------
 * GPIO map publish — fires on every MQTT connect
 * --------------------------------------------------------------------- */

static void publish_gpio_map(void)
{
    mqtt_config_t mcfg;
    config_store_get_mqtt(&mcfg);

    /* Build: {"0":32,"1":33,...,"input_only":[2,3,8]} skipping reserved pins */
    char buf[256];
    int  pos = 0;
    pos += snprintf(buf + pos, sizeof(buf) - pos, "{");

    bool first = true;
    for (int i = 0; i < CONFIG_STORE_GPIO_COUNT; i++) {
        if (local_io_pin_is_reserved(i)) continue;
        pos += snprintf(buf + pos, sizeof(buf) - pos,
                        "%s\"%d\":%d", first ? "" : ",", i, s_gpio_map[i]);
        first = false;
    }

    /* append input_only array */
    pos += snprintf(buf + pos, sizeof(buf) - pos, ",\"input_only\":[");
    bool first_ro = true;
    for (int i = 0; i < CONFIG_STORE_GPIO_COUNT; i++) {
        if (local_io_pin_is_reserved(i)) continue;
        if (local_io_pin_is_input_only(i)) {
            pos += snprintf(buf + pos, sizeof(buf) - pos,
                            "%s%d", first_ro ? "" : ",", i);
            first_ro = false;
        }
    }
    snprintf(buf + pos, sizeof(buf) - pos, "]}");

    char topic[80];
    snprintf(topic, sizeof(topic), "%s/gpio/map", mcfg.topic);
    mqtt_bridge_publish(topic, buf, 0, true);
    ESP_LOGI(TAG, "GPIO map published: %s", buf);
}
