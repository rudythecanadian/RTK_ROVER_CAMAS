/**
 * RGB LED Status Indicator (WS2812 on GPIO 2)
 */

#ifndef LED_H
#define LED_H

#include "esp_err.h"

// LED Colors
typedef enum {
    LED_OFF = 0,
    LED_RED,        // Error / No connection
    LED_ORANGE,     // Waiting / Partial connection
    LED_YELLOW,     // UBX data (no RTCM)
    LED_GREEN,      // 100% RTCM flowing
    LED_BLUE,       // WiFi connecting
    LED_PURPLE,     // NTRIP connecting
    LED_WHITE,      // Startup
    LED_CYAN,       // Mixed data (some RTCM)
} led_color_t;

/**
 * Initialize the RGB LED
 */
esp_err_t led_init(void);

/**
 * Set LED to a solid color
 */
void led_set_color(led_color_t color);

/**
 * Set LED with custom RGB values (0-255 each)
 */
void led_set_rgb(uint8_t r, uint8_t g, uint8_t b);

/**
 * Pulse/flash the LED (non-blocking, call periodically)
 */
void led_pulse(led_color_t color);

/**
 * Update LED based on data statistics
 * rtcm_percent: 0-100 indicating what percent of data is RTCM
 */
void led_update_data_status(int rtcm_percent, bool wifi_ok, bool ntrip_ok);

#endif // LED_H
