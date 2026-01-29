/**
 * WiFi Connection Handler
 */

#ifndef WIFI_H
#define WIFI_H

#include "esp_err.h"
#include <stdbool.h>

/**
 * Initialize WiFi in station mode and connect to configured AP
 * Blocks until connected or max retries exceeded
 */
esp_err_t wifi_init_sta(void);

/**
 * Check if WiFi is currently connected
 */
bool wifi_is_connected(void);

/**
 * Get the SSID of the currently connected network
 * Returns empty string if not connected
 */
const char* wifi_get_ssid(void);

#endif // WIFI_H
