#ifndef MOTOR_GUARD_ML_INFERENCE_H
#define MOTOR_GUARD_ML_INFERENCE_H

#include "sensors.h"

#ifdef __cplusplus
extern "C" {
#endif

// Initializes the Edge Impulse model (if needed)
void ml_inference_init(void);

// Runs inference on the gathered motor metrics
// Returns an updated fault level (0=Normal, 1=Warning, 2=Anomaly) or leaves it unchanged.
void ml_inference_run(motor_metrics_t *metrics);

#ifdef __cplusplus
}
#endif

#endif // MOTOR_GUARD_ML_INFERENCE_H
