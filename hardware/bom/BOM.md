# 📦 Motor Guard — Hardware Bill of Materials (BOM)

| Component | Part / Module | Function | Operating Range / Spec | Interface / Pin |
| :--- | :--- | :--- | :--- | :--- |
| **Microcontroller** | ESP32-WROOM-32 | Central Controller & Edge Processing | 240 MHz Dual Core, 4MB Flash, Wi-Fi | GPIO 0 - 39 |
| **Vibration Sensor** | ADXL335 | 3-Axis Analog Accelerometer | &plusmn;3g range, analog voltage output | GPIO 34 (X), 35 (Y), 32 (Z) |
| **AC Voltage Sensor** | ZMPT101B | AC Mains Line Voltage Measurement | 0–250V AC, Analog sampling | GPIO 33 |
| **DC Voltage/Current** | INA219 | High-side DC Bus Current & Voltage | 0–26V DC, 0–3.2A Current | GPIO 21 (SDA), 22 (SCL) |
| **Temperature Sensor** | DS18B20 | Motor Body & Bearing Thermal Monitoring | -55&deg;C to +125&deg;C, 1-Wire Digital | GPIO 4 |
| **RPM Tachometer** | Hall Effect / TCRT5000 | Motor Speed & Rotation Frequency | 0–5000 RPM, Pulse interrupt | GPIO 27 |
| **Relay Switch** | 1-Channel 5V Relay | Automatic Over-fault Power Tripping | 250V AC / 10A, Active LOW/HIGH | GPIO 25 |
| **Power Supply** | Buck Converter / USB | Regulated Power Source | 5V DC / 2A supply to ESP32 board | VIN / GND |

---

## ⚡ Power & Voltage Tolerances

- **ESP32 Core**: 3.3V Logic Level
- **Sensors VCC**:
  - ADXL335: 3.3V
  - ZMPT101B: 5V VCC (Signal attenuated to 3.3V max)
  - INA219: 3.3V VCC, SDA/SCL pull-ups to 3.3V
  - DS18B20: 3.3V VCC (with 4.7k&Omega; pull-up resistor to 3.3V)
  - Hall Sensor: 3.3V VCC
  - Relay Module: 5V VCC (Optocoupled input from GPIO 25)
