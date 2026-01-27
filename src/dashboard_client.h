/**
 * Dashboard Client - Sends position data to web dashboard
 */

#ifndef DASHBOARD_CLIENT_H
#define DASHBOARD_CLIENT_H

#include "esp_err.h"
#include "zed_rover.h"

/**
 * Send position update to dashboard server
 * @param pos Position data from ZED-X20P
 * @param rtcm_bytes Total RTCM bytes received
 * @param fixed_count Number of RTK Fixed solutions
 * @param float_count Number of RTK Float solutions
 * @param battery_percentage Battery level 0-100 (-1 if unavailable)
 * @return ESP_OK on success
 */
esp_err_t dashboard_send_position(const zed_position_t *pos,
                                   uint32_t rtcm_bytes,
                                   uint32_t fixed_count,
                                   uint32_t float_count,
                                   int battery_percentage);

#endif // DASHBOARD_CLIENT_H
