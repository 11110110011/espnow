#include "bme280_sensor.h"
#include "mqtt_bridge.h"
#include "config_store.h"
#include "esp_log.h"
#include "esp_check.h"
#include "driver/i2c.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "bme280";

#define I2C_PORT            I2C_NUM_0
#define I2C_SDA_GPIO        21
#define I2C_SCL_GPIO        22
#define I2C_CLK_HZ          100000
#define BME280_ADDR_LOW     0x76
#define BME280_ADDR_HIGH    0x77

#define REG_ID              0xD0
#define REG_RESET           0xE0
#define REG_CTRL_HUM        0xF2
#define REG_CTRL_MEAS       0xF4
#define REG_CONFIG          0xF5
#define REG_DATA            0xF7    /* BME280: 8 bytes (press+temp+hum), BMP280: 6 bytes */
#define REG_CALIB_00        0x88    /* 24 bytes: dig_T1..dig_P9 */
#define REG_CALIB_H1        0xA1
#define REG_CALIB_26        0xE1    /* 7 bytes: dig_H2..dig_H6 (BME280 only) */

#define BME280_CHIP_ID      0x60
#define BMP280_CHIP_ID_A    0x56    /* engineering sample */
#define BMP280_CHIP_ID_B    0x57    /* engineering sample */
#define BMP280_CHIP_ID_C    0x58    /* production */

#define PUBLISH_INTERVAL_MS 30000

static uint8_t s_addr        = BME280_ADDR_HIGH;
static bool    s_has_humidity = false;

/* Calibration coefficients */
typedef struct {
    uint16_t dig_T1;
    int16_t  dig_T2, dig_T3;
    uint16_t dig_P1;
    int16_t  dig_P2, dig_P3, dig_P4, dig_P5, dig_P6, dig_P7, dig_P8, dig_P9;
    uint8_t  dig_H1;
    int16_t  dig_H2;
    uint8_t  dig_H3;
    int16_t  dig_H4, dig_H5;
    int8_t   dig_H6;
} calib_t;

static calib_t s_cal;

/* -----------------------------------------------------------------------
 * I2C helpers
 * --------------------------------------------------------------------- */

static esp_err_t i2c_write_reg(uint8_t reg, uint8_t val)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (s_addr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);
    i2c_master_write_byte(cmd, val, true);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(I2C_PORT, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    return ret;
}

static esp_err_t i2c_read_regs(uint8_t reg, uint8_t *buf, size_t len)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (s_addr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (s_addr << 1) | I2C_MASTER_READ, true);
    if (len > 1)
        i2c_master_read(cmd, buf, len - 1, I2C_MASTER_ACK);
    i2c_master_read_byte(cmd, buf + len - 1, I2C_MASTER_NACK);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(I2C_PORT, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    return ret;
}

static esp_err_t i2c_probe(uint8_t addr)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(I2C_PORT, cmd, pdMS_TO_TICKS(50));
    i2c_cmd_link_delete(cmd);
    return ret;
}

/* -----------------------------------------------------------------------
 * Calibration
 * --------------------------------------------------------------------- */

static esp_err_t read_calibration(void)
{
    uint8_t buf[24];

    /* Temperature + pressure (shared by both chips) */
    ESP_RETURN_ON_ERROR(i2c_read_regs(REG_CALIB_00, buf, 24), TAG, "calib T/P read");
    s_cal.dig_T1 = (uint16_t)(buf[1]  << 8 | buf[0]);
    s_cal.dig_T2 = (int16_t) (buf[3]  << 8 | buf[2]);
    s_cal.dig_T3 = (int16_t) (buf[5]  << 8 | buf[4]);
    s_cal.dig_P1 = (uint16_t)(buf[7]  << 8 | buf[6]);
    s_cal.dig_P2 = (int16_t) (buf[9]  << 8 | buf[8]);
    s_cal.dig_P3 = (int16_t) (buf[11] << 8 | buf[10]);
    s_cal.dig_P4 = (int16_t) (buf[13] << 8 | buf[12]);
    s_cal.dig_P5 = (int16_t) (buf[15] << 8 | buf[14]);
    s_cal.dig_P6 = (int16_t) (buf[17] << 8 | buf[16]);
    s_cal.dig_P7 = (int16_t) (buf[19] << 8 | buf[18]);
    s_cal.dig_P8 = (int16_t) (buf[21] << 8 | buf[20]);
    s_cal.dig_P9 = (int16_t) (buf[23] << 8 | buf[22]);

    if (!s_has_humidity) return ESP_OK;

    /* Humidity calibration (BME280 only) */
    ESP_RETURN_ON_ERROR(i2c_read_regs(REG_CALIB_H1, buf, 1), TAG, "calib H1 read");
    s_cal.dig_H1 = buf[0];

    ESP_RETURN_ON_ERROR(i2c_read_regs(REG_CALIB_26, buf, 7), TAG, "calib H2-6 read");
    s_cal.dig_H2 = (int16_t)(buf[1] << 8 | buf[0]);
    s_cal.dig_H3 = buf[2];
    s_cal.dig_H4 = (int16_t)((buf[3] << 4) | (buf[4] & 0x0F));
    s_cal.dig_H5 = (int16_t)((buf[5] << 4) | (buf[4] >> 4));
    s_cal.dig_H6 = (int8_t)buf[6];

    return ESP_OK;
}

/* -----------------------------------------------------------------------
 * Compensation (Bosch datasheet integer formulas)
 * Returns temperature in 0.01 °C units.
 * --------------------------------------------------------------------- */

static int32_t compensate_temperature(int32_t adc_T, int32_t *t_fine)
{
    int32_t var1 = ((((adc_T >> 3) - ((int32_t)s_cal.dig_T1 << 1)))
                    * (int32_t)s_cal.dig_T2) >> 11;
    int32_t var2 = (((((adc_T >> 4) - (int32_t)s_cal.dig_T1)
                    * ((adc_T >> 4) - (int32_t)s_cal.dig_T1)) >> 12)
                    * (int32_t)s_cal.dig_T3) >> 14;
    *t_fine = var1 + var2;
    return (*t_fine * 5 + 128) >> 8;
}

/* Returns humidity in 1/1024 %RH */
static uint32_t compensate_humidity(int32_t adc_H, int32_t t_fine)
{
    int32_t v = t_fine - 76800;
    v = (((((adc_H << 14) - ((int32_t)s_cal.dig_H4 << 20)
          - ((int32_t)s_cal.dig_H5 * v)) + 16384) >> 15)
         * (((((((v * (int32_t)s_cal.dig_H6) >> 10)
               * (((v * (int32_t)s_cal.dig_H3) >> 11) + 32768)) >> 10)
              + 2097152) * (int32_t)s_cal.dig_H2 + 8192) >> 14));
    v = v - (((((v >> 15) * (v >> 15)) >> 7) * (int32_t)s_cal.dig_H1) >> 4);
    if (v < 0)          v = 0;
    if (v > 419430400)  v = 419430400;
    return (uint32_t)(v >> 12);
}

/* -----------------------------------------------------------------------
 * Sensor configuration (called on init and after reset)
 * --------------------------------------------------------------------- */

static esp_err_t sensor_configure(void)
{
    ESP_RETURN_ON_ERROR(i2c_write_reg(REG_RESET, 0xB6), TAG, "reset");
    vTaskDelay(pdMS_TO_TICKS(10));
    ESP_RETURN_ON_ERROR(read_calibration(), TAG, "calibration");
    if (s_has_humidity)
        ESP_RETURN_ON_ERROR(i2c_write_reg(REG_CTRL_HUM, 0x01), TAG, "ctrl_hum");
    ESP_RETURN_ON_ERROR(i2c_write_reg(REG_CTRL_MEAS, 0x27), TAG, "ctrl_meas");
    ESP_RETURN_ON_ERROR(i2c_write_reg(REG_CONFIG,    0xA0), TAG, "config");
    return ESP_OK;
}

/* -----------------------------------------------------------------------
 * MQTT reset command  topic: {base}/sensor/bme280/reset  payload: any
 * --------------------------------------------------------------------- */

static void mqtt_reset_cb(const char *topic, const char *payload, int len)
{
    (void)topic; (void)payload; (void)len;
    ESP_LOGI(TAG, "Reset requested via MQTT");
    esp_err_t err = sensor_configure();
    if (err == ESP_OK)
        ESP_LOGI(TAG, "Sensor reset OK");
    else
        ESP_LOGW(TAG, "Sensor reset failed: %s", esp_err_to_name(err));
}

/* -----------------------------------------------------------------------
 * Publish task
 * --------------------------------------------------------------------- */

static void sensor_task(void *arg)
{
    mqtt_config_t mcfg;
    config_store_get_mqtt(&mcfg);

    char topic_temp[80], topic_hum[80];
    snprintf(topic_temp, sizeof(topic_temp), "%s/sensor/temperature", mcfg.topic);
    snprintf(topic_hum,  sizeof(topic_hum),  "%s/sensor/humidity",    mcfg.topic);

    /* BME280 data is 8 bytes (press+temp+hum), BMP280 is 6 bytes (press+temp) */
    const size_t data_len = s_has_humidity ? 8 : 6;

    for (;;) {
        uint8_t data[8] = {0};
        if (i2c_read_regs(REG_DATA, data, data_len) != ESP_OK) {
            ESP_LOGE(TAG, "sensor read failed");
            vTaskDelay(pdMS_TO_TICKS(PUBLISH_INTERVAL_MS));
            continue;
        }

        int32_t adc_T = ((int32_t)data[3] << 12) | ((int32_t)data[4] << 4) | (data[5] >> 4);

        int32_t t_fine;
        int32_t temp_raw = compensate_temperature(adc_T, &t_fine);

        int temp_int  = temp_raw / 100;
        int temp_frac = temp_raw % 100;
        if (temp_frac < 0) temp_frac = -temp_frac;

        char payload[16];
        snprintf(payload, sizeof(payload), "%d.%02d", temp_int, temp_frac);
        mqtt_bridge_publish(topic_temp, payload, 0, false);

        if (s_has_humidity) {
            int32_t  adc_H  = ((int32_t)data[6] << 8) | (int32_t)data[7];
            uint32_t hum_raw = compensate_humidity(adc_H, t_fine);
            uint32_t hum_int  = hum_raw / 1024;
            uint32_t hum_frac = (hum_raw % 1024) * 100 / 1024;

            snprintf(payload, sizeof(payload), "%lu.%02lu", hum_int, hum_frac);
            mqtt_bridge_publish(topic_hum, payload, 0, false);

            ESP_LOGI(TAG, "T=%d.%02d°C  H=%lu.%02lu%%", temp_int, temp_frac, hum_int, hum_frac);
        } else {
            ESP_LOGI(TAG, "T=%d.%02d°C  (no humidity — BMP280)", temp_int, temp_frac);
        }

        vTaskDelay(pdMS_TO_TICKS(PUBLISH_INTERVAL_MS));
    }
}

/* -----------------------------------------------------------------------
 * Public API
 * --------------------------------------------------------------------- */

esp_err_t bme280_sensor_init(void)
{
    /* I2C bus */
    i2c_config_t conf = {
        .mode             = I2C_MODE_MASTER,
        .sda_io_num       = I2C_SDA_GPIO,
        .scl_io_num       = I2C_SCL_GPIO,
        .sda_pullup_en    = GPIO_PULLUP_ENABLE,
        .scl_pullup_en    = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_CLK_HZ,
    };
    ESP_RETURN_ON_ERROR(i2c_param_config(I2C_PORT, &conf), TAG, "i2c param");
    ESP_RETURN_ON_ERROR(i2c_driver_install(I2C_PORT, I2C_MODE_MASTER, 0, 0, 0),
                        TAG, "i2c install");

    /* Auto-detect I2C address */
    bool found = false;
    const uint8_t candidates[] = { BME280_ADDR_LOW, BME280_ADDR_HIGH };
    for (int i = 0; i < 2; i++) {
        if (i2c_probe(candidates[i]) == ESP_OK) {
            s_addr = candidates[i];
            found  = true;
            break;
        }
        ESP_LOGW(TAG, "No response at 0x%02X", candidates[i]);
    }
    if (!found) {
        ESP_LOGE(TAG, "No sensor found — scanning I2C bus (SDA=%d SCL=%d):", I2C_SDA_GPIO, I2C_SCL_GPIO);
        for (uint8_t addr = 0x08; addr < 0x78; addr++) {
            if (i2c_probe(addr) == ESP_OK)
                ESP_LOGE(TAG, "  device at 0x%02X", addr);
        }
        ESP_LOGE(TAG, "  scan done");
        return ESP_ERR_NOT_FOUND;
    }

    /* Identify chip */
    uint8_t chip_id;
    ESP_RETURN_ON_ERROR(i2c_read_regs(REG_ID, &chip_id, 1), TAG, "chip id read");

    const char *chip_name;
    if (chip_id == BME280_CHIP_ID) {
        chip_name     = "BME280";
        s_has_humidity = true;
    } else if (chip_id == BMP280_CHIP_ID_A ||
               chip_id == BMP280_CHIP_ID_B ||
               chip_id == BMP280_CHIP_ID_C) {
        chip_name     = "BMP280";
        s_has_humidity = false;
    } else {
        ESP_LOGE(TAG, "Unknown chip ID 0x%02X at 0x%02X", chip_id, s_addr);
        return ESP_ERR_NOT_FOUND;
    }

    /* Reset + configure */
    ESP_RETURN_ON_ERROR(sensor_configure(), TAG, "sensor configure");

    /* Subscribe to MQTT reset command */
    mqtt_config_t mcfg;
    config_store_get_mqtt(&mcfg);
    char reset_topic[80];
    snprintf(reset_topic, sizeof(reset_topic), "%s/sensor/bme280/reset", mcfg.topic);
    mqtt_bridge_subscribe(reset_topic, mqtt_reset_cb);

    xTaskCreate(sensor_task, "bme280", 3072, NULL, 3, NULL);
    ESP_LOGI(TAG, "%s initialised (addr=0x%02X, SDA=%d, SCL=%d, humidity=%s, reset topic=%s)",
             chip_name, s_addr, I2C_SDA_GPIO, I2C_SCL_GPIO,
             s_has_humidity ? "yes" : "no", reset_topic);
    return ESP_OK;
}
