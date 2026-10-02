# 📐 Hardware PCB & Enclosure Specifications

## 1. Electrical Specification & Connectors
- **Primary MCU**: ESP32-WROOM-32 standard 38-pin DevKit board.
- **Power Inputs**:
  - 5V / 2A DC via Terminal Block or Micro-USB.
  - 3.3V LDO regulator output for low-noise analog sensor rails.
- **Terminal Headers**:
  - `J1` (ADC Inputs): ADXL335 (X, Y, Z) and ZMPT101B.
  - `J2` (I2C Bus): INA219 SDA (GPIO 21), SCL (GPIO 22).
  - `J3` (Digital Inputs): Hall Tachometer (GPIO 27 with 10k pull-up).
  - `J4` (1-Wire Bus): DS18B20 (GPIO 4 with 4.7k pull-up).
  - `J5` (Power Trip Output): Relay Coil Driver (GPIO 25 Optocoupled).

---

## 2. Recommended Physical Enclosure
- **Enclosure Type**: IP65 Waterproof DIN-Rail Mounting Industrial Box (115mm x 90mm x 55mm).
- **Vibration Dampening**: Rubber anti-vibration standoffs between ADXL335 sensor PCB and motor housing mounting bracket.
