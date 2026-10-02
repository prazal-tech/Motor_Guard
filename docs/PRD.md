# 📄 Product Requirements Document (PRD) — INFRA-NEX Motor Guard

## 1. Executive Summary
**INFRA-NEX Motor Guard** is an industrial-grade edge IoT solution designed to monitor the operational health, thermal stability, electrical power parameters, and mechanical vibration of electric motors. It features automated trip protection, cloud logging, local REST API access, and an integrated real-time health score calculation algorithm.

---

## 2. Core Functional Requirements

### FR-1: Multi-Sensor Data Acquisition
- **Vibration Sensing**: Sample 3-axis acceleration from ADXL335 on GPIO 34, 35, 32 to calculate vibration vector magnitude $g_{mag} = \sqrt{g_x^2 + g_y^2 + g_z^2}$.
- **AC Voltage Sensing**: Sample ZMPT101B transformer output on GPIO 33 to compute AC RMS line voltage.
- **DC Current/Voltage Sensing**: Query INA219 via I2C (GPIO 21/22) for high-precision DC voltage and shunt current measurements.
- **Thermal Monitoring**: Read motor housing and bearing temperatures from DS18B20 digital 1-Wire sensor on GPIO 4.
- **RPM Tachometry**: Calculate motor rotation speed in RPM using high-priority GPIO interrupts on GPIO 27 (Hall Effect pulse counting).

### FR-2: Real-time Health Score Algorithm
- Compute a saturated 0–100 health index every 500 ms based on cumulative operational deductions:
  - Over-current / Over-temperature / Stall / Excessive vibration: -20 points each.
  - Degraded RPM / Low current: -10 points each.
  - Saturated minimum score = 0.

### FR-3: Fault Trip Protection
- Automatically trigger the relay module (GPIO 25) when critical thresholds are breached (e.g. Current > 0.5A, Temperature > 70°C, Vibration > 2.0g).

### FR-4: Connectivity & Monitoring Surfaces
- Serve a low-latency Web Dashboard over HTTP on port 80.
- Expose `/data` JSON telemetry API and `/toggle` relay control route.
- Support cloud logging upload to ThingSpeak.

---

## 3. Non-Functional Requirements
- **Performance**: Sensor sampling loop execution time $\le 500 \text{ ms}$.
- **Reliability**: Automatic Wi-Fi reconnection handling and NVS storage recovery.
- **Portability**: Native ESP-IDF C implementation compatible with ESP-IDF v5.x terminal tools.
