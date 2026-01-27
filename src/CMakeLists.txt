idf_component_register(
    SRCS "main.c" "wifi.c" "ntrip_client.c" "zed_rover.c" "dashboard_client.c" "battery.c" "ota_update.c"
    INCLUDE_DIRS "."
    REQUIRES driver esp_http_client esp_https_ota app_update
)
