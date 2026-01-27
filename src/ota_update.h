/**
 * OTA Update - Over-the-air firmware updates
 */

#ifndef OTA_UPDATE_H
#define OTA_UPDATE_H

#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

/**
 * Get current firmware version string
 */
const char* ota_get_version(void);

/**
 * Check if a new firmware version is available
 * @param new_version Buffer to store new version string (optional, can be NULL)
 * @param max_len Size of new_version buffer
 * @return true if update available, false otherwise
 */
bool ota_check_for_update(char *new_version, size_t max_len);

/**
 * Perform OTA update from configured URL
 * Will reboot on success
 * @return ESP_OK on success (never returns), error code on failure
 */
esp_err_t ota_perform_update(void);

#endif // OTA_UPDATE_H
