/**
 * RGB LED Status Indicator (WS2812 on GPIO 2)
 */

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/rmt_tx.h"
#include "esp_log.h"

#include "led.h"

static const char *TAG = "led";

// WS2812 LED on SparkFun Thing Plus ESP32
#define LED_GPIO 2
#define LED_RMT_RES_HZ 10000000  // 10MHz = 100ns resolution

// WS2812 timing (in RMT ticks at 10MHz)
#define WS2812_T0H 3   // 0.3us
#define WS2812_T0L 9   // 0.9us
#define WS2812_T1H 9   // 0.9us
#define WS2812_T1L 3   // 0.3us

static rmt_channel_handle_t led_channel = NULL;
static rmt_encoder_handle_t led_encoder = NULL;

// Current LED state
static uint8_t current_r = 0, current_g = 0, current_b = 0;

/**
 * WS2812 byte encoder callback
 */
static size_t ws2812_encode(rmt_encoder_t *encoder, rmt_channel_handle_t channel,
                            const void *primary_data, size_t data_size,
                            rmt_encode_state_t *ret_state)
{
    // Simple implementation - encode GRB bytes
    rmt_encoder_handle_t copy_encoder = NULL;
    rmt_copy_encoder_config_t copy_config = {};
    rmt_new_copy_encoder(&copy_config, &copy_encoder);

    const uint8_t *data = (const uint8_t *)primary_data;
    static rmt_symbol_word_t symbols[24];  // 8 bits * 3 colors

    size_t symbol_idx = 0;
    for (int byte_idx = 0; byte_idx < 3 && byte_idx < data_size; byte_idx++) {
        uint8_t byte = data[byte_idx];
        for (int bit = 7; bit >= 0; bit--) {
            if (byte & (1 << bit)) {
                // Send '1' bit
                symbols[symbol_idx].level0 = 1;
                symbols[symbol_idx].duration0 = WS2812_T1H;
                symbols[symbol_idx].level1 = 0;
                symbols[symbol_idx].duration1 = WS2812_T1L;
            } else {
                // Send '0' bit
                symbols[symbol_idx].level0 = 1;
                symbols[symbol_idx].duration0 = WS2812_T0H;
                symbols[symbol_idx].level1 = 0;
                symbols[symbol_idx].duration1 = WS2812_T0L;
            }
            symbol_idx++;
        }
    }

    size_t encoded = copy_encoder->encode(copy_encoder, channel, symbols,
                                           symbol_idx * sizeof(rmt_symbol_word_t),
                                           ret_state);
    rmt_del_encoder(copy_encoder);

    *ret_state = RMT_ENCODING_COMPLETE;
    return encoded;
}

esp_err_t led_init(void)
{
    ESP_LOGI(TAG, "Initializing RGB LED on GPIO %d", LED_GPIO);

    // Configure RMT TX channel
    rmt_tx_channel_config_t tx_config = {
        .gpio_num = LED_GPIO,
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = LED_RMT_RES_HZ,
        .mem_block_symbols = 64,
        .trans_queue_depth = 4,
    };

    esp_err_t ret = rmt_new_tx_channel(&tx_config, &led_channel);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create RMT TX channel: %s", esp_err_to_name(ret));
        return ret;
    }

    // Create simple copy encoder (we'll encode manually)
    rmt_copy_encoder_config_t encoder_config = {};
    ret = rmt_new_copy_encoder(&encoder_config, &led_encoder);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create encoder: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = rmt_enable(led_channel);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable RMT channel: %s", esp_err_to_name(ret));
        return ret;
    }

    // Start with LED off
    led_set_color(LED_OFF);

    ESP_LOGI(TAG, "RGB LED initialized");
    return ESP_OK;
}

void led_set_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    if (led_channel == NULL) return;

    current_r = r;
    current_g = g;
    current_b = b;

    // WS2812 uses GRB order
    uint8_t grb[3] = {g, r, b};

    // Encode and transmit
    rmt_symbol_word_t symbols[24];
    size_t symbol_idx = 0;

    for (int byte_idx = 0; byte_idx < 3; byte_idx++) {
        uint8_t byte = grb[byte_idx];
        for (int bit = 7; bit >= 0; bit--) {
            if (byte & (1 << bit)) {
                symbols[symbol_idx].level0 = 1;
                symbols[symbol_idx].duration0 = WS2812_T1H;
                symbols[symbol_idx].level1 = 0;
                symbols[symbol_idx].duration1 = WS2812_T1L;
            } else {
                symbols[symbol_idx].level0 = 1;
                symbols[symbol_idx].duration0 = WS2812_T0H;
                symbols[symbol_idx].level1 = 0;
                symbols[symbol_idx].duration1 = WS2812_T0L;
            }
            symbol_idx++;
        }
    }

    rmt_transmit_config_t tx_config = {
        .loop_count = 0,
    };

    rmt_transmit(led_channel, led_encoder, symbols, sizeof(symbols), &tx_config);
    rmt_tx_wait_all_done(led_channel, portMAX_DELAY);
}

void led_set_color(led_color_t color)
{
    // Brightness reduced to not be blinding (max ~50)
    switch (color) {
        case LED_OFF:
            led_set_rgb(0, 0, 0);
            break;
        case LED_RED:
            led_set_rgb(50, 0, 0);
            break;
        case LED_ORANGE:
            led_set_rgb(50, 25, 0);
            break;
        case LED_YELLOW:
            led_set_rgb(50, 50, 0);
            break;
        case LED_GREEN:
            led_set_rgb(0, 50, 0);
            break;
        case LED_BLUE:
            led_set_rgb(0, 0, 50);
            break;
        case LED_PURPLE:
            led_set_rgb(30, 0, 50);
            break;
        case LED_WHITE:
            led_set_rgb(40, 40, 40);
            break;
        case LED_CYAN:
            led_set_rgb(0, 40, 40);
            break;
    }
}

void led_pulse(led_color_t color)
{
    static int pulse_phase = 0;
    static bool increasing = true;

    // Get base color
    uint8_t base_r = 0, base_g = 0, base_b = 0;
    switch (color) {
        case LED_RED:    base_r = 50; break;
        case LED_ORANGE: base_r = 50; base_g = 25; break;
        case LED_YELLOW: base_r = 50; base_g = 50; break;
        case LED_GREEN:  base_g = 50; break;
        case LED_BLUE:   base_b = 50; break;
        case LED_PURPLE: base_r = 30; base_b = 50; break;
        case LED_CYAN:   base_g = 40; base_b = 40; break;
        default: break;
    }

    // Calculate brightness factor (0.2 to 1.0)
    float factor = 0.2f + (pulse_phase / 100.0f) * 0.8f;

    led_set_rgb((uint8_t)(base_r * factor),
                (uint8_t)(base_g * factor),
                (uint8_t)(base_b * factor));

    // Update phase
    if (increasing) {
        pulse_phase += 5;
        if (pulse_phase >= 100) increasing = false;
    } else {
        pulse_phase -= 5;
        if (pulse_phase <= 0) increasing = true;
    }
}

void led_update_data_status(int rtcm_percent, bool wifi_ok, bool ntrip_ok)
{
    if (!wifi_ok) {
        led_pulse(LED_BLUE);  // Pulsing blue = WiFi connecting
    } else if (!ntrip_ok) {
        led_pulse(LED_PURPLE);  // Pulsing purple = NTRIP connecting
    } else if (rtcm_percent >= 95) {
        led_set_color(LED_GREEN);  // Solid green = 100% RTCM
    } else if (rtcm_percent >= 50) {
        led_set_color(LED_CYAN);   // Cyan = mostly RTCM
    } else if (rtcm_percent > 0) {
        led_set_color(LED_YELLOW); // Yellow = some RTCM
    } else {
        led_pulse(LED_ORANGE);     // Pulsing orange = no RTCM yet
    }
}
