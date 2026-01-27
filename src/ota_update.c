/**
 * OTA Update - Over-the-air firmware updates via HTTP
 *
 * Checks for new firmware version and updates if available.
 */

#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"

#include "ota_update.h"
#include "config.h"

static const char *TAG = "ota";

const char* ota_get_version(void)
{
    return FIRMWARE_VERSION;
}

/**
 * Compare version strings (e.g., "1.0.0" vs "1.0.1")
 * Returns: >0 if v1 > v2, <0 if v1 < v2, 0 if equal
 */
static int compare_versions(const char *v1, const char *v2)
{
    int major1, minor1, patch1;
    int major2, minor2, patch2;

    sscanf(v1, "%d.%d.%d", &major1, &minor1, &patch1);
    sscanf(v2, "%d.%d.%d", &major2, &minor2, &patch2);

    if (major1 != major2) return major1 - major2;
    if (minor1 != minor2) return minor1 - minor2;
    return patch1 - patch2;
}

/**
 * Check if a new version is available
 */
bool ota_check_for_update(char *new_version, size_t max_len)
{
#if !OTA_ENABLED
    return false;
#endif

    ESP_LOGI(TAG, "Checking for updates...");

    esp_http_client_config_t http_cfg = {
        .url = OTA_VERSION_URL,
        .timeout_ms = 10000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&http_cfg);
    if (client == NULL) {
        ESP_LOGE(TAG, "Failed to init HTTP client");
        return false;
    }

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to connect: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return false;
    }

    int content_length = esp_http_client_fetch_headers(client);
    if (content_length <= 0 || content_length > 32) {
        ESP_LOGW(TAG, "Invalid version response length: %d", content_length);
        esp_http_client_cleanup(client);
        return false;
    }

    char version_buf[33] = {0};
    int read_len = esp_http_client_read(client, version_buf, sizeof(version_buf) - 1);
    esp_http_client_cleanup(client);

    if (read_len <= 0) {
        ESP_LOGW(TAG, "Failed to read version");
        return false;
    }

    // Trim whitespace
    char *p = version_buf;
    while (*p && (*p == ' ' || *p == '\n' || *p == '\r')) p++;
    char *end = p + strlen(p) - 1;
    while (end > p && (*end == ' ' || *end == '\n' || *end == '\r')) *end-- = '\0';

    ESP_LOGI(TAG, "Current: %s, Available: %s", FIRMWARE_VERSION, p);

    if (compare_versions(p, FIRMWARE_VERSION) > 0) {
        ESP_LOGI(TAG, "New version available!");
        if (new_version && max_len > 0) {
            strncpy(new_version, p, max_len - 1);
            new_version[max_len - 1] = '\0';
        }
        return true;
    }

    ESP_LOGI(TAG, "Firmware is up to date");
    return false;
}

/**
 * Perform OTA update
 */
esp_err_t ota_perform_update(void)
{
#if !OTA_ENABLED
    return ESP_ERR_NOT_SUPPORTED;
#endif

    ESP_LOGI(TAG, "Starting OTA update from %s", OTA_FIRMWARE_URL);

    esp_http_client_config_t http_config = {
        .url = OTA_FIRMWARE_URL,
        .timeout_ms = 60000,
        .keep_alive_enable = true,
    };

    esp_https_ota_config_t ota_config = {
        .http_config = &http_config,
    };

    esp_err_t ret = esp_https_ota(&ota_config);

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "OTA update successful! Rebooting in 3 seconds...");
        vTaskDelay(pdMS_TO_TICKS(3000));
        esp_restart();
    } else {
        ESP_LOGE(TAG, "OTA update failed: %s", esp_err_to_name(ret));
    }

    return ret;
}
