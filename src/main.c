/**
 * RTK Rover - Camas Base Station Client
 *
 * Receives RTCM corrections from NTRIP caster, forwards to ZED-X20P,
 * and outputs high-precision RTK position.
 *
 * Hardware:
 *   - SparkFun ESP32 WROOM (QWIIC)
 *   - u-blox ZED-X20P (connected via I2C)
 *
 * Framework: ESP-IDF
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"

#include "config.h"
#include "wifi.h"
#include "ntrip_client.h"
#include "zed_rover.h"
#include "dashboard_client.h"
#include "battery.h"
#include "ota_update.h"
#include "led.h"

static const char *TAG = "main";

// Statistics
static uint32_t rtcm_bytes_received = 0;
static uint32_t rtcm_bytes_sent = 0;
static uint32_t position_count = 0;
static uint32_t fixed_count = 0;
static uint32_t float_count = 0;

// Buffer for RTCM data
#define RTCM_BUFFER_SIZE 1024
static uint8_t rtcm_buffer[RTCM_BUFFER_SIZE];

/**
 * Print position report
 */
static void print_position(const zed_position_t *pos)
{
    ESP_LOGI(TAG, "============================================================");
    ESP_LOGI(TAG, "[%02d:%02d:%02d UTC] %s",
             pos->hour, pos->min, pos->sec,
             zed_rover_fix_type_str(pos->fix_type, pos->carr_soln));
    ESP_LOGI(TAG, "  Lat: %.9f  Lon: %.9f", pos->latitude, pos->longitude);
    ESP_LOGI(TAG, "  Alt: %.3f m MSL", pos->altitude_msl);
    ESP_LOGI(TAG, "  hAcc: %.3f m  vAcc: %.3f m  Sats: %d",
             pos->h_acc, pos->v_acc, pos->num_sv);
    ESP_LOGI(TAG, "  RTCM: %lu bytes rx, %lu bytes tx",
             (unsigned long)rtcm_bytes_received, (unsigned long)rtcm_bytes_sent);

    // RTK statistics
    uint32_t rtk_total = fixed_count + float_count;
    float fixed_pct = (rtk_total > 0) ? (100.0f * fixed_count / rtk_total) : 0.0f;

    // Status indicator
    if (pos->carr_soln == 2) {
        ESP_LOGI(TAG, "  *** RTK FIXED - cm-level accuracy ***");
        ESP_LOGI(TAG, "  Fixed rate: %.1f%% (%lu/%lu)", fixed_pct,
                 (unsigned long)fixed_count, (unsigned long)rtk_total);
    } else if (pos->carr_soln == 1) {
        ESP_LOGI(TAG, "  RTK Float - converging...");
        ESP_LOGI(TAG, "  Fixed rate: %.1f%% (%lu/%lu)", fixed_pct,
                 (unsigned long)fixed_count, (unsigned long)rtk_total);
    } else if (!ntrip_client_is_connected()) {
        ESP_LOGW(TAG, "  [NO NTRIP CONNECTION]");
    }
}

/**
 * Main rover task
 */
static void rover_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Rover task started");

    TickType_t last_ntrip_attempt = 0;
    TickType_t last_position_report = 0;
    TickType_t last_led_time = 0;
    const TickType_t ntrip_retry_interval = pdMS_TO_TICKS(NTRIP_RECONNECT_INTERVAL_MS);
    const TickType_t position_interval = pdMS_TO_TICKS(POSITION_REPORT_INTERVAL_MS);
    const TickType_t led_interval = pdMS_TO_TICKS(50);  // 50ms for smooth pulsing

    zed_position_t pos;
    uint8_t last_carr_soln = 0;

    while (1) {
        bool wifi_ok = wifi_is_connected();
        bool ntrip_ok = ntrip_client_is_connected();

        // Check for stale NTRIP connection and force reconnect
        ntrip_client_check_stale();
        ntrip_ok = ntrip_client_is_connected();

        // Maintain NTRIP connection
        if (!ntrip_ok && wifi_ok) {
            TickType_t now = xTaskGetTickCount();
            if ((now - last_ntrip_attempt) >= ntrip_retry_interval) {
                ESP_LOGI(TAG, "Connecting to NTRIP caster...");
                if (ntrip_client_connect() == ESP_OK) {
                    ntrip_ok = true;
                }
                last_ntrip_attempt = now;
            }
        }

        // Receive RTCM from NTRIP and forward to ZED-X20P
        if (ntrip_ok) {
            int received = ntrip_client_receive(rtcm_buffer, RTCM_BUFFER_SIZE);
            if (received > 0) {
                rtcm_bytes_received += received;

                // Forward to ZED-X20P
                int sent = zed_rover_write_rtcm(rtcm_buffer, received);
                if (sent > 0) {
                    rtcm_bytes_sent += sent;
                }
            } else if (received < 0) {
                // Connection lost
                ntrip_ok = false;
            }
        }

        // Get position from ZED-X20P
        if (zed_rover_get_position(&pos)) {
            position_count++;

            // Track RTK solution type
            if (pos.carr_soln == 2) {
                fixed_count++;
            } else if (pos.carr_soln == 1) {
                float_count++;
            }

            last_carr_soln = pos.carr_soln;

            // Report position periodically
            TickType_t now = xTaskGetTickCount();
            if ((now - last_position_report) >= position_interval) {
                print_position(&pos);
                last_position_report = now;

                // Send to dashboard
#if DASHBOARD_ENABLED
                int battery_pct = battery_get_percentage();
                dashboard_send_position(&pos, rtcm_bytes_received,
                                        fixed_count, float_count, battery_pct);
#endif
            }
        }

        // Update LED status
        if ((xTaskGetTickCount() - last_led_time) >= led_interval) {
            last_led_time = xTaskGetTickCount();
            bool ntrip_stale = ntrip_client_is_stale();

            if (!wifi_ok) {
                led_pulse(LED_BLUE);           // Blue pulse = WiFi connecting
            } else if (!ntrip_ok) {
                led_pulse(LED_PURPLE);         // Purple pulse = NTRIP connecting
            } else if (ntrip_stale) {
                led_pulse(LED_RED);            // Red pulse = stale connection
            } else if (last_carr_soln == 2) {
                led_set_color(LED_GREEN);      // Solid green = RTK Fixed
            } else if (last_carr_soln == 1) {
                led_pulse(LED_CYAN);           // Cyan pulse = RTK Float
            } else {
                led_set_color(LED_YELLOW);     // Yellow = 3D fix, no RTK
            }
        }

        // Small delay to prevent tight loop
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

/**
 * OTA update check task
 */
static void ota_check_task(void *pvParameters)
{
    // Wait for WiFi to be ready and initial startup to complete
    vTaskDelay(pdMS_TO_TICKS(30000));  // Wait 30 seconds after boot

    ESP_LOGI(TAG, "OTA check task started (interval: %d min)", OTA_CHECK_INTERVAL_MS / 60000);

    while (1) {
        if (wifi_is_connected()) {
            char new_version[16];
            if (ota_check_for_update(new_version, sizeof(new_version))) {
                ESP_LOGI(TAG, "New firmware %s available, updating...", new_version);
                ota_perform_update();
                // If we get here, update failed - wait before retrying
                vTaskDelay(pdMS_TO_TICKS(60000));
            }
        }
        vTaskDelay(pdMS_TO_TICKS(OTA_CHECK_INTERVAL_MS));
    }
}

/**
 * Main application entry point
 */
void app_main(void)
{
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "   RTK Rover - Camas Base Client");
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "NTRIP Server: %s:%d", NTRIP_HOST, NTRIP_PORT);
    ESP_LOGI(TAG, "Mountpoint: %s", NTRIP_MOUNTPOINT);
    ESP_LOGI(TAG, "");

    // Initialize LED first for visual feedback
    ESP_LOGI(TAG, "Initializing RGB LED...");
    if (led_init() != ESP_OK) {
        ESP_LOGW(TAG, "LED initialization failed - continuing without status LED");
    } else {
        led_set_color(LED_WHITE);  // White = startup
    }

    // Initialize WiFi
    ESP_LOGI(TAG, "Initializing WiFi...");
    led_set_color(LED_BLUE);  // Blue = WiFi connecting
    if (wifi_init_sta() != ESP_OK) {
        ESP_LOGE(TAG, "WiFi initialization failed!");
        // Continue anyway - will retry
    }

    // Initialize ZED-X20P (also initializes I2C bus)
    ESP_LOGI(TAG, "Initializing ZED-X20P...");
    if (zed_rover_init() != ESP_OK) {
        ESP_LOGE(TAG, "ZED-X20P initialization failed!");
        ESP_LOGE(TAG, "Check I2C connection and power");
        // Continue anyway - might recover
    }

    // Initialize battery monitoring (requires I2C to be initialized first)
    ESP_LOGI(TAG, "Initializing battery monitor...");
    if (battery_init() != ESP_OK) {
        ESP_LOGW(TAG, "Battery init failed - continuing without battery monitoring");
    } else {
        ESP_LOGI(TAG, "Battery: %d%% (%.2fV)", battery_get_percentage(), battery_get_voltage());
    }

    // Connect to NTRIP caster
    ESP_LOGI(TAG, "Connecting to NTRIP caster...");
    if (ntrip_client_connect() != ESP_OK) {
        ESP_LOGW(TAG, "Initial NTRIP connection failed - will retry");
    }

    // Start rover task
    xTaskCreate(rover_task, "rover_task", 8192, NULL, 5, NULL);

    // Start OTA check task
    xTaskCreate(ota_check_task, "ota_check", 8192, NULL, 3, NULL);

    ESP_LOGI(TAG, "Rover running! Firmware v%s", ota_get_version());
}
