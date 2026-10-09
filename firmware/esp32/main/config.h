/*
 * INFRA-NEX / Motor Guard — ESP-IDF Configuration Header
 * Pin mappings, sensor thresholds, calibration offsets, and WiFi settings.
 */

#ifndef MOTOR_GUARD_CONFIG_H
#define MOTOR_GUARD_CONFIG_H

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_adc/adc_oneshot.h"

#ifdef __cplusplus
extern "C" {
#endif

// =====================================================================
// WIFI & THINGSPEAK CONFIGURATION
// =====================================================================
#define CONFIG_WIFI_SSID            "Jayshree krishna"
#define CONFIG_WIFI_PASSWORD        "Shreenathg@131"
#define CONFIG_WIFI_MAXIMUM_RETRY   10

#define THINGSPEAK_API_KEY          "7DF6OSIMH0V3YCKR"
#define THINGSPEAK_URL              "http://api.thingspeak.com/update"
#define THINGSPEAK_INTERVAL_MS      16000

#define CONFIG_GATEWAY_URL          "http://192.168.1.100:4000/api/ingest" // Keep for local testing if needed
#define SUPABASE_URL                "https://brlxgpzdezntqikzgynx.supabase.co"
#define SUPABASE_ANON_KEY           "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJpc3MiOiJzdXBhYmFzZSIsInJlZiI6ImJybHhncHpkZXpudHFpa3pneW54Iiwicm9sZSI6ImFub24iLCJpYXQiOjE3OTE0MjMzOTcsImV4cCI6MjEwNjk5OTM5N30.CZqPVkN3NHhnrCet0qjpY3KL65tmvNjwK9yUgz5780M"


// =====================================================================
// GPIO PIN DEFINITIONS
// =====================================================================
// ADXL335 Analog Accelerometer Pins (ADC1)
#define ADXL335_X_PIN               GPIO_NUM_34  // ADC1_CHANNEL_6
#define ADXL335_Y_PIN               GPIO_NUM_35  // ADC1_CHANNEL_7
#define ADXL335_Z_PIN               GPIO_NUM_32  // ADC1_CHANNEL_4

#define ADXL335_X_ADC_CHANNEL       ADC_CHANNEL_6
#define ADXL335_Y_ADC_CHANNEL       ADC_CHANNEL_7
#define ADXL335_Z_ADC_CHANNEL       ADC_CHANNEL_4

// ZMPT101B AC Voltage Sensor Pin (ADC1)
#define ZMPT101B_PIN                GPIO_NUM_33  // ADC1_CHANNEL_5
#define ZMPT101B_ADC_CHANNEL        ADC_CHANNEL_5

// Hall Effect / RPM Tachometer Pulse Pin
#define HALL_SENSOR_PIN             GPIO_NUM_27

// DS18B20 1-Wire Digital Temperature Sensor Pin
#define DS18B20_PIN                 GPIO_NUM_4

// Relay Output Control Pin
#define RELAY_PIN                   GPIO_NUM_25

// INA219 I2C Current & Voltage Sensor Pins
#define INA219_I2C_PORT             I2C_NUM_0
#define INA219_SDA_PIN              GPIO_NUM_21
#define INA219_SCL_PIN              GPIO_NUM_22
#define INA219_I2C_FREQ_HZ          100000
#define INA219_I2C_ADDR             0x40

// =====================================================================
// SENSOR CALIBRATION & THRESHOLD CONSTANTS
// =====================================================================
#define ADXL_THRESHOLD_ADC          200     // ADC differential threshold
#define ADXL_SENSITIVITY            373.0f  // mV/g
#define ADXL_X_ZERO                 1860
#define ADXL_Y_ZERO                 1865
#define ADXL_Z_ZERO                 1890

#define VOLTAGE_THRESHOLD_ADC       1800    // ZMPT101B raw threshold
#define CURRENT_THRESHOLD_A         0.5f    // INA219 Over-current limit (Amperes)
#define TEMP_THRESHOLD_C            30.0f   // DS18B20 High temperature threshold (°C)
#define TEMP_CRIT_C                 70.0f   // Critical fault temperature (°C)

#define RPM_MIN_RUN                 400.0f  // Degraded low RPM
#define RPM_MAX_RUN                 900.0f  // Over-speed RPM

#define PULSES_PER_REV              2.0f    // Hall sensor magnet count

#ifdef __cplusplus
}
#endif

#endif // MOTOR_GUARD_CONFIG_H
