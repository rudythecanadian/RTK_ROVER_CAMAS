/**
 * ZED-X20P I2C Driver (Rover Mode)
 *
 * Handles:
 *   - Writing RTCM corrections to the receiver
 *   - Reading position/status from NAV-PVT messages
 */

#ifndef ZED_ROVER_H
#define ZED_ROVER_H

#include "esp_err.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/**
 * Position and status data from NAV-PVT
 */
typedef struct {
    // Time
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t min;
    uint8_t sec;

    // Fix info
    uint8_t fix_type;       // 0=none, 1=DR, 2=2D, 3=3D, 4=GNSS+DR, 5=time
    uint8_t carr_soln;      // 0=none, 1=float, 2=fixed
    uint8_t num_sv;         // Number of satellites used

    // Position (high precision)
    double latitude;        // degrees
    double longitude;       // degrees
    double altitude_msl;    // meters

    // Accuracy estimates
    float h_acc;            // horizontal accuracy (m)
    float v_acc;            // vertical accuracy (m)

    // Flags
    bool valid;             // Data is valid
} zed_position_t;

/**
 * Initialize I2C and verify ZED-X20P communication
 */
esp_err_t zed_rover_init(void);

/**
 * Write RTCM data to ZED-X20P
 * Returns number of bytes written, or -1 on error
 */
int zed_rover_write_rtcm(const uint8_t *data, size_t len);

/**
 * Get number of bytes available to read
 */
int zed_rover_available(void);

/**
 * Read raw data from ZED-X20P
 * Returns number of bytes read, or -1 on error
 */
int zed_rover_read(uint8_t *buffer, size_t max_len);

/**
 * Poll for position update (NAV-PVT)
 * Returns true if new position data available
 */
bool zed_rover_get_position(zed_position_t *pos);

/**
 * Get fix type as string
 */
const char* zed_rover_fix_type_str(uint8_t fix_type, uint8_t carr_soln);

#endif // ZED_ROVER_H
