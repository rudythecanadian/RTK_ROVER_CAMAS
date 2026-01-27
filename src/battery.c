/**
 * Battery Monitor - MAX17048 Fuel Gauge (I2C)
 *
 * The SparkFun Thing Plus ESP32 WROOM USB-C has a MAX17048 fuel gauge
 * on the I2C bus (same bus as Qwiic connector).
 *
 * MAX17048 I2C Address: 0x36
 * Registers:
 *   0x02-0x03: VCELL (voltage)
 *   0x04-0x05: SOC (state of charge)
 *   0x06-0x07: MODE
 *   0x08-0x09: VERSION
 *   0x0C-0x0D: CONFIG
 */

#include <stdio.h>
#include "esp_log.h"
#include "driver/i2c.h"

#include "battery.h"
#include "config.h"

static const char *TAG = "battery";

// MAX17048 registers
#define MAX17048_VCELL    0x02  // Voltage (12-bit, units of 1.25mV)
#define MAX17048_SOC      0x04  // State of charge (%)
#define MAX17048_MODE     0x06
#define MAX17048_VERSION  0x08
#define MAX17048_CONFIG   0x0C

static bool initialized = false;

/**
 * Read 16-bit register from MAX17048
 */
static esp_err_t max17048_read_reg(uint8_t reg, uint16_t *value)
{
    uint8_t data[2];

    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (MAX17048_I2C_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (MAX17048_I2C_ADDR << 1) | I2C_MASTER_READ, true);
    i2c_master_read(cmd, data, 2, I2C_MASTER_LAST_NACK);
    i2c_master_stop(cmd);

    esp_err_t ret = i2c_master_cmd_begin(I2C_MASTER_NUM, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);

    if (ret == ESP_OK) {
        *value = (data[0] << 8) | data[1];
    }

    return ret;
}

esp_err_t battery_init(void)
{
#if !BATTERY_USE_MAX17048
    ESP_LOGW(TAG, "Battery monitoring disabled");
    return ESP_OK;
#endif

    ESP_LOGI(TAG, "Initializing MAX17048 fuel gauge (I2C addr 0x%02X)", MAX17048_I2C_ADDR);

    // I2C should already be initialized by zed_rover_init()
    // Just verify we can communicate with the MAX17048

    uint16_t version;
    esp_err_t ret = max17048_read_reg(MAX17048_VERSION, &version);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to communicate with MAX17048: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "MAX17048 version: 0x%04X", version);
    initialized = true;

    return ESP_OK;
}

float battery_get_voltage(void)
{
    if (!initialized) {
        return 0.0f;
    }

    uint16_t vcell;
    if (max17048_read_reg(MAX17048_VCELL, &vcell) != ESP_OK) {
        return 0.0f;
    }

    // VCELL is 12-bit value in upper bits, units of 1.25mV
    // Shift right by 4 to get 12-bit value, then multiply by 1.25mV
    float voltage = ((vcell >> 4) * 1.25f) / 1000.0f;

    return voltage;
}

int battery_get_percentage(void)
{
    if (!initialized) {
        return -1;
    }

    uint16_t soc;
    if (max17048_read_reg(MAX17048_SOC, &soc) != ESP_OK) {
        return -1;
    }

    // SOC is in units of 1/256%
    // High byte is integer part, low byte is fractional
    int percentage = soc >> 8;

    // Clamp to 0-100
    if (percentage > 100) percentage = 100;
    if (percentage < 0) percentage = 0;

    return percentage;
}
