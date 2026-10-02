/*
 * INFRA-NEX / Motor Guard — Health Score Algorithm Implementation
 * Real-time deduction algorithm matching health_score_calc.v & C++ firmware spec.
 */

#include "health_score.h"

uint8_t calculate_health_score(bool rpm_stall, bool rpm_over, bool rpm_degraded,
                               bool volt_bad,
                               bool high_curr, bool low_curr,
                               bool high_temp, bool high_vib)
{
    int16_t deduction = 0;

    // RPM health deduction
    if (rpm_stall || rpm_over) {
        deduction += 20;
    } else if (rpm_degraded) {
        deduction += 10;
    }

    // Voltage health deduction
    if (volt_bad) {
        deduction += 20;
    }

    // Current health deduction
    if (high_curr) {
        deduction += 20;
    } else if (low_curr) {
        deduction += 10;
    }

    // Temperature health deduction
    if (high_temp) {
        deduction += 20;
    }

    // Vibration health deduction
    if (high_vib) {
        deduction += 20;
    }

    // Score saturation: deduction >= 100 forces health to 0
    if (deduction >= 100) {
        return 0;
    }

    return (uint8_t)(100 - deduction);
}
