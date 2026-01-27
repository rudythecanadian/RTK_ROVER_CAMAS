/**
 * Battery Monitor - ADC-based battery voltage reading
 */

#ifndef BATTERY_H
#define BATTERY_H

#include "esp_err.h"

/**
 * Initialize battery ADC
 */
esp_err_t battery_init(void);

/**
 * Get battery voltage in volts
 */
float battery_get_voltage(void);

/**
 * Get battery percentage (0-100)
 * Based on LiPo discharge curve: 4.2V = 100%, 3.0V = 0%
 */
int battery_get_percentage(void);

#endif // BATTERY_H
