/*
 * INFRA-NEX / Motor Guard — Sensor Drivers Header
 * Native ESP-IDF interfaces for ADXL335, ZMPT101B, INA219, DS18B20, Hall Effect, and Relay.
 */

#ifndef MOTOR_GUARD_SENSORS_H
#define MOTOR_GUARD_SENSORS_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float raw_x;
    float raw_y;
    float raw_z;
    float vib_magnitude;
    bool  vib_movement_detected;

    float zmpt_raw;
    float ac_voltage_v;
    bool  ac_voltage_detected;

    float current_a;
    float bus_voltage_v;
    bool  high_current;
    bool  ina219_present;

    float temp_body_c;
    float temp_bearing_c;
    bool  high_temp;
    bool  ds18b20_present;

    float rpm;
    bool  rpm_stall;
    bool  rpm_over;
    bool  rpm_degraded;

    uint8_t health_score;
    uint8_t fault_level;      // 0: Normal, 1: Warning, 2: Critical
    char    fault_msg[32];
    bool    relay_state;
    uint32_t uptime_seconds;
} motor_metrics_t;

// Sensor Lifecycle API
esp_err_t sensors_init(void);
esp_err_t sensors_read_all(motor_metrics_t *metrics);
void      sensors_set_relay(bool state);
bool      sensors_get_relay(void);

#ifdef __cplusplus
}
#endif

#endif // MOTOR_GUARD_SENSORS_H
