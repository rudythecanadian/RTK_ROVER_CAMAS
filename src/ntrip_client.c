/**
 * NTRIP Client (Rover Mode)
 * Connects to NTRIP caster to receive RTCM corrections
 */

#include <string.h>
#include <sys/socket.h>
#include <netdb.h>
#include <errno.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "ntrip_client.h"
#include "config.h"

static const char *TAG = "ntrip_client";

static int s_sock = -1;
static bool s_connected = false;
static uint32_t s_bytes_received = 0;
static TickType_t s_last_data_time = 0;

// How long without data before we consider the connection stale
#define NTRIP_STALE_TIMEOUT_MS 15000

/**
 * Base64 encode credentials for HTTP Basic Auth
 */
static void base64_encode(const char *input, char *output, size_t output_len)
{
    static const char base64_chars[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    size_t input_len = strlen(input);
    size_t i = 0, j = 0;

    while (i < input_len && j < output_len - 4) {
        uint32_t octet_a = i < input_len ? (unsigned char)input[i++] : 0;
        uint32_t octet_b = i < input_len ? (unsigned char)input[i++] : 0;
        uint32_t octet_c = i < input_len ? (unsigned char)input[i++] : 0;

        uint32_t triple = (octet_a << 16) + (octet_b << 8) + octet_c;

        output[j++] = base64_chars[(triple >> 18) & 0x3F];
        output[j++] = base64_chars[(triple >> 12) & 0x3F];
        output[j++] = (i > input_len + 1) ? '=' : base64_chars[(triple >> 6) & 0x3F];
        output[j++] = (i > input_len) ? '=' : base64_chars[triple & 0x3F];
    }
    output[j] = '\0';
}

esp_err_t ntrip_client_connect(void)
{
    if (s_connected) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Connecting to NTRIP caster: %s:%d/%s",
             NTRIP_HOST, NTRIP_PORT, NTRIP_MOUNTPOINT);

    // Resolve hostname
    struct hostent *host = gethostbyname(NTRIP_HOST);
    if (host == NULL) {
        ESP_LOGE(TAG, "DNS lookup failed for %s", NTRIP_HOST);
        return ESP_FAIL;
    }

    // Create socket
    s_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s_sock < 0) {
        ESP_LOGE(TAG, "Failed to create socket: errno %d", errno);
        return ESP_FAIL;
    }

    // Set socket timeout
    struct timeval timeout = {
        .tv_sec = 10,
        .tv_usec = 0
    };
    setsockopt(s_sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(s_sock, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

    // Connect
    struct sockaddr_in server_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(NTRIP_PORT),
        .sin_addr = *((struct in_addr *)host->h_addr),
    };

    if (connect(s_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) != 0) {
        ESP_LOGE(TAG, "Socket connect failed: errno %d", errno);
        close(s_sock);
        s_sock = -1;
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "TCP connected, sending GET request...");

    // Build NTRIP client request - MUST match exact format that works
    // Order matters: Host, User-Agent, Ntrip-Version, Authorization, then empty line
    char request[512];
    int req_len;

    // Check if we need authentication
    if (strlen(NTRIP_USER) > 0 && strlen(NTRIP_PASSWORD) > 0) {
        // Build credentials string and encode
        char credentials[128];
        char auth_base64[256];
        snprintf(credentials, sizeof(credentials), "%s:%s", NTRIP_USER, NTRIP_PASSWORD);
        base64_encode(credentials, auth_base64, sizeof(auth_base64));

        req_len = snprintf(request, sizeof(request),
            "GET /%s HTTP/1.1\r\n"
            "Host: %s\r\n"
            "User-Agent: NTRIP TestClient/1.0\r\n"
            "Ntrip-Version: Ntrip/2.0\r\n"
            "Authorization: Basic %s\r\n"
            "\r\n",
            NTRIP_MOUNTPOINT, NTRIP_HOST, auth_base64);

        ESP_LOGI(TAG, "Using authentication for NTRIP");
    } else {
        req_len = snprintf(request, sizeof(request),
            "GET /%s HTTP/1.1\r\n"
            "Host: %s\r\n"
            "User-Agent: NTRIP TestClient/1.0\r\n"
            "Ntrip-Version: Ntrip/2.0\r\n"
            "\r\n",
            NTRIP_MOUNTPOINT, NTRIP_HOST);
    }

    if (send(s_sock, request, req_len, 0) < 0) {
        ESP_LOGE(TAG, "Failed to send GET request: errno %d", errno);
        close(s_sock);
        s_sock = -1;
        return ESP_FAIL;
    }

    // Read response
    char response[512];
    int len = recv(s_sock, response, sizeof(response) - 1, 0);
    if (len < 0) {
        ESP_LOGE(TAG, "Failed to receive response: errno %d", errno);
        close(s_sock);
        s_sock = -1;
        return ESP_FAIL;
    }
    response[len] = '\0';

    ESP_LOGI(TAG, "NTRIP response: %.100s...", response);

    // Check for success - "ICY 200 OK" or "HTTP/1.1 200 OK"
    if (strstr(response, "200") != NULL || strstr(response, "ICY") != NULL) {
        // Skip remaining headers (find \r\n\r\n)
        char *body_start = strstr(response, "\r\n\r\n");
        if (body_start) {
            // Any data after headers is RTCM data - we'll get it on next recv
        }

        ESP_LOGI(TAG, "NTRIP caster connected - receiving RTCM corrections");
        s_connected = true;
        s_last_data_time = xTaskGetTickCount();

        // Set non-blocking for data reception
        struct timeval recv_timeout = {
            .tv_sec = 0,
            .tv_usec = 100000  // 100ms timeout for non-blocking reads
        };
        setsockopt(s_sock, SOL_SOCKET, SO_RCVTIMEO, &recv_timeout, sizeof(recv_timeout));

        return ESP_OK;
    } else {
        ESP_LOGE(TAG, "NTRIP connection rejected: %s", response);
        close(s_sock);
        s_sock = -1;
        return ESP_FAIL;
    }
}

void ntrip_client_disconnect(void)
{
    if (s_sock >= 0) {
        close(s_sock);
        s_sock = -1;
    }
    s_connected = false;
    ESP_LOGI(TAG, "Disconnected from NTRIP caster");
}

bool ntrip_client_is_connected(void)
{
    return s_connected;
}

int ntrip_client_receive(uint8_t *buffer, size_t max_len)
{
    if (!s_connected || s_sock < 0) {
        return -1;
    }

    int received = recv(s_sock, buffer, max_len, 0);

    if (received < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            // No data available (non-blocking)
            return 0;
        }
        ESP_LOGE(TAG, "Failed to receive RTCM data: errno %d", errno);
        s_connected = false;
        close(s_sock);
        s_sock = -1;
        return -1;
    }

    if (received == 0) {
        // Connection closed by server
        ESP_LOGW(TAG, "NTRIP connection closed by server");
        s_connected = false;
        close(s_sock);
        s_sock = -1;
        return -1;
    }

    s_bytes_received += received;
    s_last_data_time = xTaskGetTickCount();
    return received;
}

uint32_t ntrip_client_get_bytes_received(void)
{
    return s_bytes_received;
}

bool ntrip_client_is_stale(void)
{
    if (!s_connected) return false;

    TickType_t elapsed = xTaskGetTickCount() - s_last_data_time;
    return elapsed > pdMS_TO_TICKS(NTRIP_STALE_TIMEOUT_MS);
}

void ntrip_client_check_stale(void)
{
    if (ntrip_client_is_stale()) {
        ESP_LOGW(TAG, "NTRIP data stale (>%d sec) - forcing reconnect",
                 NTRIP_STALE_TIMEOUT_MS / 1000);
        ntrip_client_disconnect();
    }
}
