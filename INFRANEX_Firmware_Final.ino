// ============================================================
//  INFRA-NEX · ESP32 Firmware · FINAL
//  Merged Sensor Code + Health Score Calculation
//  Sensors: ADXL335 + ZMPT101B + Hall Effect + 
//           INA219 + DS18B20 + Relay
// ============================================================

//  GPIO Map:
//    ADXL335  X   -> GPIO 34 (ADC)
//    ADXL335  Y   -> GPIO 35 (ADC)
//    ADXL335  Z   -> GPIO 32 (ADC)
//    ZMPT101B OUT -> GPIO 33 (ADC)
//    Hall Sensor  -> GPIO 27 (Digital)
//    INA219   SDA -> GPIO 21 (I2C)
//    INA219   SCL -> GPIO 22 (I2C)
//    DS18B20  DAT -> GPIO 4  (1-Wire)
//    Relay    IN  -> GPIO 25 (Digital OUT)

// ============================================================

#include <Wire.h>
#include <Adafruit_INA219.h>
#include <OneWire.h>
#include <DallasTemperature.h>

// ── Pin Definitions ──────────────────────────────────────────

#define X_PIN             34
#define Y_PIN             35
#define Z_PIN             32
#define ZMPT_PIN          33
#define HALL_SENSOR_PIN   27
#define DS18B20_PIN        4
#define RELAY_PIN         25

// I2C pins (INA219) — ESP32 default
#define SDA_PIN           21
#define SCL_PIN           22

// ── Thresholds ───────────────────────────────────────────────

#define ACCEL_THRESHOLD   200     // ADC units
#define VOLTAGE_THRESHOLD 1800    // ADC units (ZMPT101B)
#define CURRENT_THRESHOLD 0.5f    // Amperes (INA219)
#define TEMP_THRESHOLD    30.0f   // Celsius (DS18B20)

// ── Sensor Objects ───────────────────────────────────────────

Adafruit_INA219  ina219;
OneWire          oneWire(DS18B20_PIN);
DallasTemperature ds18b20(&oneWire);

// ── Accelerometer calibration center values ──────────────────

int xCenter, yCenter, zCenter;

// ── INA219 availability flag ─────────────────────────────────

bool ina219Found = false;

// ============================================================
// HEALTH SCORE CALCULATION (from health_score_calc.v)
// ============================================================

uint8_t calculateHealthScore(bool rpm_stall, bool rpm_over, bool rpm_degraded,
                              bool volt_bad,
                              bool high_curr, bool low_curr,
                              bool high_temp, bool high_vib) {
  int16_t deduction = 0;

  // RPM health tier
  if (rpm_stall || rpm_over)      deduction += 20;
  else if (rpm_degraded)          deduction += 10;

  // Voltage health
  if (volt_bad)                   deduction += 20;

  // Current health tier
  if (high_curr)                  deduction += 20;
  else if (low_curr)              deduction += 10;

  // Temperature health
  if (high_temp)                  deduction += 20;

  // Vibration health
  if (high_vib)                   deduction += 20;

  // Score saturation: deduction >= 100 forces health to 0
  if (deduction >= 100) return 0;
  return (uint8_t)(100 - deduction);
}

// ============================================================

void setup() {
  Serial.begin(115200);

  // Relay — default OFF
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);

  // Hall effect sensor
  pinMode(HALL_SENSOR_PIN, INPUT);

  // I2C for INA219
  Wire.begin(SDA_PIN, SCL_PIN);
  ina219Found = ina219.begin();

  if (!ina219Found) {
    Serial.println("[WARN] INA219 not found — skipping current sensor.");
  }

  // DS18B20 1-Wire temperature sensor
  ds18b20.begin();

  // Calibrate ADXL335 at rest position
  xCenter = analogRead(X_PIN);
  yCenter = analogRead(Y_PIN);
  zCenter = analogRead(Z_PIN);

  Serial.println("========================================");
  Serial.println(" INFRA-NEX Firmware — Started");
  Serial.println(" With Health Score Calculation");
  Serial.println("========================================");
}

// ============================================================

void loop() {

  // ── 1. ADXL335 Accelerometer ─────────────────────────────

  int x = analogRead(X_PIN);
  int y = analogRead(Y_PIN);
  int z = analogRead(Z_PIN);

  int xDiff = abs(x - xCenter);
  int yDiff = abs(y - yCenter);
  int zDiff = abs(z - zCenter);

  bool movementDetected = (xDiff > ACCEL_THRESHOLD ||
                           yDiff > ACCEL_THRESHOLD ||
                           zDiff > ACCEL_THRESHOLD);

  Serial.print("[ADXL335] X: "); Serial.print(x);
  Serial.print("  Y: ");         Serial.print(y);
  Serial.print("  Z: ");         Serial.print(z);
  Serial.print("  -> ");
  Serial.println(movementDetected ? "MOVEMENT DETECTED" : "Stable");

  // ── 2. ZMPT101B Voltage Sensor ────────────────────────────

  int zmptValue = analogRead(ZMPT_PIN);
  bool voltageDetected = (zmptValue > VOLTAGE_THRESHOLD);

  Serial.print("[ZMPT101B] Reading: "); Serial.print(zmptValue);
  Serial.print("  -> ");
  Serial.println(voltageDetected ? "VOLTAGE DETECTED" : "Low / No AC");

  // ── 3. Hall Effect Sensor ─────────────────────────────────

  bool magnetDetected = (digitalRead(HALL_SENSOR_PIN) == LOW);

  Serial.print("[HALL]     State: ");
  Serial.println(magnetDetected ? "MAGNET DETECTED" : "No Magnet");

  // ── 4. INA219 Current Sensor ──────────────────────────────

  bool highCurrent = false;
  float current_A = 0.0f;
  float busVoltage = 0.0f;

  if (ina219Found) {
    busVoltage = ina219.getBusVoltage_V();
    current_A  = ina219.getCurrent_mA() / 1000.0f;
    highCurrent = (current_A > CURRENT_THRESHOLD);

    Serial.print("[INA219]   Voltage: "); Serial.print(busVoltage, 2);
    Serial.print(" V  |  Current: ");     Serial.print(current_A, 3);
    Serial.print(" A  -> ");
    Serial.println(highCurrent ? "HIGH CURRENT" : "Normal");
  }

  // ── 5. DS18B20 Temperature Sensor ────────────────────────

  ds18b20.requestTemperatures();
  float temperature = ds18b20.getTempCByIndex(0);

  bool highTemp = (temperature != DEVICE_DISCONNECTED_C &&
                   temperature > TEMP_THRESHOLD);

  Serial.print("[DS18B20]  Temp: ");

  if (temperature == DEVICE_DISCONNECTED_C) {
    Serial.print("SENSOR ERROR");
  } else {
    Serial.print(temperature, 1);
    Serial.print(" °C");
  }

  Serial.print("  -> ");
  Serial.println(highTemp ? "HIGH TEMPERATURE" : "Normal");

  // ── 6. HEALTH SCORE CALCULATION ──────────────────────────
  // Derive boolean flags from sensor values (matching dashboard thresholds)

  // RPM flags — you'll need to calculate RPM from Hall sensor
  // Placeholder values: integrate with your Hall sensor ISR/counter
  float rpm = 0.0f;  // TODO: Set from your RPM counter
  bool rpm_stall    = (rpm < 30);
  bool rpm_over     = (rpm > 700);
  bool rpm_degraded = (rpm < 425 || rpm > 600) && !rpm_stall && !rpm_over;

  // Voltage flags — from ZMPT reading (you may need to convert ADC → volts)
  // Assuming zmptValue is raw ADC, convert to actual voltage if needed
  float voltage_dc = 12.0f;  // TODO: Convert zmptValue to DC volts (or read from INA219)
  bool volt_bad    = (voltage_dc < 9.6 || voltage_dc > 14.4);

  // Current flags
  bool low_curr    = (current_A < 0.1);  // Stall/no-load condition

  // Temperature flags
  bool high_vib    = (movementDetected);  // Use accel flag as proxy; ideally compute magnitude
  // TODO: Convert ADXL335 ADC to g's and compare to 0.8g threshold for more precision

  // Calculate health score
  uint8_t healthScore = calculateHealthScore(
    rpm_stall, rpm_over, rpm_degraded,
    volt_bad, highCurrent, low_curr, highTemp, high_vib
  );

  Serial.print("[HEALTH]   Score: "); Serial.print(healthScore);
  Serial.println("/100");

  // ── Relay Decision ────────────────────────────────────────

  bool triggerRelay = movementDetected ||
                      voltageDetected  ||
                      magnetDetected   ||
                      highCurrent      ||
                      highTemp;

  if (triggerRelay) {
    digitalWrite(RELAY_PIN, HIGH);
    Serial.print("[RELAY]    ON  |");

    if (movementDetected) Serial.print(" Movement");
    if (voltageDetected)  Serial.print(" Voltage");
    if (magnetDetected)   Serial.print(" Magnet");
    if (highCurrent)      Serial.print(" HighCurrent");
    if (highTemp)         Serial.print(" HighTemp");

    Serial.println();
  } else {
    digitalWrite(RELAY_PIN, LOW);
    Serial.println("[RELAY]    OFF | All sensors normal");
  }

  Serial.println("────────────────────────────────────────");

  // ── ThingSpeak Upload ────────────────────────────────────
  // TODO: Uncomment and integrate with your WiFi + ThingSpeak code
  // Field mapping:
  //   field1 = RPM
  //   field2 = Voltage (DC)
  //   field3 = Current (A)
  //   field4 = Temperature (°C)
  //   field5 = Vibration (g or raw accel)
  //   field6 = Threat Level (0-3)
  //   field7 = Fault Code (0-7)
  //   field8 = Health Score (0-100) ← NEW
  //
  // ThingSpeak.setField(1, rpm);
  // ThingSpeak.setField(2, voltage_dc);
  // ThingSpeak.setField(3, current_A);
  // ThingSpeak.setField(4, temperature);
  // ThingSpeak.setField(5, vibration_g);
  // ThingSpeak.setField(6, threatLevel);
  // ThingSpeak.setField(7, faultCode);
  // ThingSpeak.setField(8, healthScore);  ← ADD THIS
  // ThingSpeak.writeFields(myChannelNumber, myWriteAPIKey);

  delay(500); // 500 ms balances all sensor update rates
}
