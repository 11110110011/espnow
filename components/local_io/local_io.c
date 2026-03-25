#include "local_io.h"
#include "config_store.h"
#include "mqtt_bridge.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "local_io";

#define DEBOUNCE_MS       50
#define PULSE_PERIOD_MS   100
#define POLL_INTERVAL_MS  20

/* Map logical pin indices 0–7 to actual GPIO numbers.
   Adjust these for the target hardware. */
static const int s_gpio_map[CONFIG_STORE_GPIO_COUNT] = {
    32, 33, 34, 35, 25, 26, 27, 14, 36
};

static gpio_cfg_t  s_cfg[CONFIG_STORE_GPIO_COUNT];
static bool        s_last_input[CONFIG_STORE_GPIO_COUNT];

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
    ESP_LOGI(TAG, "Local I/O initialised");
    return ESP_OK;
}

esp_err_t local_io_set_output(int pin, bool state)
{
    if (pin < 0 || pin >= CONFIG_STORE_GPIO_COUNT) return ESP_ERR_INVALID_ARG;
    if (s_cfg[pin].mode != CFG_GPIO_MODE_OUTPUT)       return ESP_ERR_INVALID_STATE;
    bool level = s_cfg[pin].invert ? !state : state;
    gpio_set_level(s_gpio_map[pin], level ? 1 : 0);
    mqtt_bridge_publish_gpio_state(pin, state);
    return ESP_OK;
}

esp_err_t local_io_trigger_pulse(int pin)
{
    if (pin < 0 || pin >= CONFIG_STORE_GPIO_COUNT) return ESP_ERR_INVALID_ARG;
    if (s_cfg[pin].mode != CFG_GPIO_MODE_OUTPUT)       return ESP_ERR_INVALID_STATE;
    if (!s_cfg[pin].pulse_mode)                    return ESP_ERR_INVALID_STATE;

    int count = s_cfg[pin].pulse_count;
    if (count < 1) count = 1;
    if (count > 5) count = 5;

    for (int i = 0; i < count; i++) {
        gpio_set_level(s_gpio_map[pin], 1);
        vTaskDelay(pdMS_TO_TICKS(PULSE_PERIOD_MS / 2));
        gpio_set_level(s_gpio_map[pin], 0);
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
