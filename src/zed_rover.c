/**
 * ZED-X20P I2C Driver (Rover Mode)
 */

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c.h"
#include "esp_log.h"

#include "zed_rover.h"
#include "config.h"

static const char *TAG = "zed_rover";

// u-blox register addresses
#define UBX_REG_DATA_LEN_H  0xFD  // High byte of data length
#define UBX_REG_DATA_LEN_L  0xFE  // Low byte of data length
#define UBX_REG_DATA        0xFF  // Data stream register

// UBX protocol constants
#define UBX_SYNC1 0xB5
#define UBX_SYNC2 0x62

// UBX message classes
#define UBX_CLASS_NAV 0x01
#define UBX_CLASS_CFG 0x06

// UBX message IDs
#define UBX_NAV_PVT 0x07

// I2C timeout
#define I2C_TIMEOUT_MS 100

// Buffer for parsing UBX messages
static uint8_t ubx_buffer[256];
static int ubx_buffer_len = 0;

/**
 * Calculate UBX checksum
 */
static void ubx_checksum(const uint8_t *data, size_t len, uint8_t *ck_a, uint8_t *ck_b)
{
    *ck_a = 0;
    *ck_b = 0;
    for (size_t i = 0; i < len; i++) {
        *ck_a += data[i];
        *ck_b += *ck_a;
    }
}

esp_err_t zed_rover_init(void)
{
    ESP_LOGI(TAG, "Initializing I2C for ZED-X20P (Rover)...");

    // Configure I2C
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_MASTER_SDA_IO,
        .scl_io_num = I2C_MASTER_SCL_IO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_MASTER_FREQ_HZ,
    };

    esp_err_t ret = i2c_param_config(I2C_MASTER_NUM, &conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C param config failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = i2c_driver_install(I2C_MASTER_NUM, conf.mode, 0, 0, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C driver install failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // Scan I2C bus to find devices
    ESP_LOGI(TAG, "Scanning I2C bus (SDA=%d, SCL=%d)...", I2C_MASTER_SDA_IO, I2C_MASTER_SCL_IO);
    int devices_found = 0;
    for (uint8_t addr = 0x08; addr < 0x78; addr++) {
        uint8_t dummy;
        esp_err_t result = i2c_master_read_from_device(I2C_MASTER_NUM, addr, &dummy, 1, pdMS_TO_TICKS(10));
        if (result == ESP_OK) {
            ESP_LOGI(TAG, "  Found device at address 0x%02X", addr);
            devices_found++;
        }
    }
    if (devices_found == 0) {
        ESP_LOGW(TAG, "  No I2C devices found! Check QWIIC cable connection.");
    } else {
        ESP_LOGI(TAG, "  Total: %d device(s) found", devices_found);
    }

    // Verify communication by checking available bytes
    vTaskDelay(pdMS_TO_TICKS(100));

    int avail = zed_rover_available();
    if (avail < 0) {
        ESP_LOGE(TAG, "ZED-X20P not responding on I2C address 0x%02X", ZED_I2C_ADDR);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "ZED-X20P detected, %d bytes available", avail);
    return ESP_OK;
}

int zed_rover_available(void)
{
    uint8_t len_bytes[2];

    esp_err_t ret = i2c_master_write_read_device(I2C_MASTER_NUM, ZED_I2C_ADDR,
        (uint8_t[]){UBX_REG_DATA_LEN_H}, 1, len_bytes, 2,
        pdMS_TO_TICKS(I2C_TIMEOUT_MS));

    if (ret != ESP_OK) {
        return -1;
    }

    int available = (len_bytes[0] << 8) | len_bytes[1];

    // 0xFFFF means no data or error
    if (available == 0xFFFF) {
        return 0;
    }

    return available;
}

int zed_rover_read(uint8_t *buffer, size_t max_len)
{
    int available = zed_rover_available();
    if (available <= 0) {
        return available;
    }

    size_t to_read = (available < max_len) ? available : max_len;

    uint8_t reg = UBX_REG_DATA;
    esp_err_t ret = i2c_master_write_read_device(I2C_MASTER_NUM, ZED_I2C_ADDR,
        &reg, 1, buffer, to_read,
        pdMS_TO_TICKS(I2C_TIMEOUT_MS));

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read data: %s", esp_err_to_name(ret));
        return -1;
    }

    return to_read;
}

int zed_rover_write_rtcm(const uint8_t *data, size_t len)
{
    if (len == 0) return 0;

    // Write directly to the data register (0xFF)
    // u-blox receivers accept raw RTCM data written to I2C
    esp_err_t ret = i2c_master_write_to_device(I2C_MASTER_NUM, ZED_I2C_ADDR,
        data, len,
        pdMS_TO_TICKS(I2C_TIMEOUT_MS));

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write RTCM: %s", esp_err_to_name(ret));
        return -1;
    }

    return len;
}

bool zed_rover_get_position(zed_position_t *pos)
{
    if (pos == NULL) return false;

    memset(pos, 0, sizeof(zed_position_t));

    // Read available data
    int avail = zed_rover_available();
    if (avail <= 0) {
        return false;
    }

    // Read into buffer
    uint8_t temp_buf[256];
    int read_len = zed_rover_read(temp_buf, sizeof(temp_buf));
    if (read_len <= 0) {
        return false;
    }

    // Append to our parsing buffer
    int copy_len = (read_len < (int)(sizeof(ubx_buffer) - ubx_buffer_len))
                   ? read_len : (sizeof(ubx_buffer) - ubx_buffer_len);
    memcpy(ubx_buffer + ubx_buffer_len, temp_buf, copy_len);
    ubx_buffer_len += copy_len;

    // Search for NAV-PVT message in buffer
    for (int i = 0; i < ubx_buffer_len - 8; i++) {
        if (ubx_buffer[i] == UBX_SYNC1 && ubx_buffer[i+1] == UBX_SYNC2) {
            uint8_t msg_class = ubx_buffer[i+2];
            uint8_t msg_id = ubx_buffer[i+3];
            uint16_t payload_len = ubx_buffer[i+4] | (ubx_buffer[i+5] << 8);

            // Check if we have the complete message
            int msg_total_len = 6 + payload_len + 2;  // header + payload + checksum
            if (i + msg_total_len > ubx_buffer_len) {
                // Incomplete message, wait for more data
                break;
            }

            // Verify checksum
            uint8_t ck_a, ck_b;
            ubx_checksum(&ubx_buffer[i+2], 4 + payload_len, &ck_a, &ck_b);
            if (ck_a != ubx_buffer[i + 6 + payload_len] ||
                ck_b != ubx_buffer[i + 7 + payload_len]) {
                // Bad checksum, skip this byte
                continue;
            }

            // Check if it's NAV-PVT (class 0x01, id 0x07, length 92)
            if (msg_class == UBX_CLASS_NAV && msg_id == UBX_NAV_PVT && payload_len == 92) {
                uint8_t *p = &ubx_buffer[i + 6];  // Start of payload

                // Parse NAV-PVT payload
                // Bytes 0-3: iTOW (ignored)
                pos->year = p[4] | (p[5] << 8);
                pos->month = p[6];
                pos->day = p[7];
                pos->hour = p[8];
                pos->min = p[9];
                pos->sec = p[10];
                // Byte 11: valid flags
                uint8_t valid_flags = p[11];

                // Bytes 20: fixType
                pos->fix_type = p[20];
                // Byte 21: flags (includes carrSoln in bits 6-7)
                uint8_t flags = p[21];
                pos->carr_soln = (flags >> 6) & 0x03;

                // Byte 23: numSV
                pos->num_sv = p[23];

                // Bytes 24-27: lon (1e-7 degrees)
                int32_t lon_raw = p[24] | (p[25] << 8) | (p[26] << 16) | (p[27] << 24);
                pos->longitude = lon_raw * 1e-7;

                // Bytes 28-31: lat (1e-7 degrees)
                int32_t lat_raw = p[28] | (p[29] << 8) | (p[30] << 16) | (p[31] << 24);
                pos->latitude = lat_raw * 1e-7;

                // Bytes 36-39: hMSL (mm)
                int32_t alt_raw = p[36] | (p[37] << 8) | (p[38] << 16) | (p[39] << 24);
                pos->altitude_msl = alt_raw / 1000.0;

                // Bytes 40-43: hAcc (mm)
                uint32_t h_acc_raw = p[40] | (p[41] << 8) | (p[42] << 16) | (p[43] << 24);
                pos->h_acc = h_acc_raw / 1000.0f;

                // Bytes 44-47: vAcc (mm)
                uint32_t v_acc_raw = p[44] | (p[45] << 8) | (p[46] << 16) | (p[47] << 24);
                pos->v_acc = v_acc_raw / 1000.0f;

                pos->valid = (valid_flags & 0x01) && (pos->fix_type >= 2);

                // Remove parsed message from buffer
                int remaining = ubx_buffer_len - (i + msg_total_len);
                if (remaining > 0) {
                    memmove(ubx_buffer, &ubx_buffer[i + msg_total_len], remaining);
                }
                ubx_buffer_len = remaining;

                return true;
            }

            // Skip this message
            int remaining = ubx_buffer_len - (i + msg_total_len);
            if (remaining > 0) {
                memmove(ubx_buffer, &ubx_buffer[i + msg_total_len], remaining);
            }
            ubx_buffer_len = remaining;
            i = -1;  // Restart search from beginning
        }
    }

    // If buffer is getting full with no valid messages, clear it
    if (ubx_buffer_len > 200) {
        ubx_buffer_len = 0;
    }

    return false;
}

const char* zed_rover_fix_type_str(uint8_t fix_type, uint8_t carr_soln)
{
    if (carr_soln == 2) return "RTK FIXED";
    if (carr_soln == 1) return "RTK FLOAT";

    switch (fix_type) {
        case 0: return "No Fix";
        case 1: return "Dead Reckoning";
        case 2: return "2D Fix";
        case 3: return "3D Fix";
        case 4: return "GNSS + DR";
        case 5: return "Time Only";
        default: return "Unknown";
    }
}
