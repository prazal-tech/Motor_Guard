#include "ml_inference.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "EDGE_ML";

// Simulated ML Model Classes
typedef enum {
    CLASS_NORMAL = 0,
    CLASS_BEARING_WEAR = 1,
    CLASS_IMBALANCE = 2,
    CLASS_ELEC_FAULT = 3,
    CLASS_OVERVOLTAGE = 4
} ml_class_t;

void ml_inference_init(void) {
    ESP_LOGI(TAG, "Initializing Edge ML Inference Module (Custom Decision Tree)...");
}

void ml_inference_run(motor_metrics_t *metrics) {
    // 1. Feature extraction
    float vib = metrics->vib_magnitude;
    float current = metrics->current_a;
    float temp = metrics->temp_body_c;
    float rpm = metrics->rpm;
    float voltage = metrics->ac_voltage_v;

    // 2. Simulated Decision Tree Inference
    ml_class_t prediction = CLASS_NORMAL;
    float confidence = 1.0f;

    if (voltage > 250.0f) {
        // Severe Overvoltage condition (Grid irregularity)
        prediction = CLASS_OVERVOLTAGE;
        confidence = 0.99f;
    }
    else if (vib > 1.5f && temp > 40.0f) {
        // High vibration + elevated temp strongly suggests bearing wear
        prediction = CLASS_BEARING_WEAR;
        confidence = 0.92f;
    } 
    else if (vib > 1.8f && rpm < 400.0f) {
        // High vibration + low RPM suggests mechanical imbalance/load issue
        prediction = CLASS_IMBALANCE;
        confidence = 0.88f;
    }
    else if (current > 0.45f && temp > 35.0f) {
        // Elevated current + temp without high vibration suggests electrical fault/stator issue
        prediction = CLASS_ELEC_FAULT;
        confidence = 0.85f;
    }
    else if (vib < 0.5f && temp < 30.0f && current < 0.2f && voltage >= 180.0f && voltage <= 240.0f) {
        prediction = CLASS_NORMAL;
        confidence = 0.95f;
    }

    // 3. Apply predictions to system fault state if confidence is high
    if (prediction != CLASS_NORMAL && confidence > 0.80f) {
        metrics->fault_level = 2; // Critical anomaly
        
        switch (prediction) {
            case CLASS_BEARING_WEAR:
                snprintf(metrics->fault_msg, sizeof(metrics->fault_msg), "ML: BEARING WEAR");
                break;
            case CLASS_IMBALANCE:
                snprintf(metrics->fault_msg, sizeof(metrics->fault_msg), "ML: IMBALANCE");
                break;
            case CLASS_ELEC_FAULT:
                snprintf(metrics->fault_msg, sizeof(metrics->fault_msg), "ML: ELEC FAULT");
                break;
            case CLASS_OVERVOLTAGE:
                snprintf(metrics->fault_msg, sizeof(metrics->fault_msg), "ML: OVERVOLTAGE");
                break;
            default:
                break;
        }
        ESP_LOGW(TAG, "Anomaly Detected by ML! Class: %d, Confidence: %.2f", prediction, confidence);
    } else {
        ESP_LOGD(TAG, "ML Inference: Normal operation.");
    }
}
