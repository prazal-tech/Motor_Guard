# 🔌 Motor Guard — GPIO Pinout & Connection Table

The following hardware pin mapping is configured in [`firmware/main/config.h`](file:///c:/Users/praza/Desktop/Motor_Guard/firmware/main/config.h):

| ESP32 Pin | Peripheral / Sensor | Signal | Direction | Connection Notes |
| :--- | :--- | :--- | :--- | :--- |
| **GPIO 34** | ADXL335 | X-Axis Analog | Input (ADC1_CH6) | Direct analog pin to ESP32 ADC1 |
| **GPIO 35** | ADXL335 | Y-Axis Analog | Input (ADC1_CH7) | Direct analog pin to ESP32 ADC1 |
| **GPIO 32** | ADXL335 | Z-Axis Analog | Input (ADC1_CH4) | Direct analog pin to ESP32 ADC1 |
| **GPIO 33** | ZMPT101B | AC Voltage OUT | Input (ADC1_CH5) | Ensure 3.3V peak voltage divider |
| **GPIO 27** | Hall Sensor | Pulse Interrupt | Input (Pull-up) | Falling edge interrupt for RPM pulse |
| **GPIO 4** | DS18B20 | 1-Wire Data | Bi-directional | Requires 4.7k&Omega; pull-up resistor to 3.3V |
| **GPIO 25** | Relay Module | Relay Control IN | Output | Digital output signal for trip relay |
| **GPIO 21** | INA219 | I2C SDA | Bi-directional | I2C Master Data |
| **GPIO 22** | INA219 | I2C SCL | Output | I2C Master Clock |
| **3.3V** | Power Rail | 3.3V VCC | Power Output | ADXL335, INA219, DS18B20, Hall VCC |
| **5V / VIN** | Power Rail | 5V VCC | Power Input | ZMPT101B VCC, Relay Module VCC |
| **GND** | Ground Rail | Common GND | Power Ground | Shared ground across all sensors |

---

## 🛠️ Wiring Circuit Schematic Diagram

```
                 +--------------------------------+
                 |          ESP32-WROOM32         |
                 |                                |
   ADXL335 X --->| GPIO 34 (ADC1_6)               |
   ADXL335 Y --->| GPIO 35 (ADC1_7)               |
   ADXL335 Z --->| GPIO 32 (ADC1_4)               |
  ZMPT101B   --->| GPIO 33 (ADC1_5)               |
 Hall Sensor --->| GPIO 27 (Tach Interrupt)       |===> High-Speed RPM Calculation
    DS18B20 <--->| GPIO 4  (1-Wire Temp)          |===> Motor Body & Bearing Thermal
     INA219 <--->| GPIO 21 (SDA) / GPIO 22 (SCL)  |===> I2C DC Voltage & Current
 Relay Module<---| GPIO 25 (Digital Output)       |===> Automatic Emergency Trip
                 +--------------------------------+
```
