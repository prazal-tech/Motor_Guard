/*
 * MotorGuardCore — Shared Core Firmware Utilities & Abstractions
 */

#ifndef MOTOR_GUARD_CORE_H
#define MOTOR_GUARD_CORE_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float rpm_min_threshold;
    float rpm_max_threshold;
    float current_trip_a;
    float temp_trip_c;
    float vib_trip_g;
} motor_guard_config_t;

#ifdef __cplusplus
}
#endif

#endif // MOTOR_GUARD_CORE_H
