/*
 * INFRA-NEX / Motor Guard — Health Score Header
 */

#ifndef MOTOR_GUARD_HEALTH_SCORE_H
#define MOTOR_GUARD_HEALTH_SCORE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

uint8_t calculate_health_score(bool rpm_stall, bool rpm_over, bool rpm_degraded,
                               bool volt_bad,
                               bool high_curr, bool low_curr,
                               bool high_temp, bool high_vib);

#ifdef __cplusplus
}
#endif

#endif // MOTOR_GUARD_HEALTH_SCORE_H
