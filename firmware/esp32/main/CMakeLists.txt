idf_component_register(
    SRCS "main.c" "sensors.c" "health_score.c" "web_server.c"
    INCLUDE_DIRS "."
    REQUIRES esp_adc esp_event esp_http_server esp_http_client esp_netif esp_timer esp_wifi nvs_flash driver esp_driver_gpio esp_driver_i2c log
)
