/**
 * Dashboard Client - Sends position data to web dashboard via HTTP POST
 */

#include <string.h>
#include <sys/socket.h>
#include <netdb.h>
#include <errno.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "dashboard_client.h"
#include "config.h"
#include "ota_update.h"

static const char *TAG = "dashboard";

esp_err_t dashboard_send_position(const zed_position_t *pos,
                                   uint32_t rtcm_bytes,
                                   uint32_t fixed_count,
                                   uint32_t float_count,
                                   int battery_percentage)
{
#if !DASHBOARD_ENABLED
    return ESP_OK;
#endif

    if (pos == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    // Resolve hostname
    struct hostent *host = gethostbyname(DASHBOARD_HOST);
    if (host == NULL) {
        ESP_LOGW(TAG, "DNS lookup failed for %s", DASHBOARD_HOST);
        return ESP_FAIL;
    }

    // Create socket
    int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock < 0) {
        ESP_LOGW(TAG, "Failed to create socket: errno %d", errno);
        return ESP_FAIL;
    }

    // Set timeout
    struct timeval timeout = {
        .tv_sec = 5,
        .tv_usec = 0
    };
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

    // Connect
    struct sockaddr_in server_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(DASHBOARD_PORT),
        .sin_addr = *((struct in_addr *)host->h_addr),
    };

    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) != 0) {
        ESP_LOGW(TAG, "Dashboard connect failed: errno %d", errno);
        close(sock);
        return ESP_FAIL;
    }

    // Build JSON payload
    char json[512];
    int json_len = snprintf(json, sizeof(json),
        "{"
        "\"latitude\":%.9f,"
        "\"longitude\":%.9f,"
        "\"altitude\":%.3f,"
        "\"h_acc\":%.4f,"
        "\"v_acc\":%.4f,"
        "\"fix_type\":%d,"
        "\"carr_soln\":%d,"
        "\"num_sv\":%d,"
        "\"rtcm_bytes\":%lu,"
        "\"fixed_count\":%lu,"
        "\"float_count\":%lu,"
        "\"hour\":%d,"
        "\"min\":%d,"
        "\"sec\":%d,"
        "\"battery_pct\":%d,"
        "\"firmware_version\":\"%s\""
        "}",
        pos->latitude,
        pos->longitude,
        pos->altitude_msl,
        pos->h_acc,
        pos->v_acc,
        pos->fix_type,
        pos->carr_soln,
        pos->num_sv,
        (unsigned long)rtcm_bytes,
        (unsigned long)fixed_count,
        (unsigned long)float_count,
        pos->hour,
        pos->min,
        pos->sec,
        battery_percentage,
        ota_get_version()
    );

    // Build HTTP request
    char request[768];
    int req_len = snprintf(request, sizeof(request),
        "POST %s HTTP/1.1\r\n"
        "Host: %s:%d\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n"
        "\r\n"
        "%s",
        DASHBOARD_PATH, DASHBOARD_HOST, DASHBOARD_PORT, json_len, json
    );

    // Send request
    if (send(sock, request, req_len, 0) < 0) {
        ESP_LOGW(TAG, "Failed to send: errno %d", errno);
        close(sock);
        return ESP_FAIL;
    }

    // Read response (just check for success, don't parse)
    char response[128];
    int len = recv(sock, response, sizeof(response) - 1, 0);
    if (len > 0) {
        response[len] = '\0';
        // Check for 200 OK
        if (strstr(response, "200") == NULL) {
            ESP_LOGW(TAG, "Dashboard returned error");
        }
    }

    close(sock);
    return ESP_OK;
}
