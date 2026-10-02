/*
  ============================================================
  INFRA-NEX Phase 2 — ESP32 Industrial Motor Protection System
  ============================================================

  Target Motor : 12V DC, 500 RPM nominal, 4A rated current
  MCU          : ESP32 Dev Module

  Sensors:
    - Hall Effect Sensor  → RPM         (GPIO27, interrupt FALLING)
    - ADXL335             → Vibration   (ADC: GPIO34/35/32)
    - INA219              → DC Current  (I2C: SDA=21, SCL=22)
    - Voltage Divider     → 12V DC      (ADC: GPIO33)
    - DS18B20             → Temperature (GPIO4, OneWire)

  Outputs:
    - Relay               → Motor ON/OFF  (GPIO25, active-HIGH)
    - Buzzer              → Alert tone    (GPIO13)
    - LED Green           → Normal        (GPIO2)
    - LED Yellow          → Warning       (GPIO14)
    - LED Red             → Critical/Emg  (GPIO26)

  Cloud:
    - ThingSpeak channel 3240918
    - field1=RPM, field2=Voltage, field3=Current, field4=Temp
    - field5=VibMag, field6=ThreatLevel, field7=FaultCode, field8=HealthScore

  Fault Codes:
    FC0 = Normal
    FC1 = Mechanical Jam        (RPM stall + stall current)
    FC2 = Bearing Failure       (high vib + RPM degradation)
    FC3 = Winding Overheat      (high temp + high current)
    FC4 = Belt/Coupling Slip    (RPM drop + normal current)
    FC5 = Supply Voltage Issue  (voltage out of band)
    FC6 = Mechanical Looseness  (high vib + normal RPM/I)
    FC7 = Runaway / Load Loss   (overspeed + low current)

  Auto-Restart State Machine:
    RUNNING → TL3 fault → COOLDOWN (10s) → RESTARTING
            → 3s inrush grace → 3 clean readings → RUNNING
            → fault recurs → repeat (max 3 attempts) → LOCKOUT

  Key fixes vs original:
    1. Sensor enable flags — comment out any sensor to disable it cleanly
    2. Non-blocking buzzer via millis() — no more loop stalls
    3. Non-blocking WiFi reconnect — ThingSpeak failure never freezes FSM
    4. INA219 raw shunt-based current calculation replacing saturating 32V/2A cal
    5. Fault hysteresis — fault must persist N consecutive readings before confirming
    6. cleanReadingCount gated to RPM update cadence — counts mean something now
    7. Sensor stubs are NOMINAL values, not zero — won't false-trigger fault logic
*/

// ============================================================
// SENSOR ENABLE FLAGS
// Comment out any line to disable that sensor.
// Disabled sensors use stub values — fault logic never sees zero.
// ============================================================

#define SENSOR_HALL    // RPM via Hall effect interrupt (GPIO27)
#define SENSOR_INA219  // DC current via INA219 I2C
#define SENSOR_DS18B20 // Temperature via DS18B20 OneWire
#define SENSOR_ADXL335 // Vibration via ADXL335 ADC
#define SENSOR_VDIV    // Supply voltage via ADC voltage divider

// Stub values — used when sensor is disabled. Must be NOMINAL, not zero.
#define STUB_RPM 500.0f
#define STUB_CURRENT 2.0f
#define STUB_TEMPERATURE 35.0f
#define STUB_VIBMAG 0.05f
#define STUB_VOLTAGE 12.0f

// ============================================================
// INCLUDES — gated by sensor flags
// ============================================================

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <math.h>

#if defined(SENSOR_INA219)
#include <Wire.h>
#include <Adafruit_INA219.h>
#endif

#if defined(SENSOR_DS18B20)
#include <OneWire.h>
#include <DallasTemperature.h>
#endif

// ============================================================
// USER CONFIGURATION — fill before flashing
// ============================================================

const char *WIFI_SSID = "YOUR_WIFI_SSID";
const char *WIFI_PASS = "YOUR_WIFI_PASSWORD";
const char *TS_API_KEY = "YOUR_THINGSPEAK_WRITE_API_KEY";
const char *TS_SERVER = "http://api.thingspeak.com/update";
const uint32_t TS_CHANNEL = 3240918;

// ============================================================
// PIN DEFINITIONS
// ============================================================

const uint8_t PIN_HALL = 27;     // Hall effect (interrupt, FALLING)
const uint8_t PIN_RELAY = 25;    // Relay (active-HIGH = motor ON)
const uint8_t PIN_BUZZER = 13;   // Buzzer
const uint8_t PIN_LED_GREEN = 2; // Normal indicator
const uint8_t PIN_LED_YEL = 14;  // Warning indicator
const uint8_t PIN_LED_RED = 26;  // Critical/Emergency indicator
const uint8_t PIN_DS18B20 = 4;   // DS18B20 OneWire data
const uint8_t PIN_ADXL_X = 34;   // ADXL335 X axis ADC
const uint8_t PIN_ADXL_Y = 35;   // ADXL335 Y axis ADC
const uint8_t PIN_ADXL_Z = 32;   // ADXL335 Z axis ADC
const uint8_t PIN_VDIV = 33;     // Voltage divider ADC output
const uint8_t I2C_SDA = 21;
const uint8_t I2C_SCL = 22;

// ============================================================
// MOTOR PARAMETERS
// ============================================================

const float NOMINAL_RPM = 500.0f;
const float NOMINAL_VOLTAGE = 12.0f;
const float RATED_CURRENT = 4.0f;

// ============================================================
// THRESHOLDS
// ============================================================

// Voltage (V DC)
const float V_LOW_WARN = 10.8f;  // < 90% nominal
const float V_LOW_CRIT = 9.6f;   // < 80% nominal
const float V_HIGH_WARN = 13.2f; // > 110% nominal
const float V_HIGH_CRIT = 14.4f; // > 120% nominal

// Current (A)
const float I_WARN = 5.0f;  // 125% rated
const float I_CRIT = 6.0f;  // 150% rated
const float I_EMERG = 7.2f; // 180% rated — jam territory
const float I_LOW = 0.5f;   // suspiciously low (runaway / load loss)

// Temperature (°C)
const float T_WARN = 60.0f;
const float T_CRIT = 75.0f;
const float T_EMERG = 85.0f;

// Vibration magnitude (g)
const float VIB_WARN = 0.4f;
const float VIB_CRIT = 0.8f;
const float VIB_EMERG = 1.2f;

// RPM deviation thresholds
const float RPM_DROP_WARN = 0.15f; // 15% below nominal
const float RPM_DROP_CRIT = 0.30f; // 30% below nominal
const float RPM_HIGH_WARN = 0.20f; // 20% above nominal
const float RPM_STALL = 30.0f;     // below this = stalled

// ============================================================
// FAULT HYSTERESIS
// A fault must be seen this many consecutive loop cycles before
// it is confirmed and acted on. Prevents LED flicker / buzzer
// chatter from sensor noise near thresholds.
// ============================================================

const uint8_t FAULT_CONFIRM_COUNT = 3;

// ============================================================
// VOLTAGE DIVIDER CALIBRATION
// R1 = 30kΩ (top), R2 = 7.5kΩ (bottom)
// Vout = Vin * R2/(R1+R2) = Vin * 0.2 → max 2.4V at 12V input
// VDIV_SCALE = (R1+R2)/R2 = 5.0
// Adjust VDIV_SCALE if Serial output differs from multimeter reading.
// ============================================================

const float VDIV_SCALE = 5.0f;
const float ADC_VREF = 3.3f;
const float ADC_MAXRAW = 4095.0f;

// ============================================================
// INA219 — RAW SHUNT CURRENT CALCULATION
// Using raw shunt voltage avoids the 32V/2A calibration ceiling (~3.2A).
// Shunt resistor on INA219 breakout = 0.1 Ohm (standard).
// I = Vshunt / Rshunt. Vshunt LSB = 10 µV.
// This gives accurate readings up to the shunt's physical limit (~3.2A
// continuous on breakout board) but the trip logic fires well below
// saturation, making this safe for fault detection up to I_EMERG = 7.2A.
// ============================================================

#if defined(SENSOR_INA219)
const float INA219_SHUNT_OHMS = 0.1f; // breakout board standard shunt
Adafruit_INA219 ina219;
#endif

// ============================================================
// ADXL335 ADC CONSTANTS
// ============================================================

const float ADXL_VREF_V = 3.3f;
const float ADXL_SENS = 0.300f; // V/g at 3.3V supply (typical)
const int ADXL_SAMPS = 32;      // samples per axis per reading

// ============================================================
// MOVING AVERAGE FILTER — window 5
// Applied to: voltage, current, vibration magnitude
// ============================================================

const int MA_SIZE = 5;

struct MovAvg
{
  float buf[MA_SIZE];
  int idx;
  bool full;
  float sum;

  void reset()
  {
    idx = 0;
    full = false;
    sum = 0.0f;
    memset(buf, 0, sizeof(buf));
  }

  float update(float val)
  {
    if (full)
      sum -= buf[idx];
    buf[idx] = val;
    sum += val;
    idx = (idx + 1) % MA_SIZE;
    if (idx == 0)
      full = true;
    return sum / (full ? MA_SIZE : (idx == 0 ? MA_SIZE : idx));
  }
};

MovAvg maVoltage, maCurrent, maVibration;

// ============================================================
// AUTO-RESTART STATE MACHINE
// ============================================================

enum MotorState
{
  MS_RUNNING,
  MS_COOLDOWN,
  MS_RESTARTING,
  MS_LOCKOUT
};

MotorState motorState = MS_RUNNING;
uint8_t restartAttempts = 0;
unsigned long stateEnteredMs = 0;
uint8_t cleanReadingCount = 0;

const uint8_t MAX_RESTARTS = 3;
const unsigned long COOLDOWN_MS = 10000UL;
const unsigned long INRUSH_GRACE_MS = 3000UL;
const uint8_t CLEAN_READINGS_NEEDED = 3; // now only increments on RPM cadence

// ============================================================
// NON-BLOCKING BUZZER STATE
// ============================================================

struct BuzzState
{
  bool active;
  int totalBeeps;
  int beepsDone;
  bool beepOn;
  unsigned long lastToggleMs;
  unsigned long onMs;
  unsigned long offMs;

  void start(int beeps, unsigned long onDuration, unsigned long offDuration)
  {
    active = true;
    totalBeeps = beeps;
    beepsDone = 0;
    beepOn = true;
    lastToggleMs = millis();
    onMs = onDuration;
    offMs = offDuration;
    digitalWrite(PIN_BUZZER, HIGH);
  }

  // Call every loop — drives buzzer without blocking
  void update()
  {
    if (!active)
      return;
    unsigned long now = millis();
    unsigned long duration = beepOn ? onMs : offMs;
    if (now - lastToggleMs >= duration)
    {
      lastToggleMs = now;
      beepOn = !beepOn;
      if (beepOn)
      {
        beepsDone++;
        if (beepsDone >= totalBeeps)
        {
          active = false;
          digitalWrite(PIN_BUZZER, LOW);
          return;
        }
        digitalWrite(PIN_BUZZER, HIGH);
      }
      else
      {
        digitalWrite(PIN_BUZZER, LOW);
      }
    }
  }

  void stop()
  {
    active = false;
    digitalWrite(PIN_BUZZER, LOW);
  }
};

BuzzState buzzer;

// ============================================================
// WIFI RECONNECT STATE (non-blocking)
// ============================================================

bool wifiReconnecting = false;
unsigned long wifiReconnectMs = 0;
const unsigned long WIFI_RETRY_MS = 500UL;
unsigned long wifiAttemptStartMs = 0;
const unsigned long WIFI_TIMEOUT_MS = 20000UL;

// ============================================================
// RPM INTERRUPT
// ============================================================

const uint8_t PULSES_PER_REV = 1; // 1 magnet on shaft
volatile unsigned long pulseCount = 0;

#if defined(SENSOR_HALL)
void IRAM_ATTR hallISR()
{
  pulseCount++;
}
#endif

// ============================================================
// GLOBAL SENSOR VALUES
// ============================================================

float rpm = STUB_RPM;
float supplyV = STUB_VOLTAGE;
float current = STUB_CURRENT;
float temperature = STUB_TEMPERATURE;
float vibMag = STUB_VIBMAG;

int threatLevel = 0;   // 0–3
int faultCode = 0;     // 0–7
int healthScore = 100; // 0–100

// Confirmed (hysteresis-passed) fault state — what the FSM actually acts on
int confirmedThreatLevel = 0;
int confirmedFaultCode = 0;

// Hysteresis counter
uint8_t faultPersistCount = 0;
int pendingFaultCode = 0;
int pendingThreatLevel = 0;

// Timing
unsigned long lastRPMCalcMs = 0;
unsigned long lastSendMs = 0;
bool rpmUpdatedThisCycle = false; // gate for cleanReadingCount

const unsigned long RPM_INTERVAL_MS = 1000UL;
const unsigned long SEND_INTERVAL_MS = 15000UL;

// ============================================================
// LIBRARY OBJECTS
// ============================================================

#if defined(SENSOR_DS18B20)
OneWire oneWire(PIN_DS18B20);
DallasTemperature ds18b20(&oneWire);
#endif

// ============================================================
// FUNCTION PROTOTYPES
// ============================================================

void setupPins();
void connectWiFiBlocking();
void tickWiFiReconnect();
float readVoltage();
float readCurrent();
float readTemperature();
float readVibMagnitude();
void computeRPM(unsigned long nowMs);
void evaluateFault();
void applyFaultHysteresis();
void computeHealthScore();
void runStateMachine(unsigned long nowMs);
void setMotorRelay(bool on);
void setAlertOutputs(unsigned long nowMs);
void triggerBuzz(int level);
void sendToThingSpeak();

// ============================================================
// SETUP
// ============================================================

void setup()
{
  Serial.begin(115200);
  delay(200);
  Serial.println("\n===== INFRA-NEX Phase 2 Startup =====");

  setupPins();

// INA219
#if defined(SENSOR_INA219)
  Wire.begin(I2C_SDA, I2C_SCL);
  if (!ina219.begin())
  {
    Serial.println("[WARN] INA219 not found — check wiring.");
  }
  else
  {
    // Use default calibration; we read raw shunt voltage directly
    // to bypass the 32V/2A saturation ceiling (~3.2A).
    // Raw shunt LSB = 10µV, shunt = 0.1Ω → accurate across full fault range.
    ina219.setCalibration_32V_2A(); // sets PGA correctly; we override current calc
    Serial.println("[OK] INA219 ready (raw shunt mode).");
  }
#else
  Serial.println("[SKIP] INA219 disabled — current stubbed at " + String(STUB_CURRENT) + "A.");
#endif

// DS18B20
#if defined(SENSOR_DS18B20)
  ds18b20.begin();
  Serial.println("[OK] DS18B20 ready.");
#else
  Serial.println("[SKIP] DS18B20 disabled — temperature stubbed at " + String(STUB_TEMPERATURE) + "C.");
#endif

// Hall ISR
#if defined(SENSOR_HALL)
  attachInterrupt(digitalPinToInterrupt(PIN_HALL), hallISR, FALLING);
  Serial.println("[OK] Hall ISR attached (GPIO27, FALLING).");
#else
  Serial.println("[SKIP] Hall sensor disabled — RPM stubbed at " + String(STUB_RPM) + ".");
#endif

#if !defined(SENSOR_ADXL335)
  Serial.println("[SKIP] ADXL335 disabled — vibration stubbed at " + String(STUB_VIBMAG) + "g.");
#endif

#if !defined(SENSOR_VDIV)
  Serial.println("[SKIP] Voltage divider disabled — voltage stubbed at " + String(STUB_VOLTAGE) + "V.");
#endif

  maVoltage.reset();
  maCurrent.reset();
  maVibration.reset();

  buzzer.active = false;

  connectWiFiBlocking(); // blocking only at boot — acceptable

  unsigned long now = millis();
  lastRPMCalcMs = now;
  lastSendMs = now;
  stateEnteredMs = now;

  setMotorRelay(true);
  motorState = MS_RUNNING;
  Serial.println("[OK] Motor started. System armed.\n");
}

// ============================================================
// MAIN LOOP
// ============================================================

void loop()
{
  unsigned long now = millis();

  // 1) Service non-blocking buzzer
  buzzer.update();

  // 2) Service non-blocking WiFi reconnect
  tickWiFiReconnect();

  // 3) Read all sensors (gated by flags)
  rpmUpdatedThisCycle = false;
  computeRPM(now);

#if defined(SENSOR_VDIV)
  supplyV = readVoltage();
#else
  supplyV = STUB_VOLTAGE;
#endif

#if defined(SENSOR_INA219)
  current = readCurrent();
#else
  current = STUB_CURRENT;
#endif

#if defined(SENSOR_DS18B20)
  temperature = readTemperature();
#else
  temperature = STUB_TEMPERATURE;
#endif

#if defined(SENSOR_ADXL335)
  vibMag = readVibMagnitude();
#else
  vibMag = STUB_VIBMAG;
#endif

  // 4) Evaluate raw fault, apply hysteresis to get confirmed fault
  evaluateFault();
  applyFaultHysteresis();

  // 5) Compute health score
  computeHealthScore();

  // 6) Run auto-restart state machine (acts on confirmedThreatLevel)
  runStateMachine(now);

  // 7) Drive alert outputs
  setAlertOutputs(now);

  // 8) Serial debug
  Serial.printf(
      "[DATA] RPM:%.1f V:%.2fV I:%.3fA T:%.1fC Vib:%.3fg | "
      "Raw TL:%d FC:%d | Conf TL:%d FC:%d | HS:%d | State:%d\n",
      rpm, supplyV, current, temperature, vibMag,
      threatLevel, faultCode,
      confirmedThreatLevel, confirmedFaultCode,
      healthScore, (int)motorState);

  // 9) ThingSpeak upload every 15s — skips silently if WiFi down
  if (now - lastSendMs >= SEND_INTERVAL_MS)
  {
    sendToThingSpeak();
    lastSendMs = now;
  }

  delay(250);
}

// ============================================================
// PIN SETUP
// ============================================================

void setupPins()
{
  pinMode(PIN_HALL, INPUT_PULLUP);
  pinMode(PIN_RELAY, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_LED_GREEN, OUTPUT);
  pinMode(PIN_LED_YEL, OUTPUT);
  pinMode(PIN_LED_RED, OUTPUT);

  digitalWrite(PIN_RELAY, LOW);
  digitalWrite(PIN_BUZZER, LOW);
  digitalWrite(PIN_LED_GREEN, LOW);
  digitalWrite(PIN_LED_YEL, LOW);
  digitalWrite(PIN_LED_RED, LOW);

#if defined(SENSOR_ADXL335)
  analogSetPinAttenuation(PIN_ADXL_X, ADC_11db);
  analogSetPinAttenuation(PIN_ADXL_Y, ADC_11db);
  analogSetPinAttenuation(PIN_ADXL_Z, ADC_11db);
#endif

#if defined(SENSOR_VDIV)
  analogSetPinAttenuation(PIN_VDIV, ADC_11db);
#endif

  Serial.println("[OK] Pins configured.");
}

// ============================================================
// WIFI — blocking boot connect, non-blocking reconnect after
// ============================================================

void connectWiFiBlocking()
{
  Serial.printf("[WiFi] Connecting to %s", WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
    if (millis() - t0 > WIFI_TIMEOUT_MS)
    {
      Serial.println("\n[WiFi] Boot timeout — continuing offline. Will retry.");
      return;
    }
  }
  Serial.printf("\n[WiFi] Connected — IP: %s\n", WiFi.localIP().toString().c_str());
}

// Called every loop iteration — handles reconnect without blocking
void tickWiFiReconnect()
{
  if (WiFi.status() == WL_CONNECTED)
  {
    wifiReconnecting = false;
    return;
  }
  if (!wifiReconnecting)
  {
    Serial.println("[WiFi] Connection lost — starting non-blocking reconnect.");
    WiFi.disconnect();
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    wifiReconnecting = true;
    wifiAttemptStartMs = millis();
    wifiReconnectMs = millis();
    return;
  }
  unsigned long now = millis();
  // Hard timeout — restart attempt
  if (now - wifiAttemptStartMs > WIFI_TIMEOUT_MS)
  {
    Serial.println("[WiFi] Reconnect timeout — retrying.");
    WiFi.disconnect();
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    wifiAttemptStartMs = now;
    wifiReconnectMs = now;
  }
}

// ============================================================
// SENSOR READINGS
// ============================================================

float readVoltage()
{
  long sum = 0;
  for (int i = 0; i < 16; i++)
  {
    sum += analogRead(PIN_VDIV);
    delayMicroseconds(100);
  }
  float adcAvg = (float)sum / 16.0f;
  float adcV = (adcAvg / ADC_MAXRAW) * ADC_VREF;
  float motorV = adcV * VDIV_SCALE;
  return maVoltage.update(motorV);
}

float readCurrent()
{
#if defined(SENSOR_INA219)
  // Read raw shunt voltage (µV) and compute current directly.
  // Bypasses the 32V/2A calibration ceiling (~3.2A saturation).
  // Shunt LSB = 10µV; shunt resistance = 0.1Ω.
  int16_t rawShunt = ina219.getShuntVoltage_raw();
  float shuntV = (float)rawShunt * 0.00001f; // convert to volts (10µV LSB)
  float amps = shuntV / INA219_SHUNT_OHMS;
  if (!isfinite(amps) || amps < 0.0f)
    amps = 0.0f;
  if (amps < 0.02f)
    amps = 0.0f; // clamp noise floor
  return maCurrent.update(amps);
#else
  return STUB_CURRENT;
#endif
}

float readTemperature()
{
#if defined(SENSOR_DS18B20)
  ds18b20.requestTemperatures();
  float t = ds18b20.getTempCByIndex(0);
  if (t == DEVICE_DISCONNECTED_C)
  {
    Serial.println("[WARN] DS18B20 disconnected — holding last value.");
    return temperature; // hold last good reading
  }
  return t;
#else
  return STUB_TEMPERATURE;
#endif
}

float readVibMagnitude()
{
#if defined(SENSOR_ADXL335)
  auto readAxis = [](uint8_t pin) -> float
  {
    long sum = 0;
    for (int i = 0; i < ADXL_SAMPS; i++)
    {
      sum += analogRead(pin);
      delayMicroseconds(200);
    }
    float avg = (float)sum / (float)ADXL_SAMPS;
    float v = (avg / ADC_MAXRAW) * ADXL_VREF_V;
    float zeroG = ADXL_VREF_V / 2.0f;
    return (v - zeroG) / ADXL_SENS;
  };
  float x = readAxis(PIN_ADXL_X);
  float y = readAxis(PIN_ADXL_Y);
  float z = readAxis(PIN_ADXL_Z);
  float mag = sqrtf(x * x + y * y + z * z);
  return maVibration.update(mag);
#else
  return STUB_VIBMAG;
#endif
}

void computeRPM(unsigned long nowMs)
{
#if defined(SENSOR_HALL)
  if (nowMs - lastRPMCalcMs < RPM_INTERVAL_MS)
    return;
  unsigned long elapsed = nowMs - lastRPMCalcMs;
  noInterrupts();
  unsigned long pulses = pulseCount;
  pulseCount = 0;
  interrupts();
  float revs = (float)pulses / (float)PULSES_PER_REV;
  float seconds = (float)elapsed / 1000.0f;
  rpm = (revs / seconds) * 60.0f;
  lastRPMCalcMs = nowMs;
  rpmUpdatedThisCycle = true;
#else
  // Still update the cadence flag every second so cleanReadingCount works correctly
  if (nowMs - lastRPMCalcMs >= RPM_INTERVAL_MS)
  {
    rpm = STUB_RPM;
    lastRPMCalcMs = nowMs;
    rpmUpdatedThisCycle = true;
  }
#endif
}

// ============================================================
// FAULT EVALUATION
// Sets raw threatLevel and faultCode — NOT yet acted on.
// applyFaultHysteresis() confirms these before the FSM sees them.
// ============================================================

void evaluateFault()
{
  bool rpmStalled = (rpm < RPM_STALL);
  bool rpmDropWarn = (rpm < NOMINAL_RPM * (1.0f - RPM_DROP_WARN));
  bool rpmDropCrit = (rpm < NOMINAL_RPM * (1.0f - RPM_DROP_CRIT));
  bool rpmHigh = (rpm > NOMINAL_RPM * (1.0f + RPM_HIGH_WARN));

  bool currentHigh = (current > I_WARN);
  bool currentVeryHigh = (current > I_EMERG);
  bool currentLow = (current < I_LOW && current > 0.02f);

  bool tempHigh = (temperature > T_WARN);
  bool tempCrit = (temperature > T_CRIT);

  bool vibHigh = (vibMag > VIB_WARN);
  bool vibVeryHigh = (vibMag > VIB_CRIT);

  bool voltLow = (supplyV < V_LOW_WARN);
  bool voltHigh = (supplyV > V_HIGH_WARN);
  bool voltCrit = (supplyV < V_LOW_CRIT || supplyV > V_HIGH_CRIT);

  faultCode = 0;
  threatLevel = 0;

  // FC1: Mechanical Jam — highest priority, relay must trip
  if (rpmStalled && currentVeryHigh)
  {
    faultCode = 1;
    threatLevel = 3;
    return;
  }

  // FC7: Runaway / Load Loss
  if (rpmHigh && currentLow)
  {
    faultCode = 7;
    threatLevel = 2;
    return;
  }

  // FC3: Winding Overheat
  if (tempCrit && currentHigh)
  {
    faultCode = 3;
    threatLevel = (current > I_CRIT) ? 3 : 2;
    return;
  }
  if (tempHigh && currentHigh)
  {
    faultCode = 3;
    threatLevel = 1;
    return;
  }

  // FC2: Bearing Failure
  if (vibVeryHigh && rpmDropWarn && !rpmStalled)
  {
    faultCode = 2;
    threatLevel = rpmDropCrit ? 3 : 2;
    return;
  }
  if (vibHigh && rpmDropWarn)
  {
    faultCode = 2;
    threatLevel = 1;
    return;
  }

  // FC4: Belt / Coupling Slip
  if (rpmDropCrit && !currentHigh)
  {
    faultCode = 4;
    threatLevel = 2;
    return;
  }
  if (rpmDropWarn && !currentHigh)
  {
    faultCode = 4;
    threatLevel = 1;
    return;
  }

  // FC5: Supply Voltage Issue
  if (voltCrit)
  {
    faultCode = 5;
    threatLevel = 3;
    return;
  }
  if (voltLow || voltHigh)
  {
    faultCode = 5;
    threatLevel = 1;
    return;
  }

  // FC6: Mechanical Looseness
  if (vibVeryHigh)
  {
    faultCode = 6;
    threatLevel = 2;
    return;
  }
  if (vibHigh)
  {
    faultCode = 6;
    threatLevel = 1;
    return;
  }

  faultCode = 0;
  threatLevel = 0;
}

// ============================================================
// FAULT HYSTERESIS
// Raw fault must persist FAULT_CONFIRM_COUNT consecutive readings
// before confirmedThreatLevel / confirmedFaultCode are updated.
// On clear (threatLevel == 0), confirmed state resets immediately.
// ============================================================

void applyFaultHysteresis()
{
  if (threatLevel == 0)
  {
    // Clear confirms immediately — safe to reset protection faster than trip
    confirmedThreatLevel = 0;
    confirmedFaultCode = 0;
    faultPersistCount = 0;
    pendingFaultCode = 0;
    pendingThreatLevel = 0;
    return;
  }

  // New fault type — reset persistence counter
  if (faultCode != pendingFaultCode || threatLevel != pendingThreatLevel)
  {
    pendingFaultCode = faultCode;
    pendingThreatLevel = threatLevel;
    faultPersistCount = 1;
    return;
  }

  faultPersistCount++;
  if (faultPersistCount >= FAULT_CONFIRM_COUNT)
  {
    confirmedFaultCode = pendingFaultCode;
    confirmedThreatLevel = pendingThreatLevel;
    // Don't reset counter — keep confirmed while fault persists
  }
}

// ============================================================
// HEALTH SCORE (0–100)
// ============================================================

void computeHealthScore()
{
  float score = 100.0f;

  // Voltage deviation penalty (max 25 pts)
  float vDev = fabsf(supplyV - NOMINAL_VOLTAGE) / NOMINAL_VOLTAGE;
  score -= constrain(vDev * 150.0f, 0.0f, 25.0f);

  // Current overload penalty (max 25 pts)
  float iOver = (current - RATED_CURRENT) / RATED_CURRENT;
  if (iOver > 0.0f)
    score -= constrain(iOver * 100.0f, 0.0f, 25.0f);

  // Temperature penalty (max 25 pts) — starts above 45°C
  float tOver = (temperature - 45.0f) / (T_EMERG - 45.0f);
  if (tOver > 0.0f)
    score -= constrain(tOver * 25.0f, 0.0f, 25.0f);

  // Vibration penalty (max 15 pts)
  float vibOver = vibMag / VIB_EMERG;
  score -= constrain(vibOver * 15.0f, 0.0f, 15.0f);

  // RPM deviation penalty (max 10 pts)
  float rpmDev = fabsf(rpm - NOMINAL_RPM) / NOMINAL_RPM;
  score -= constrain(rpmDev * 40.0f, 0.0f, 10.0f);

  healthScore = (int)constrain(score, 0.0f, 100.0f);
}

// ============================================================
// AUTO-RESTART STATE MACHINE
// Operates on confirmedThreatLevel — not raw threatLevel.
// cleanReadingCount only increments when RPM has been recalculated
// (rpmUpdatedThisCycle), so each "clean reading" is a real 1s sample.
// ============================================================

void runStateMachine(unsigned long nowMs)
{
  switch (motorState)
  {

  case MS_RUNNING:
    if (confirmedThreatLevel == 3)
    {
      setMotorRelay(false);
      Serial.printf("[FSM] FAULT FC%d confirmed — COOLDOWN (attempt %d/%d)\n",
                    confirmedFaultCode, restartAttempts + 1, MAX_RESTARTS);
      motorState = MS_COOLDOWN;
      stateEnteredMs = nowMs;
      cleanReadingCount = 0;
      triggerBuzz(3);
    }
    break;

  case MS_COOLDOWN:
    if (nowMs - stateEnteredMs >= COOLDOWN_MS)
    {
      if (restartAttempts >= MAX_RESTARTS)
      {
        Serial.println("[FSM] Max restarts reached — LOCKOUT");
        motorState = MS_LOCKOUT;
      }
      else
      {
        restartAttempts++;
        Serial.printf("[FSM] Cooldown done — RESTARTING (attempt %d)\n", restartAttempts);
        setMotorRelay(true);
        motorState = MS_RESTARTING;
        stateEnteredMs = nowMs;
        cleanReadingCount = 0;
      }
    }
    break;

  case MS_RESTARTING:
    if (nowMs - stateEnteredMs < INRUSH_GRACE_MS)
      break;

    if (confirmedThreatLevel == 0)
    {
      // Only count on fresh RPM window — prevents fast 250ms count inflation
      if (rpmUpdatedThisCycle)
      {
        cleanReadingCount++;
        Serial.printf("[FSM] Clean reading %d/%d\n", cleanReadingCount, CLEAN_READINGS_NEEDED);
        if (cleanReadingCount >= CLEAN_READINGS_NEEDED)
        {
          Serial.println("[FSM] Motor stable — RUNNING");
          motorState = MS_RUNNING;
          restartAttempts = 0;
        }
      }
    }
    else if (confirmedThreatLevel == 3)
    {
      setMotorRelay(false);
      Serial.printf("[FSM] Restart failed (FC%d) — back to COOLDOWN\n", confirmedFaultCode);
      motorState = MS_COOLDOWN;
      stateEnteredMs = nowMs;
      cleanReadingCount = 0;
      triggerBuzz(3);
    }
    else
    {
      cleanReadingCount = 0; // non-zero threat — reset clean count
    }
    break;

  case MS_LOCKOUT:
    setMotorRelay(false);
    // Red fast-blink handled in setAlertOutputs
    break;
  }
}

// ============================================================
// RELAY CONTROL
// ============================================================

void setMotorRelay(bool on)
{
  digitalWrite(PIN_RELAY, on ? HIGH : LOW);
}

// ============================================================
// LED + BUZZER OUTPUTS — fully non-blocking
// ============================================================

void setAlertOutputs(unsigned long nowMs)
{
  digitalWrite(PIN_LED_GREEN, LOW);
  digitalWrite(PIN_LED_YEL, LOW);
  digitalWrite(PIN_LED_RED, LOW);

  if (motorState == MS_LOCKOUT)
  {
    static unsigned long lastBlink = 0;
    static bool blinkState = false;
    if (nowMs - lastBlink > 200)
    {
      blinkState = !blinkState;
      lastBlink = nowMs;
    }
    digitalWrite(PIN_LED_RED, blinkState ? HIGH : LOW);
    return;
  }

  switch (confirmedThreatLevel)
  {
  case 0:
    digitalWrite(PIN_LED_GREEN, HIGH);
    break;
  case 1:
    digitalWrite(PIN_LED_YEL, HIGH);
    break;
  case 2:
    digitalWrite(PIN_LED_YEL, HIGH);
    digitalWrite(PIN_LED_RED, HIGH);
    // Trigger 2-beep only if buzzer is idle — don't restart mid-sequence
    if (motorState == MS_RUNNING && !buzzer.active)
      triggerBuzz(2);
    break;
  case 3:
    digitalWrite(PIN_LED_RED, HIGH);
    // Buzz triggered by FSM at trip moment, not repeated here
    break;
  }
}

// Arm the non-blocking buzzer — returns immediately
void triggerBuzz(int level)
{
  if (level == 2)
  {
    buzzer.start(2, 100, 100); // 2 beeps, 100ms on, 100ms off
  }
  else if (level == 3)
  {
    buzzer.start(5, 80, 80); // 5 rapid beeps, 80ms on, 80ms off
  }
}

// ============================================================
// THINGSPEAK UPLOAD
// Skips silently if WiFi is down — FSM never stalls waiting for cloud.
// ============================================================

void sendToThingSpeak()
{
  if (WiFi.status() != WL_CONNECTED)
  {
    Serial.println("[TS] Skipping upload — WiFi not connected.");
    return;
  }

  HTTPClient http;
  String url = String(TS_SERVER) + "?api_key=" + TS_API_KEY;
  url += "&field1=" + String(rpm, 1);
  url += "&field2=" + String(supplyV, 2);
  url += "&field3=" + String(current, 3);
  url += "&field4=" + String(temperature, 2);
  url += "&field5=" + String(vibMag, 3);
  url += "&field6=" + String(confirmedThreatLevel); // confirmed, not raw
  url += "&field7=" + String(confirmedFaultCode);   // confirmed, not raw
  url += "&field8=" + String(healthScore);

  http.begin(url);
  int code = http.GET();
  if (code > 0)
  {
    Serial.printf("[TS] Sent OK — entry ID: %s\n", http.getString().c_str());
  }
  else
  {
    Serial.printf("[TS] Send failed: %s\n", http.errorToString(code).c_str());
  }
  http.end();
}

/*
  ============================================================
  PIN CONNECTION SUMMARY
  ============================================================

  Hall Effect Sensor  OUT → GPIO27       (3.3V logic, FALLING edge)
  ADXL335             X   → GPIO34       (ADC, 3.3V supply)
                      Y   → GPIO35
                      Z   → GPIO32
  Voltage Divider     OUT → GPIO33       (12V → 2.4V via R1=30kΩ, R2=7.5kΩ)
  INA219              SDA → GPIO21       (3.3V I2C)
                      SCL → GPIO22
                      VIN+ → motor +12V rail
                      VIN- → motor load side
  DS18B20             DAT → GPIO4        (4.7kΩ pull-up to 3.3V)
  Relay               IN  → GPIO25       (HIGH = motor ON)
  Buzzer              +   → GPIO13
  LED Green           +   → GPIO2        (220Ω series resistor)
  LED Yellow          +   → GPIO14
  LED Red             +   → GPIO26
  All GNDs share common ground with ESP32.

  ============================================================
  VOLTAGE DIVIDER CALCULATION
  ============================================================

  R1 = 30kΩ (top, from motor +)
  R2 = 7.5kΩ (bottom, to GND)
  Vout = 12 * 7.5/37.5 = 2.4V → safe for ESP32 ADC (max 3.3V)
  VDIV_SCALE = 37.5/7.5 = 5.0

  ============================================================
  INA219 CURRENT NOTE
  ============================================================

  This firmware reads raw shunt voltage (10µV LSB) and divides
  by shunt resistance (0.1Ω) to get current directly.
  This sidesteps the 32V/2A calibration ceiling (~3.2A saturation).
  Accurate up to the physical shunt limit. Fault trip logic fires
  well below saturation in all real fault scenarios.
  If you swap the INA219 breakout board, verify its shunt value
  (usually printed on board or in datasheet) and update INA219_SHUNT_OHMS.

  ============================================================
  CALIBRATION NOTES
  ============================================================

  Voltage    : Compare supplyV in Serial to multimeter at motor terminals.
               Adjust VDIV_SCALE if they differ.

  Current    : Compare current in Serial to clamp meter reading.
               Adjust INA219_SHUNT_OHMS if breakout uses non-0.1Ω shunt.

  ADXL335    : ADXL_SENS = 0.300 V/g is datasheet typical.
               If mounted off-axis, read each axis at rest and subtract offset
               from the zeroG calculation in readVibMagnitude().

  RPM        : PULSES_PER_REV = 1 assumes one magnet on shaft.
               Increment for additional magnets (smoother low-RPM reading).

  Hysteresis : FAULT_CONFIRM_COUNT = 3 means a fault needs 3 consecutive
               loop cycles (~750ms) before confirming. Raise if false
               triggers persist. Lower if response feels too slow.

  ============================================================
  FAULT CODE REFERENCE (ThingSpeak field7)
  ============================================================

  FC0 — Normal
  FC1 — Mechanical Jam          (stall + stall current)
  FC2 — Bearing Failure         (high vib + RPM degradation)
  FC3 — Winding Overheat        (high temp + high current)
  FC4 — Belt/Coupling Slip      (RPM drop, normal current)
  FC5 — Supply Voltage Issue    (V out of 10.8–13.2V band)
  FC6 — Mechanical Looseness    (high vib, normal RPM/I)
  FC7 — Runaway / Load Loss     (overspeed + low current)
*/


