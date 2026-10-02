# 🏛️ Technical Architecture Document — Motor Guard

## 1. System Components Architecture

```
+-------------------------------------------------------------------+
|                        ESP32 MICROCONTROLLER                      |
|                                                                   |
|   +-----------------------------------------------------------+   |
|   |                    FreeRTOS Task Scheduler                |   |
|   +-----------------------------+-----------------------------+   |
|                                 |                                 |
|   +-----------------------------v-----------------------------+   |
|   |                 Sensor Sampling Loop (500ms)              |   |
|   |   - ADC Oneshot: ADXL335 (X,Y,Z), ZMPT101B (Voltage)     |   |
|   |   - I2C Driver: INA219 (Current/Voltage)                  |   |
|   |   - 1-Wire Driver: DS18B20 (Temperature)                  |   |
|   |   - GPIO Interrupt: Hall Tachometer (RPM)                 |   |
|   +-----------------------------+-----------------------------+   |
|                                 |                                 |
|   +-----------------------------v-----------------------------+   |
|   |                 Health Score Deduction Engine             |   |
|   |   - Computes deductions for RPM, Voltage, Current, Temp   |   |
|   +-----------------------------+-----------------------------+   |
|                                 |                                 |
|   +-----------------------------v-----------------------------+   |
|   |            Emergency Protection & Trip Decision           |   |
|   |   - Intercepts GPIO 25 Relay on Critical Fault            |   |
|   +-----------------------------+-----------------------------+   |
|                                 |                                 |
|   +-----------------------------v-----------------------------+   |
|   |                    ESP HTTP Web Server                    |   |
|   |   - Serves / (Dashboard UI)                               |   |
|   |   - Serves /data (cJSON Telemetry)                        |   |
|   |   - Serves /toggle (Relay Control API)                    |   |
|   +-----------------------------------------------------------+   |
+-------------------------------------------------------------------+
```

---

## 2. Software Subsystems

### 2.1 Sensor Driver Layer (`sensors.c`)
- Utilizes ESP-IDF `esp_adc/adc_oneshot.h` for high-accuracy single-shot analog sampling on ADC1.
- Implements microsecond-level bitbanging (`ets_delay_us`) for 1-Wire DS18B20 scratchpad reads.
- Installs GPIO ISR service for negative-edge pulse detection on GPIO 27 to calculate motor RPM.

### 2.2 Health Engine (`health_score.c`)
- Evaluates sensor values against calibrated upper/lower limits.
- Evaluates operational status and deducts score in 10 and 20 point steps.
- Enforces saturation at 0 (minimum) and 100 (maximum).

### 2.3 Web Engine (`web_server.c`)
- Leverages `esp_http_server` component with non-blocking async URI registration.
- Constructs lightweight `cJSON` objects for REST API consumers.

---

## 3. RTL Coprocessor Interoperability
The hardware deduction rules implemented in C (`health_score.c`) strictly mirror the logic defined in the Verilog HDL module [`rtl/health_score_calc.v`](file:///c:/Users/praza/Desktop/Motor_Guard/rtl/health_score_calc.v) for potential FPGA or ASIC coprocessor deployment.
