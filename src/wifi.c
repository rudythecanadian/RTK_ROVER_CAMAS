/**
 * WiFi Connection Handler with Multi-Network Support
 *
 * Scans for available networks and connects to the strongest known network.
 * Automatically reconnects on signal loss, trying other networks if needed.
 */

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "wifi.h"
#include "config.h"

static const char *TAG = "wifi_multi";

// ============================================================================
// CONFIGURE YOUR WIFI NETWORKS HERE
// Networks are tried in order of signal strength (strongest first)
// ============================================================================
typedef struct {
    const char *ssid;
    const char *password;
} wifi_network_t;

static const wifi_network_t wifi_networks[] = {
    { "RudyTheCanadian", "BIG22slick" },  // iPhone hotspot (portable)
    { "Glasshouse2.4", "BIG22slick" },    // Home network
    // Add more networks here as needed
};

#define NUM_NETWORKS (sizeof(wifi_networks) / sizeof(wifi_networks[0]))

// ============================================================================

// Event group bits
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1
#define WIFI_SCAN_DONE_BIT BIT2

static EventGroupHandle_t s_wifi_event_group;
static int s_retry_num = 0;
static bool s_connected = false;
static int s_current_network_idx = -1;
static char s_connected_ssid[33] = {0};

// Forward declarations
static void wifi_scan_and_connect(void);
static int find_best_network(wifi_ap_record_t *ap_records, uint16_t ap_count);

static void event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT) {
        switch (event_id) {
            case WIFI_EVENT_STA_START:
                ESP_LOGI(TAG, "WiFi started, initiating scan...");
                xEventGroupSetBits(s_wifi_event_group, WIFI_SCAN_DONE_BIT);
                break;

            case WIFI_EVENT_SCAN_DONE:
                ESP_LOGI(TAG, "Scan complete");
                xEventGroupSetBits(s_wifi_event_group, WIFI_SCAN_DONE_BIT);
                break;

            case WIFI_EVENT_STA_DISCONNECTED: {
                wifi_event_sta_disconnected_t *event = (wifi_event_sta_disconnected_t *)event_data;
                s_connected = false;
                s_connected_ssid[0] = '\0';

                ESP_LOGW(TAG, "Disconnected from %s (reason: %d)",
                         event->ssid, event->reason);

                if (s_retry_num < WIFI_MAXIMUM_RETRY) {
                    s_retry_num++;
                    ESP_LOGI(TAG, "Reconnecting (attempt %d/%d)...",
                             s_retry_num, WIFI_MAXIMUM_RETRY);
                    esp_wifi_connect();
                } else {
                    ESP_LOGW(TAG, "Max retries reached, scanning for other networks...");
                    s_retry_num = 0;
                    xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
                }
                break;
            }

            default:
                break;
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "===========================================");
        ESP_LOGI(TAG, "Connected to: %s", s_connected_ssid);
        ESP_LOGI(TAG, "IP Address:   " IPSTR, IP2STR(&event->ip_info.ip));
        ESP_LOGI(TAG, "Gateway:      " IPSTR, IP2STR(&event->ip_info.gw));
        ESP_LOGI(TAG, "===========================================");
        s_retry_num = 0;
        s_connected = true;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

/**
 * Find the best known network from scan results
 * Returns index into wifi_networks[], or -1 if none found
 */
static int find_best_network(wifi_ap_record_t *ap_records, uint16_t ap_count)
{
    int best_idx = -1;
    int8_t best_rssi = -127;

    ESP_LOGI(TAG, "Found %d networks:", ap_count);

    for (int i = 0; i < ap_count; i++) {
        const char *ssid = (const char *)ap_records[i].ssid;
        int8_t rssi = ap_records[i].rssi;

        // Check if this is a known network
        bool is_known = false;
        int network_idx = -1;
        for (int j = 0; j < NUM_NETWORKS; j++) {
            if (strcmp(ssid, wifi_networks[j].ssid) == 0) {
                is_known = true;
                network_idx = j;
                break;
            }
        }

        ESP_LOGI(TAG, "  %s: %s (RSSI: %d dBm)%s",
                 is_known ? "[KNOWN]" : "       ",
                 ssid, rssi,
                 rssi < WIFI_RSSI_THRESHOLD ? " [weak]" : "");

        // Check if this is the best known network
        if (is_known && rssi > best_rssi && rssi >= WIFI_RSSI_THRESHOLD) {
            best_rssi = rssi;
            best_idx = network_idx;
        }
    }

    if (best_idx >= 0) {
        ESP_LOGI(TAG, "Best network: %s (RSSI: %d dBm)",
                 wifi_networks[best_idx].ssid, best_rssi);
    } else {
        ESP_LOGW(TAG, "No known networks found with sufficient signal");
    }

    return best_idx;
}

/**
 * Scan for networks and connect to the best one
 */
static void wifi_scan_and_connect(void)
{
    ESP_LOGI(TAG, "Scanning for WiFi networks...");

    // Clear any previous connection
    esp_wifi_disconnect();

    // Configure and start scan
    wifi_scan_config_t scan_config = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = false,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time.active.min = 100,
        .scan_time.active.max = 300,
    };

    xEventGroupClearBits(s_wifi_event_group, WIFI_SCAN_DONE_BIT);
    esp_err_t err = esp_wifi_scan_start(&scan_config, false);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Scan start failed: %s", esp_err_to_name(err));
        return;
    }

    // Wait for scan to complete
    xEventGroupWaitBits(s_wifi_event_group, WIFI_SCAN_DONE_BIT,
                        pdTRUE, pdFALSE, pdMS_TO_TICKS(10000));

    // Get scan results
    uint16_t ap_count = 0;
    esp_wifi_scan_get_ap_num(&ap_count);

    if (ap_count == 0) {
        ESP_LOGW(TAG, "No networks found");
        return;
    }

    wifi_ap_record_t *ap_records = malloc(sizeof(wifi_ap_record_t) * ap_count);
    if (!ap_records) {
        ESP_LOGE(TAG, "Failed to allocate memory for scan results");
        return;
    }

    esp_wifi_scan_get_ap_records(&ap_count, ap_records);

    // Find best known network
    int best_idx = find_best_network(ap_records, ap_count);
    free(ap_records);

    if (best_idx < 0) {
        ESP_LOGW(TAG, "No suitable network found, will retry...");
        return;
    }

    // Connect to the best network
    s_current_network_idx = best_idx;
    strncpy(s_connected_ssid, wifi_networks[best_idx].ssid, sizeof(s_connected_ssid) - 1);

    wifi_config_t wifi_config = {
        .sta = {
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg = {
                .capable = true,
                .required = false
            },
        },
    };

    strncpy((char *)wifi_config.sta.ssid, wifi_networks[best_idx].ssid,
            sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password, wifi_networks[best_idx].password,
            sizeof(wifi_config.sta.password) - 1);

    ESP_LOGI(TAG, "Connecting to: %s", wifi_networks[best_idx].ssid);

    esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    esp_wifi_connect();
}

/**
 * Background task to manage WiFi connection
 */
static void wifi_manager_task(void *pvParameters)
{
    // Initial scan and connect
    vTaskDelay(pdMS_TO_TICKS(1000));  // Brief delay for WiFi to initialize
    wifi_scan_and_connect();

    while (1) {
        EventBits_t bits = xEventGroupWaitBits(
            s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE,
            pdFALSE,
            pdMS_TO_TICKS(WIFI_SCAN_INTERVAL_MS)
        );

        if (bits & WIFI_FAIL_BIT) {
            // Connection failed, rescan
            xEventGroupClearBits(s_wifi_event_group, WIFI_FAIL_BIT);
            vTaskDelay(pdMS_TO_TICKS(2000));  // Brief delay before rescan
            wifi_scan_and_connect();
        } else if (!(bits & WIFI_CONNECTED_BIT)) {
            // Timeout - check if we're still connected
            if (!s_connected) {
                ESP_LOGI(TAG, "Periodic scan for better network...");
                wifi_scan_and_connect();
            }
        }
    }
}

esp_err_t wifi_init_sta(void)
{
    esp_err_t ret;

    ESP_LOGI(TAG, "Initializing multi-network WiFi manager");
    ESP_LOGI(TAG, "Configured networks:");
    for (int i = 0; i < NUM_NETWORKS; i++) {
        ESP_LOGI(TAG, "  %d. %s", i + 1, wifi_networks[i].ssid);
    }

    // Initialize NVS
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // Register event handlers
    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    // Start WiFi manager task
    xTaskCreate(wifi_manager_task, "wifi_mgr", 4096, NULL, 5, NULL);

    // Wait for initial connection (with timeout)
    ESP_LOGI(TAG, "Waiting for initial connection...");
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                           WIFI_CONNECTED_BIT,
                                           pdFALSE,
                                           pdFALSE,
                                           pdMS_TO_TICKS(30000));

    if (bits & WIFI_CONNECTED_BIT) {
        return ESP_OK;
    }

    ESP_LOGW(TAG, "Initial connection timeout - will keep trying in background");
    return ESP_OK;  // Return OK anyway, background task will keep trying
}

bool wifi_is_connected(void)
{
    return s_connected;
}

const char* wifi_get_ssid(void)
{
    return s_connected_ssid;
}
