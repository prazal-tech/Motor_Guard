#include <Wire.h>
#include <Adafruit_INA219.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <WiFi.h>
#include <HTTPClient.h>

// =====================================================
// WIFI + THINGSPEAK CONFIG
// =====================================================
const char* WIFI_SSID     = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

const char* THINGSPEAK_API_KEY = "7DF6OSIMH0V3YCKR";   // ThingSpeak Write API Key
const char* THINGSPEAK_HOST    = "http://api.thingspeak.com/update";

const unsigned long WIFI_RECONNECT_INTERVAL   = 10000;  // try reconnect every 10s if dropped
const unsigned long THINGSPEAK_UPLOAD_INTERVAL = 16000; // ThingSpeak min is 15s, 16s for safety

unsigned long wifiRetryTimer = 0;
unsigned long thingSpeakTimer = 0;

// =====================================================
// GPIO MAP
// =====================================================
const int ADXL_X_PIN   = 34;
const int ADXL_Y_PIN   = 35;
const int ADXL_Z_PIN   = 32;
const int ZMPT_PIN     = 33;
const int HALL_PIN     = 27;
const int ONE_WIRE_BUS = 4;
const int I2C_SDA_PIN  = 21;
const int I2C_SCL_PIN  = 22;
const int RELAY_PIN    = 25;

// =====================================================
// RELAY LOGIC
// =====================================================
const bool RELAY_ACTIVE_LOW = true;

// =====================================================
// SENSOR CALIBRATION CONSTANTS (unchanged from your sensor code)
// =====================================================
const float MAGNETS_PER_REV = 2.0;

const int ADXL_X_ZERO = 1860;
const int ADXL_Y_ZERO = 1865;
const int ADXL_Z_ZERO = 1890;
const float ADXL_SENSITIVITY = 373.0;

const int   ZMPT_SAMPLES     = 400;
const float ZMPT_CALIBRATION = 0.5;
const int   ESP32_DC_OFFSET  = 2048;

const unsigned long READ_INTERVAL = 500;

// =====================================================================
// FAULT THRESHOLDS
// =====================================================================
const float RPM_MIN_RUN        = 400.0;
const float RPM_MAX_RUN        = 900.0;

const float CURR_TRIP_mA       = 60.0;
const float CURR_LOW_mA        = 15.0;

const float VIB_TRIP_g         = 0.90;

const float DC_VOLT_MIN        = 10.0;
const float DC_VOLT_MAX        = 15.0;

const float AC_RMS_MIN         = 280.0;
const float AC_RMS_MAX         = 330.0;

const float TEMP_MAX_C         = 70.0;
// =====================================================================

// =====================================================
// TIMING (debounce + auto-restart)
// =====================================================
const unsigned long FAULT_CONFIRM_MS = 3000;
const unsigned long STARTUP_GRACE_MS = 2000;
const unsigned long RESTART_DELAY_MS = 5000;

// =====================================================
// GLOBALS — sensors
// =====================================================
Adafruit_INA219 ina219;
bool ina219Found = false;

OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);
DeviceAddress sensorAddress;
bool tempSensorFound = false;

unsigned long readTimer = 0;

volatile unsigned long lastPulseTime = 0;
volatile unsigned long pulseInterval = 0;
volatile bool newPulseDetected = false;

bool relayState = false;

bool conversionInProgress = false;
unsigned long conversionStartTime = 0;
unsigned long conversionDelayMs = 188;
float lastValidTempC = DEVICE_DISCONNECTED_C;

// -------- Latest readings cache (used by ThingSpeak uploader) --------
float g_currentRPM   = 0;
float g_vibrationG   = 0;
float g_current_mA   = 0;
float g_acVoltageRMS = 0;
float g_motorVolts   = 0;
float g_tempC        = 0;
bool  g_relayIsOn    = false;
int   g_faultCode    = 0;

// =====================================================
// GLOBALS — fault logic / state machine
// =====================================================
enum SysState { RUNNING, TRIPPED_WAIT };
SysState state = RUNNING;
unsigned long stateChangeTime = 0;
unsigned long motorStartTime  = 0;

struct FaultTimer { bool active = false; unsigned long start = 0; };
FaultTimer rpmLowFault, rpmHighFault, currHighFault, currLowFault, vibFault;
FaultTimer dcLowFault, dcHighFault, acLowFault, acHighFault, tempFault;

enum FaultCode { FC0_NORMAL, FC1_JAM, FC2_BEARING, FC3_OVERHEAT, FC4_SLIP,
                 FC5_SUPPLY, FC6_LOOSENESS, FC7_RUNAWAY };

const char* faultName(FaultCode fc) {
  switch (fc) {
    case FC1_JAM:       return "FC1-MechanicalJam";
    case FC2_BEARING:   return "FC2-BearingFailure";
    case FC3_OVERHEAT:  return "FC3-WindingOverheat";
    case FC4_SLIP:      return "FC4-BeltSlip";
    case FC5_SUPPLY:    return "FC5-SupplyFault";
    case FC6_LOOSENESS: return "FC6-Looseness";
    case FC7_RUNAWAY:   return "FC7-Runaway";
    default:            return "FC0-Normal";
  }
}

void updateTimer(FaultTimer &f, bool bad, unsigned long now) {
  if (bad) { if (!f.active) { f.active = true; f.start = now; } }
  else { f.active = false; }
}
bool isConfirmed(FaultTimer &f, unsigned long now) {
  return f.active && (now - f.start >= FAULT_CONFIRM_MS);
}

// =====================================================
// ISR
// =====================================================
void IRAM_ATTR countPulse() {
  unsigned long currentMicros = micros();
  if (currentMicros - lastPulseTime > 20000) {
    pulseInterval = currentMicros - lastPulseTime;
    lastPulseTime = currentMicros;
    newPulseDetected = true;
  }
}

// =====================================================
// RELAY HELPERS
// =====================================================
void setRelay(bool turnOn) {
  relayState = turnOn;
  if (RELAY_ACTIVE_LOW) digitalWrite(RELAY_PIN, turnOn ? LOW : HIGH);
  else digitalWrite(RELAY_PIN, turnOn ? HIGH : LOW);
}
void relayOn()  { setRelay(true); }
void relayOff() { setRelay(false); }

// =====================================================
// SENSOR HELPERS
// =====================================================
float readZMPT_RMS() {
  long long sumSquares = 0;
  for (int i = 0; i < ZMPT_SAMPLES; i++) {
    int raw = analogRead(ZMPT_PIN) - ESP32_DC_OFFSET;
    sumSquares += (long)raw * raw;
    delayMicroseconds(40);
  }
  float rms = sqrt((float)sumSquares / ZMPT_SAMPLES);
  return rms * ZMPT_CALIBRATION;
}

float readRPM() {
  static float currentRPM = 0.0;
  if (micros() - lastPulseTime > 2000000) {
    currentRPM = 0.0;
  } else if (newPulseDetected) {
    noInterrupts();
    unsigned long intervalCopy = pulseInterval;
    newPulseDetected = false;
    interrupts();
    if (intervalCopy > 0) {
      currentRPM = (60000000.0 / intervalCopy) / MAGNETS_PER_REV;
    }
  }
  return currentRPM;
}

void startTemperatureConversion() {
  if (tempSensorFound && !conversionInProgress) {
    sensors.requestTemperatures();
    conversionInProgress = true;
    conversionStartTime = millis();
  }
}

void updateTemperatureIfReady() {
  if (!tempSensorFound || !conversionInProgress) return;
  if (millis() - conversionStartTime >= conversionDelayMs) {
    float tempC = sensors.getTempCByIndex(0);
    lastValidTempC = (tempC != DEVICE_DISCONNECTED_C) ? tempC : DEVICE_DISCONNECTED_C;
    conversionInProgress = false;
  }
}

// =====================================================
// WIFI HELPERS (non-blocking)
// =====================================================
void connectWiFi() {
  Serial.print("Connecting to WiFi: ");
  Serial.println(WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  unsigned long startAttempt = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startAttempt < 10000) {
    delay(250);   // only blocking spot, limited to setup() and max 10s
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("\nWiFi connected. IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\nWiFi connect failed, will retry in background.");
  }
}

void maintainWiFi(unsigned long now) {
  if (WiFi.status() != WL_CONNECTED && now - wifiRetryTimer >= WIFI_RECONNECT_INTERVAL) {
    wifiRetryTimer = now;
    Serial.println("WiFi disconnected, retrying...");
    WiFi.disconnect();
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  }
}

// =====================================================
// THINGSPEAK UPLOAD (non-blocking, rate-limited)
// =====================================================
void uploadToThingSpeak() {
  if (WiFi.status() != WL_CONNECTED) return;
  if (String(THINGSPEAK_API_KEY) == "PASTE_YOUR_WRITE_API_KEY_HERE") return; // no key set yet

  HTTPClient http;

  String url = String(THINGSPEAK_HOST) + "?api_key=" + THINGSPEAK_API_KEY +
               "&field1=" + String(g_currentRPM, 1) +
               "&field2=" + String(g_vibrationG, 3) +
               "&field3=" + String(g_current_mA, 1) +
               "&field4=" + String(g_acVoltageRMS, 1) +
               "&field5=" + String(g_motorVolts, 2) +
               "&field6=" + String(g_tempC, 1) +
               "&field7=" + String(g_relayIsOn ? 1 : 0) +
               "&field8=" + String(g_faultCode);

  http.begin(url);
  int httpCode = http.GET();

  if (httpCode > 0) {
    Serial.print("ThingSpeak upload OK, entry #");
    Serial.println(http.getString());
  } else {
    Serial.print("ThingSpeak upload failed, error: ");
    Serial.println(http.errorToString(httpCode));
  }
  http.end();
}

// =====================================================
// SETUP
// =====================================================
void setup() {
  Serial.begin(115200);

  pinMode(HALL_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(HALL_PIN), countPulse, FALLING);

  pinMode(RELAY_PIN, OUTPUT);
  relayOff();

  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  ina219Found = ina219.begin();
  if (ina219Found) {
    ina219.setCalibration_32V_2A();
    Serial.println("INA219: OK");
  } else {
    Serial.println("INA219: NOT FOUND");
  }

  sensors.begin();
  int deviceCount = sensors.getDeviceCount();
  Serial.print("DS18B20 devices found: "); Serial.println(deviceCount);

  if (deviceCount > 0 && sensors.getAddress(sensorAddress, 0)) {
    sensors.setResolution(sensorAddress, 10);
    sensors.setWaitForConversion(false);
    conversionDelayMs = 750 / (1 << (12 - sensors.getResolution(sensorAddress)));
    tempSensorFound = true;
    startTemperatureConversion();
    Serial.println("DS18B20: OK");
  } else {
    Serial.println("DS18B20: NOT FOUND / ADDRESS FAIL");
  }

  analogReadResolution(12);

  connectWiFi();

  Serial.println("=== INFRA-NEX THRESHOLD + FAULT LOGIC + CLOUD STARTED ===");
  readTimer = millis();
  state = RUNNING;
  motorStartTime = millis();
}

// =====================================================
// LOOP
// =====================================================
void loop() {
  unsigned long now = millis();
  updateTemperatureIfReady();
  maintainWiFi(now);

  if (now - readTimer >= READ_INTERVAL) {
    readTimer = now;

    // -------- Relay drive based on state --------
    bool relayIsOn;
    if (state == RUNNING) {
      relayOn();
      relayIsOn = true;
    } else {
      relayOff();
      relayIsOn = false;
    }

    // -------- Read all sensors --------
    float currentRPM = readRPM();

    int rawX = analogRead(ADXL_X_PIN);
    int rawY = analogRead(ADXL_Y_PIN);
    int rawZ = analogRead(ADXL_Z_PIN);
    float gX = (float)(rawX - ADXL_X_ZERO) / ADXL_SENSITIVITY;
    float gY = (float)(rawY - ADXL_Y_ZERO) / ADXL_SENSITIVITY;
    float gZ = (float)(rawZ - ADXL_Z_ZERO) / ADXL_SENSITIVITY;
    float vibrationG = sqrt((gX * gX) + (gY * gY) + (gZ * gZ));

    float current_mA = 0, busVoltage = 0, shuntVolts = 0, motorVolts = 0;
    if (ina219Found) {
      current_mA = ina219.getCurrent_mA();
      busVoltage = ina219.getBusVoltage_V();
      shuntVolts = ina219.getShuntVoltage_mV();
      motorVolts = busVoltage + (shuntVolts / 1000.0);
    }

    float acVoltageRMS = readZMPT_RMS();
    if (!conversionInProgress) startTemperatureConversion();

    bool sensorFaultTemp = (lastValidTempC == DEVICE_DISCONNECTED_C);

    // -------- Fault condition checks --------
    bool pastStartupGrace = relayIsOn && (now - motorStartTime >= STARTUP_GRACE_MS);

    bool rpmLowBad   = pastStartupGrace && (currentRPM < RPM_MIN_RUN);
    bool rpmHighBad  = pastStartupGrace && (currentRPM > RPM_MAX_RUN);
    bool currHighBad = pastStartupGrace && (current_mA > CURR_TRIP_mA);
    bool currLowBad  = pastStartupGrace && (current_mA < CURR_LOW_mA);
    bool vibBad      = relayIsOn && (vibrationG > VIB_TRIP_g);
    bool dcLowBad    = relayIsOn && (motorVolts < DC_VOLT_MIN);
    bool dcHighBad   = relayIsOn && (motorVolts > DC_VOLT_MAX);
    bool acLowBad    = (acVoltageRMS < AC_RMS_MIN) && (acVoltageRMS > 0);
    bool acHighBad   = (acVoltageRMS > AC_RMS_MAX);
    bool tempBad     = relayIsOn && !sensorFaultTemp && (lastValidTempC > TEMP_MAX_C);

    // -------- Debounce each channel --------
    updateTimer(rpmLowFault,   rpmLowBad,   now);
    updateTimer(rpmHighFault,  rpmHighBad,  now);
    updateTimer(currHighFault, currHighBad, now);
    updateTimer(currLowFault,  currLowBad,  now);
    updateTimer(vibFault,      vibBad,      now);
    updateTimer(dcLowFault,    dcLowBad,    now);
    updateTimer(dcHighFault,   dcHighBad,   now);
    updateTimer(acLowFault,    acLowBad,    now);
    updateTimer(acHighFault,   acHighBad,   now);
    updateTimer(tempFault,     tempBad,     now);

    bool rpmLowC   = isConfirmed(rpmLowFault, now);
    bool rpmHighC  = isConfirmed(rpmHighFault, now);
    bool currHighC = isConfirmed(currHighFault, now);
    bool currLowC  = isConfirmed(currLowFault, now);
    bool vibC      = isConfirmed(vibFault, now);
    bool dcLowC    = isConfirmed(dcLowFault, now);
    bool dcHighC   = isConfirmed(dcHighFault, now);
    bool acLowC    = isConfirmed(acLowFault, now);
    bool acHighC   = isConfirmed(acHighFault, now);
    bool tempOkC   = isConfirmed(tempFault, now);

    // -------- Multi-sensor correlation -> Fault Code --------
    FaultCode fc = FC0_NORMAL;
    if      (currHighC && rpmLowC)                    fc = FC1_JAM;
    else if (vibC && rpmLowC)                         fc = FC2_BEARING;
    else if (tempOkC && currHighC)                    fc = FC3_OVERHEAT;
    else if (rpmLowC && !currHighC)                   fc = FC4_SLIP;
    else if (dcLowC || dcHighC || acLowC || acHighC)  fc = FC5_SUPPLY;
    else if (vibC && !rpmLowC)                        fc = FC6_LOOSENESS;
    else if (currLowC && rpmHighC)                    fc = FC7_RUNAWAY;

    bool anyFaultConfirmed = (fc != FC0_NORMAL);

    // -------- State machine: trip -> wait 5s -> auto-restart --------
    if (state == RUNNING && anyFaultConfirmed) {
      state = TRIPPED_WAIT;
      stateChangeTime = now;
      relayOff();
      relayIsOn = false;
      Serial.print(">>> FAULT TRIPPED: "); Serial.println(faultName(fc));
    } else if (state == TRIPPED_WAIT && (now - stateChangeTime >= RESTART_DELAY_MS)) {
      Serial.println(">>> Restart delay elapsed -> retrying motor <<<");
      state = RUNNING;
      motorStartTime = now;
    }

    // -------- Cache latest values for cloud upload --------
    g_currentRPM   = currentRPM;
    g_vibrationG   = vibrationG;
    g_current_mA   = current_mA;
    g_acVoltageRMS = acVoltageRMS;
    g_motorVolts   = motorVolts;
    g_tempC        = sensorFaultTemp ? 0 : lastValidTempC;
    g_relayIsOn    = relayIsOn;
    g_faultCode    = (int)fc;

    // -------- Status print --------
    Serial.print("State: "); Serial.print(state == RUNNING ? "RUNNING" : "TRIPPED_WAIT");
    Serial.print(" | Fault: "); Serial.print(faultName(fc));
    Serial.print(" | Relay: "); Serial.print(relayIsOn ? "ON" : "OFF");
    Serial.print(" | RPM: "); Serial.print(currentRPM, 1);
    Serial.print(" | Temp: ");
    if (sensorFaultTemp) Serial.print("ERR");
    else { Serial.print(lastValidTempC, 1); Serial.print("C"); }
    Serial.print(" | DC: "); Serial.print(motorVolts, 2); Serial.print("V");
    Serial.print(" | Current: "); Serial.print(current_mA, 1); Serial.print("mA");
    Serial.print(" | AC RMS: "); Serial.print(acVoltageRMS, 1); Serial.print("V");
    Serial.print(" | Vib: "); Serial.print(vibrationG, 3); Serial.println("g");
    Serial.print(" | WiFi: "); Serial.println(WiFi.status() == WL_CONNECTED ? "OK" : "DOWN");
  }

  // -------- ThingSpeak upload (separate, rate-limited timer) --------
  if (now - thingSpeakTimer >= THINGSPEAK_UPLOAD_INTERVAL) {
    thingSpeakTimer = now;
    uploadToThingSpeak();
  }
}
