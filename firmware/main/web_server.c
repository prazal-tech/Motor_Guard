/*
 * INFRA-NEX / Motor Guard — ESP-IDF HTTP Web Server Implementation
 * Implements GET /, GET /data JSON endpoint, and POST /toggle relay route.
 */

#include "web_server.h"
#include "esp_log.h"
#include "cJSON.h"
#include <string.h>

static const char *TAG = "WEB_SERVER";
static httpd_handle_t server_handle = NULL;
static motor_metrics_t current_metrics;

// HTML Dashboard String
static const char *index_html_str =
"<!DOCTYPE html><html><head><meta charset='UTF-8'><title>Motor Guard ESP-IDF Dashboard</title>"
"<style>body{background:#080b0f;color:#cbd5e1;font-family:sans-serif;padding:20px;}"
"h1{color:#f59e0b;}.card{background:#141b24;border:1px solid #1f2d3d;padding:15px;margin:10px 0;border-radius:6px;}"
".val{font-weight:bold;color:#10b981;}button{background:#f59e0b;color:#000;border:none;padding:10px 20px;cursor:pointer;font-weight:bold;border-radius:4px;}</style></head>"
"<body><h1>INFRA-NEX Motor Guard (ESP-IDF Native)</h1>"
"<div class='card'><h2>Live Telemetry</h2>"
"<p>RPM: <span id='rpm' class='val'>--</span></p>"
"<p>AC Voltage: <span id='voltage' class='val'>--</span> V</p>"
"<p>Current: <span id='current' class='val'>--</span> A</p>"
"<p>Temperature: <span id='temp' class='val'>--</span> &deg;C</p>"
"<p>Vibration: <span id='vib' class='val'>--</span> g</p>"
"<p>Health Score: <span id='health' class='val'>--</span> / 100</p>"
"<p>Fault Status: <span id='fault' class='val'>--</span></p>"
"<p>Relay: <span id='relay' class='val'>--</span></p>"
"<button onclick='toggleRelay()'>Toggle Relay</button>"
"</div>"
"<script>"
"function update(){fetch('/data').then(r=>r.json()).then(d=>{"
"document.getElementById('rpm').innerText=d.rpm.toFixed(1);"
"document.getElementById('voltage').innerText=d.voltage_V.toFixed(1);"
"document.getElementById('current').innerText=d.current_A.toFixed(3);"
"document.getElementById('temp').innerText=d.temp_body_C.toFixed(1);"
"document.getElementById('vib').innerText=d.vib_g.toFixed(3);"
"document.getElementById('health').innerText=d.healthScore;"
"document.getElementById('fault').innerText=d.fault;"
"document.getElementById('relay').innerText=d.relayState?'ON':'OFF';"
"});}"
"setInterval(update,2000);update();"
"function toggleRelay(){fetch('/toggle',{method:'POST'}).then(()=>update());}"
"</script></body></html>";

// GET / Handler
static esp_err_t root_get_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, index_html_str, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

// GET /data JSON Handler
static esp_err_t data_get_handler(httpd_req_t *req) {
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "rpm", current_metrics.rpm);
    cJSON_AddNumberToObject(root, "voltage_V", current_metrics.ac_voltage_v);
    cJSON_AddNumberToObject(root, "current_A", current_metrics.current_a);
    cJSON_AddNumberToObject(root, "temp_body_C", current_metrics.temp_body_c);
    cJSON_AddNumberToObject(root, "temp_bearing_C", current_metrics.temp_bearing_c);
    cJSON_AddNumberToObject(root, "vib_g", current_metrics.vib_magnitude);
    cJSON_AddNumberToObject(root, "healthScore", current_metrics.health_score);
    cJSON_AddStringToObject(root, "fault", current_metrics.fault_msg);
    cJSON_AddNumberToObject(root, "faultLevel", current_metrics.fault_level);
    cJSON_AddBoolToObject(root, "relayState", current_metrics.relay_state);
    cJSON_AddNumberToObject(root, "uptime", current_metrics.uptime_seconds);

    const char *json_str = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_str, HTTPD_RESP_USE_STRLEN);

    cJSON_free((void*)json_str);
    cJSON_Delete(root);
    return ESP_OK;
}

// POST /toggle Handler
static esp_err_t toggle_post_handler(httpd_req_t *req) {
    bool current = sensors_get_relay();
    sensors_set_relay(!current);
    httpd_resp_set_type(req, "application/json");
    const char *resp = "{\"status\":\"ok\",\"toggled\":true}";
    httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

esp_err_t start_web_server(void) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 8192;

    ESP_LOGI(TAG, "Starting HTTP Server on port %d", config.server_port);
    if (httpd_start(&server_handle, &config) == ESP_OK) {
        httpd_uri_t root_uri = {
            .uri      = "/",
            .method   = HTTP_GET,
            .handler  = root_get_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server_handle, &root_uri);

        httpd_uri_t data_uri = {
            .uri      = "/data",
            .method   = HTTP_GET,
            .handler  = data_get_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server_handle, &data_uri);

        httpd_uri_t toggle_uri = {
            .uri      = "/toggle",
            .method   = HTTP_POST,
            .handler  = toggle_post_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server_handle, &toggle_uri);

        return ESP_OK;
    }
    ESP_LOGE(TAG, "Failed to start HTTP server!");
    return ESP_FAIL;
}

void update_web_server_metrics(const motor_metrics_t *metrics) {
    if (metrics) {
        memcpy(&current_metrics, metrics, sizeof(motor_metrics_t));
    }
}
