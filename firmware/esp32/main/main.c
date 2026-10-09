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

#include "esp_http_client.h"
#include "cJSON.h"

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

// ── HTTP POST to Gateway ────────────────────────────────────────────
static void post_telemetry_to_gateway(const motor_metrics_t *metrics) {
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "rpm", metrics->rpm);
    cJSON_AddNumberToObject(root, "ac_voltage_v", metrics->ac_voltage_v);
    cJSON_AddNumberToObject(root, "bus_voltage_v", metrics->bus_voltage_v);
    cJSON_AddNumberToObject(root, "current_a", metrics->current_a);
    cJSON_AddNumberToObject(root, "temp_body_c", metrics->temp_body_c);
    cJSON_AddNumberToObject(root, "temp_bearing_c", metrics->temp_bearing_c);
    cJSON_AddNumberToObject(root, "vib_magnitude", metrics->vib_magnitude);
    cJSON_AddNumberToObject(root, "health_score", metrics->health_score);
    cJSON_AddStringToObject(root, "fault_msg", metrics->fault_msg);
    cJSON_AddNumberToObject(root, "fault_level", metrics->fault_level);
    cJSON_AddBoolToObject(root, "relay_state", metrics->relay_state);

    char *post_data = cJSON_PrintUnformatted(root);
    
    esp_http_client_config_t config = {
        .url = SUPABASE_URL "/rest/v1/telemetry",
        .method = HTTP_METHOD_POST,
        .timeout_ms = 4000,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_header(client, "apikey", SUPABASE_ANON_KEY);
    
    char auth_header[400];
    snprintf(auth_header, sizeof(auth_header), "Bearer %s", SUPABASE_ANON_KEY);
    esp_http_client_set_header(client, "Authorization", auth_header);
    
    // Some supabase endpoints require Prefer header for immediate response parsing, optional.
    esp_http_client_set_header(client, "Prefer", "return=minimal");

    esp_http_client_set_post_field(client, post_data, strlen(post_data));

    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        ESP_LOGD(TAG, "Telemetry pushed to gateway. Status = %d", esp_http_client_get_status_code(client));
    } else {
        ESP_LOGE(TAG, "Failed to push telemetry to gateway: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
    cJSON_free(post_data);
    cJSON_Delete(root);
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

            // Send to external gateway service
            post_telemetry_to_gateway(&metrics);
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
