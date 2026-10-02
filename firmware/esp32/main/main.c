/*
 * INFRA-NEX / Motor Guard — Main Firmware Entry (ESP-IDF Native C)
 * Hardware Motor Protection System for ESP32.
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"

#include "config.h"
#include "sensors.h"
#include "web_server.h"

static const char *TAG = "MAIN";

// ── Wi-Fi Event Handler ─────────────────────────────────────────────
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
        ESP_LOGI(TAG, "Connecting to Wi-Fi SSID: %s...", CONFIG_WIFI_SSID);
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        esp_wifi_connect();
        ESP_LOGW(TAG, "Wi-Fi disconnected. Reconnecting...");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Wi-Fi Connected! IP Address: " IPSTR, IP2STR(&event->ip_info.ip));
        start_web_server();
        ESP_LOGI(TAG, "Dashboard API available at http://" IPSTR "/", IP2STR(&event->ip_info.ip));
    }
}

static void wifi_init_sta(void) {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = CONFIG_WIFI_SSID,
            .password = CONFIG_WIFI_PASSWORD,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
}

// ── FreeRTOS Sensor Sampling Task ───────────────────────────────────
static void sensor_task(void *pvParameters) {
    motor_metrics_t metrics;
    ESP_LOGI(TAG, "Sensor sampling task started (500ms cycle).");

    while (1) {
        if (sensors_read_all(&metrics) == ESP_OK) {
            update_web_server_metrics(&metrics);

            ESP_LOGI(TAG, "[TELEMETRY] RPM: %.1f | Voltage: %.1fV | Current: %.3fA | Temp: %.1f°C | Vib: %.3fg | Health: %d/100 | Fault: %s",
                     metrics.rpm,
                     metrics.ac_voltage_v,
                     metrics.current_a,
                     metrics.temp_body_c,
                     metrics.vib_magnitude,
                     metrics.health_score,
                     metrics.fault_msg);
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

// ── ESP-IDF Main Entry Point ────────────────────────────────────────
void app_main(void) {
    ESP_LOGI(TAG, "==================================================");
    ESP_LOGI(TAG, "   INFRA-NEX Motor Guard Firmware (ESP-IDF Native) ");
    ESP_LOGI(TAG, "==================================================");

    // 1. Initialize NVS Flash
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // 2. Initialize Hardware Sensors (ADXL335, ZMPT101B, INA219, DS18B20, Hall, Relay)
    ESP_ERROR_CHECK(sensors_init());

    // 3. Initialize Wi-Fi & Web Server
    wifi_init_sta();

    // 4. Create Sensor Sampling Task
    xTaskCreate(sensor_task, "sensor_task", 4096, NULL, 5, NULL);
}
