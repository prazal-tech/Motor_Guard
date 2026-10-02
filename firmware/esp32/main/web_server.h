/*
 * INFRA-NEX / Motor Guard — ESP-IDF HTTP Web Server Header
 */

#ifndef MOTOR_GUARD_WEB_SERVER_H
#define MOTOR_GUARD_WEB_SERVER_H

#include "esp_err.h"
#include "esp_http_server.h"
#include "sensors.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t start_web_server(void);
void      update_web_server_metrics(const motor_metrics_t *metrics);

#ifdef __cplusplus
}
#endif

#endif // MOTOR_GUARD_WEB_SERVER_H
