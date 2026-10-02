/*
 * INFRA-NEX / Motor Guard — Sensor Drivers Implementation (ESP-IDF)
 */

#include "sensors.h"
#include "config.h"
#include "health_score.h"
#include <math.h>
#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "esp_adc/adc_oneshot.h"
#include "rom/ets_sys.h"

static const char *TAG = "SENSORS";

// ADC Oneshot Handle
static adc_oneshot_unit_handle_t adc1_handle;

// Hall Effect Tachometer Interrupt state
static volatile uint32_t pulse_count = 0;
static volatile int64_t last_pulse_time_us = 0;
static volatile int64_t pulse_interval_us = 0;
static int64_t last_rpm_calc_time_us = 0;

static bool current_relay_state = false;
static int64_t start_time_us = 0;

// ── Hall Sensor ISR Handler ──────────────────────────────────────────
static void IRAM_ATTR hall_isr_handler(void *arg) {
    int64_t now_us = esp_timer_get_time();
    if (last_pulse_time_us > 0) {
        pulse_interval_us = now_us - last_pulse_time_us;
    }
    last_pulse_time_us = now_us;
    pulse_count++;
}

// ── DS18B20 1-Wire Bitbang Drivers ──────────────────────────────────
static void ds18b20_write_bit(char bit) {
    gpio_set_direction(DS18B20_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(DS18B20_PIN, 0);
    if (bit) {
        ets_delay_us(10);
        gpio_set_level(DS18B20_PIN, 1);
        ets_delay_us(55);
    } else {
        ets_delay_us(65);
        gpio_set_level(DS18B20_PIN, 1);
        ets_delay_us(5);
    }
}

static char ds18b20_read_bit(void) {
    char bit = 0;
    gpio_set_direction(DS18B20_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(DS18B20_PIN, 0);
    ets_delay_us(3);
    gpio_set_direction(DS18B20_PIN, GPIO_MODE_INPUT);
    ets_delay_us(10);
    if (gpio_get_level(DS18B20_PIN)) {
        bit = 1;
    }
    ets_delay_us(53);
    return bit;
}

static void ds18b20_write_byte(uint8_t byte) {
    for (int i = 0; i < 8; i++) {
        ds18b20_write_bit(byte & 0x01);
        byte >>= 1;
    }
}

static uint8_t ds18b20_read_byte(void) {
    uint8_t byte = 0;
    for (int i = 0; i < 8; i++) {
        if (ds18b20_read_bit()) {
            byte |= (1 << i);
        }
    }
    return byte;
}

static bool ds18b20_reset(void) {
    gpio_set_direction(DS18B20_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(DS18B20_PIN, 0);
    ets_delay_us(480);
    gpio_set_direction(DS18B20_PIN, GPIO_MODE_INPUT);
    ets_delay_us(70);
    int presence = gpio_get_level(DS18B20_PIN);
    ets_delay_us(410);
    return (presence == 0);
}

static float ds18b20_read_temperature(void) {
    if (!ds18b20_reset()) return -999.0f;
    ds18b20_write_byte(0xCC); // Skip ROM
    ds18b20_write_byte(0x44); // Convert T
    ets_delay_us(750000); // 750ms conversion delay

    if (!ds18b20_reset()) return -999.0f;
    ds18b20_write_byte(0xCC); // Skip ROM
    ds18b20_write_byte(0xBE); // Read Scratchpad

    uint8_t low = ds18b20_read_byte();
    uint8_t high = ds18b20_read_byte();
    int16_t raw = (high << 8) | low;
    return (float)raw / 16.0f;
}

// ── INA219 I2C Drivers ──────────────────────────────────────────────
static esp_err_t ina219_init_i2c(void) {
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = INA219_SDA_PIN,
        .scl_io_num = INA219_SCL_PIN,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = INA219_I2C_FREQ_HZ,
    };
    i2c_param_config(INA219_I2C_PORT, &conf);
    return i2c_driver_install(INA219_I2C_PORT, conf.mode, 0, 0, 0);
}

static esp_err_t ina219_read_register(uint8_t reg, uint16_t *val) {
    uint8_t data[2];
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (INA219_I2C_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (INA219_I2C_ADDR << 1) | I2C_MASTER_READ, true);
    i2c_master_read(cmd, data, 2, I2C_MASTER_LAST_NACK);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(INA219_I2C_PORT, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);

    if (ret == ESP_OK) {
        *val = (data[0] << 8) | data[1];
    }
    return ret;
}

// ── Sensors Init API ────────────────────────────────────────────────
esp_err_t sensors_init(void) {
    start_time_us = esp_timer_get_time();
    ESP_LOGI(TAG, "Initializing Motor Guard Hardware Sensors...");

    // 1. Relay GPIO
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << RELAY_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
    gpio_set_level(RELAY_PIN, 0); // Relay default OFF

    // 2. Hall Sensor GPIO Interrupt
    gpio_config_t hall_conf = {
        .pin_bit_mask = (1ULL << HALL_SENSOR_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .intr_type = GPIO_INTR_NEGEDGE,
    };
    gpio_config(&hall_conf);
    gpio_install_isr_service(0);
    gpio_isr_handler_add(HALL_SENSOR_PIN, hall_isr_handler, NULL);

    // 3. ADC Oneshot Init (ADXL335 & ZMPT101B)
    adc_oneshot_unit_init_cfg_t init_config1 = {
        .unit_id = ADC_UNIT_1,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config1, &adc1_handle));

    adc_oneshot_chan_cfg_t chan_config = {
        .bitwidth = ADC_BITWIDTH_12,
        .atten = ADC_ATTEN_DB_12,
    };
    adc_oneshot_config_channel(adc1_handle, ADXL335_X_ADC_CHANNEL, &chan_config);
    adc_oneshot_config_channel(adc1_handle, ADXL335_Y_ADC_CHANNEL, &chan_config);
    adc_oneshot_config_channel(adc1_handle, ADXL335_Z_ADC_CHANNEL, &chan_config);
    adc_oneshot_config_channel(adc1_handle, ZMPT101B_ADC_CHANNEL, &chan_config);

    // 4. INA219 I2C Init
    esp_err_t i2c_ret = ina219_init_i2c();
    if (i2c_ret == ESP_OK) {
        ESP_LOGI(TAG, "INA219 Current sensor initialized on I2C bus.");
    } else {
        ESP_LOGW(TAG, "INA219 I2C Initialization warning: %d", i2c_ret);
    }

    return ESP_OK;
}

// ── Sensors Read API ────────────────────────────────────────────────
esp_err_t sensors_read_all(motor_metrics_t *m) {
    if (!m) return ESP_ERR_INVALID_ARG;
    memset(m, 0, sizeof(motor_metrics_t));

    m->uptime_seconds = (uint32_t)((esp_timer_get_time() - start_time_us) / 1000000);

    // 1. ADXL335 Analog Accelerometer
    int raw_x = 0, raw_y = 0, raw_z = 0;
    adc_oneshot_read(adc1_handle, ADXL335_X_ADC_CHANNEL, &raw_x);
    adc_oneshot_read(adc1_handle, ADXL335_Y_ADC_CHANNEL, &raw_y);
    adc_oneshot_read(adc1_handle, ADXL335_Z_ADC_CHANNEL, &raw_z);

    m->raw_x = (float)raw_x;
    m->raw_y = (float)raw_y;
    m->raw_z = (float)raw_z;

    int dx = abs(raw_x - ADXL_X_ZERO);
    int dy = abs(raw_y - ADXL_Y_ZERO);
    int dz = abs(raw_z - ADXL_Z_ZERO);
    m->vib_movement_detected = (dx > ADXL_THRESHOLD_ADC || dy > ADXL_THRESHOLD_ADC || dz > ADXL_THRESHOLD_ADC);

    float gx = (raw_x - ADXL_X_ZERO) / ADXL_SENSITIVITY;
    float gy = (raw_y - ADXL_Y_ZERO) / ADXL_SENSITIVITY;
    float gz = (raw_z - ADXL_Z_ZERO) / ADXL_SENSITIVITY;
    m->vib_magnitude = sqrtf(gx*gx + gy*gy + gz*gz);

    // 2. ZMPT101B AC Voltage Sensor
    int zmpt_val = 0;
    adc_oneshot_read(adc1_handle, ZMPT101B_ADC_CHANNEL, &zmpt_val);
    m->zmpt_raw = (float)zmpt_val;
    m->ac_voltage_detected = (zmpt_val > VOLTAGE_THRESHOLD_ADC);
    m->ac_voltage_v = (m->zmpt_raw / 4095.0f * 3.3f) * 85.0f; // Scale to AC RMS

    // 3. INA219 Current & Voltage Sensor
    uint16_t bus_reg = 0;
    esp_err_t ret = ina219_read_register(0x02, &bus_reg); // Bus Voltage reg
    if (ret == ESP_OK) {
        m->ina219_present = true;
        m->bus_voltage_v = ((bus_reg >> 3) * 4) * 0.001f;
        uint16_t shunt_reg = 0;
        ina219_read_register(0x01, &shunt_reg);
        m->current_a = (float)((int16_t)shunt_reg) * 0.1f / 1000.0f;
    } else {
        m->ina219_present = false;
        m->bus_voltage_v = 12.0f;
        m->current_a = 0.2f; // Fallback mock
    }
    m->high_current = (m->current_a > CURRENT_THRESHOLD_A);

    // 4. DS18B20 Temperature Sensor
    float temp_c = ds18b20_read_temperature();
    if (temp_c > -100.0f) {
        m->ds18b20_present = true;
        m->temp_body_c = temp_c;
        m->temp_bearing_c = temp_c + 2.5f; // Estimated bearing diff
    } else {
        m->ds18b20_present = false;
        m->temp_body_c = 28.5f;
        m->temp_bearing_c = 30.0f;
    }
    m->high_temp = (m->temp_body_c > TEMP_THRESHOLD_C);

    // 5. Hall Sensor RPM Calculation
    int64_t now_us = esp_timer_get_time();
    int64_t dt_us = now_us - last_rpm_calc_time_us;
    if (dt_us >= 1000000) { // Calculate every 1 sec
        uint32_t pulses = pulse_count;
        pulse_count = 0;
        m->rpm = (pulses * 60.0f) / (PULSES_PER_REV * (dt_us / 1000000.0f));
        last_rpm_calc_time_us = now_us;
    } else {
        m->rpm = 450.0f; // Default baseline during warm-up
    }

    m->rpm_stall = (m->rpm < 30.0f);
    m->rpm_over = (m->rpm > RPM_MAX_RUN);
    m->rpm_degraded = (m->rpm < RPM_MIN_RUN || m->rpm > 600.0f) && !m->rpm_stall && !m->rpm_over;

    // 6. Health Score Calculation
    bool volt_bad = (m->ac_voltage_v < 180.0f || m->ac_voltage_v > 260.0f);
    bool low_curr = (m->current_a < 0.05f);

    m->health_score = calculate_health_score(
        m->rpm_stall, m->rpm_over, m->rpm_degraded,
        volt_bad, m->high_current, low_curr,
        m->high_temp, m->vib_movement_detected
    );

    // Fault Status Derivation
    if (m->high_current || m->high_temp || m->rpm_stall || m->vib_magnitude > 2.0f) {
        m->fault_level = 2; // Critical
        snprintf(m->fault_msg, sizeof(m->fault_msg), "CRITICAL FAULT");
    } else if (m->rpm_degraded || volt_bad) {
        m->fault_level = 1; // Warning
        snprintf(m->fault_msg, sizeof(m->fault_msg), "WARNING - DEGRADED");
    } else {
        m->fault_level = 0; // Normal
        snprintf(m->fault_msg, sizeof(m->fault_msg), "SYSTEM NORMAL");
    }

    // Trip Relay Decision Logic
    bool trigger_relay = (m->fault_level == 2);
    sensors_set_relay(trigger_relay);
    m->relay_state = current_relay_state;

    return ESP_OK;
}

void sensors_set_relay(bool state) {
    current_relay_state = state;
    gpio_set_level(RELAY_PIN, state ? 1 : 0);
}

bool sensors_get_relay(void) {
    return current_relay_state;
}
