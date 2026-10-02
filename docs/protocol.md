# 📡 Motor Guard Telemetry & Protocol Specification

This document details the communication protocols, API payload schemas, and cloud field mappings for the **INFRA-NEX Motor Guard** system.

---

## 1. Local HTTP REST API

The ESP32 embedded web server exposes two primary REST endpoints over HTTP on port 80.

### 1.1 `GET /data`
Retrieves real-time sensor measurements, system uptime, health score, and fault status.

#### Response Headers
```http
HTTP/1.1 200 OK
Content-Type: application/json
Access-Control-Allow-Origin: *
```

#### Response Body Schema
```json
{
  "rpm": 450.0,
  "voltage_V": 230.5,
  "current_A": 0.215,
  "temp_body_C": 28.5,
  "temp_bearing_C": 31.0,
  "vib_g": 0.045,
  "healthScore": 100,
  "fault": "SYSTEM NORMAL",
  "faultLevel": 0,
  "relayState": false,
  "uptime": 124
}
```

| Field Name | Type | Unit | Description |
| :--- | :--- | :--- | :--- |
| `rpm` | `float` | RPM | Motor rotational speed |
| `voltage_V` | `float` | Volts (AC) | Mains RMS voltage |
| `current_A` | `float` | Amperes (DC) | Shunt current drawn by motor |
| `temp_body_C` | `float` | °C | Motor housing temperature |
| `temp_bearing_C` | `float` | °C | Motor bearing temperature |
| `vib_g` | `float` | $g$ | 3-axis vibration vector magnitude |
| `healthScore` | `integer` | 0–100 | Saturated motor health index |
| `fault` | `string` | Text | Human-readable fault state message |
| `faultLevel` | `integer` | 0, 1, 2 | 0: Normal, 1: Warning, 2: Critical |
| `relayState` | `boolean` | bool | Emergency relay status (`true` = Trips Power) |
| `uptime` | `integer` | Seconds | System operational uptime |

---

### 1.2 `POST /toggle`
Manually toggles the emergency power relay.

#### Response Body
```json
{
  "status": "ok",
  "toggled": true
}
```

---

## 2. Cloud Telemetry — ThingSpeak Field Mapping

Telemetry is periodically uploaded to ThingSpeak every 16 seconds.

| Field | Parameter | Unit | Range / Description |
| :--- | :--- | :--- | :--- |
| **Field 1** | Motor Speed | RPM | 0–5000 RPM |
| **Field 2** | AC Line Voltage | V | 0–300V AC |
| **Field 3** | Motor Current | A | 0–5.0A DC |
| **Field 4** | Motor Body Temp | °C | -20°C to +125°C |
| **Field 5** | Vibration Magnitude | $g$ | 0–5.0g |
| **Field 6** | Threat Level | 0–3 | Risk assessment index |
| **Field 7** | Fault Code | Bitmask | Error code representation |
| **Field 8** | Health Score | 0–100 | Saturated health score |
