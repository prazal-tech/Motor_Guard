// INFRA-NEX: ESP32 Motor Monitoring with ThingSpeak Cloud and Web Dashboard
// Features: 6 sensors, fault detection, ThingSpeak API upload, relay+LED control, Web Server with Dashboard/Graphs/Table

#include <WiFi.h>
#include "ThingSpeak.h"
#include <Wire.h>
#include <Adafruit_INA219.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <math.h>
#include <ESPAsyncWebServer.h>  // New: For web server
#include <AsyncTCP.h>           // Dependency

// ======================================
// THINGSPEAK CONFIG - UPDATE THESE!
// ======================================
#define THINGSPEAK_CHANNEL_ID     1234567UL
#define THINGSPEAK_WRITE_APIKEY   "MC8F2GPRVGC3KZIC"

// WiFi credentials
const char* WIFI_SSID     = "vivo Y200 5G";
const char* WIFI_PASSWORD = "priyanknh";

// Sensor pins (unchanged)
#define PIN_DS18B20     4
#define PIN_ADXL_X      34
#define PIN_ADXL_Y      35
#define PIN_ADXL_Z      32
#define PIN_TCRT_RPM    27
#define PIN_ZMPT_VOLT   33
#define PIN_RELAY       25
#define PIN_LED_GREEN   2
#define PIN_LED_YELLOW  14
#define PIN_LED_RED     26

// Thresholds (unchanged)
const float TEMP_WARN = 70.0, TEMP_CRIT = 90.0;
const float VIB_WARN = 0.35, VIB_CRIT = 0.50;
const float VOLT_LOW = 190.0, VOLT_HIGH = 250.0;
const float RPM_LOW = 1300, RPM_HIGH = 1700;
const float CURR_HIGH = 6.0;

// Global variables (unchanged)
volatile uint32_t rpmPulses = 0;
float currentRPM = 0;
uint32_t lastRPMTime = 0;
unsigned long ledTimer = 0;
bool ledState = false;
uint8_t faultLevel = 0;

OneWire oneWire(PIN_DS18B20);
DallasTemperature ds18b20(&oneWire);
Adafruit_INA219 ina219;
WiFiClient client;

// ---------- NEW: Web Server and Data Storage ----------
AsyncWebServer server(80);

// Struct for historical data (circular buffer)
#define MAX_HISTORY 50
struct SensorData {
  float current_A, voltage_V, temp_body_C, temp_bearing_C, vib_g, rpm;
  unsigned long timestamp;
};
SensorData history[MAX_HISTORY];
int historyIndex = 0;
int historyCount = 0;

// Performance variables
float avgCurrent = 0, avgVoltage = 0, avgTempBody = 0, avgTempBearing = 0, avgVib = 0, avgRPM = 0;
float minTempBody = 999, maxTempBody = -999, minRPM = 999, maxRPM = -999;
unsigned long uptimeMillis = 0;
String currentFault = "NORMAL";
bool relayState = false;

// HTML for dashboard (embedded)
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>INFRA-NEX Motor Dashboard</title>
  <link href="https://cdn.jsdelivr.net/npm/bootstrap@5.3.0/dist/css/bootstrap.min.css" rel="stylesheet">
  <script src="https://cdn.jsdelivr.net/npm/chart.js"></script>
</head>
<body class="bg-light">
  <div class="container mt-4">
    <h1 class="text-center">INFRA-NEX Motor Monitoring Dashboard</h1>
    <p class="text-center">ThingSpeak Channel: <a href="https://thingspeak.com/channels/1234567" target="_blank">View on ThingSpeak</a></p>
    <div class="row">
      <div class="col-md-4">
        <div class="card">
          <div class="card-header">Controls</div>
          <div class="card-body">
            <button id="toggleRelay" class="btn btn-primary">Toggle Relay</button>
            <p id="relayStatus">Relay: OFF</p>
            <p id="ledStatus">LED: Normal</p>
          </div>
        </div>
      </div>
      <div class="col-md-8">
        <div class="card">
          <div class="card-header">Performance Overview</div>
          <div class="card-body">
            <table class="table table-striped">
              <thead><tr><th>Metric</th><th>Value</th></tr></thead>
              <tbody>
                <tr><td>Uptime</td><td id="uptime">0s</td></tr>
                <tr><td>Fault Status</td><td id="fault">NORMAL</td></tr>
                <tr><td>Avg Current (A)</td><td id="avgCurrent">0</td></tr>
                <tr><td>Avg Voltage (V)</td><td id="avgVoltage">0</td></tr>
                <tr><td>Avg Temp Body (°C)</td><td id="avgTempBody">0</td></tr>
                <tr><td>Avg Temp Bearing (°C)</td><td id="avgTempBearing">0</td></tr>
                <tr><td>Avg Vibration (g)</td><td id="avgVib">0</td></tr>
                <tr><td>Avg RPM</td><td id="avgRPM">0</td></tr>
                <tr><td>Min/Max Temp Body (°C)</td><td id="minMaxTemp">0/0</td></tr>
                <tr><td>Min/Max RPM</td><td id="minMaxRPM">0/0</td></tr>
              </tbody>
            </table>
          </div>
        </div>
      </div>
    </div>
    <div class="row mt-4">
      <div class="col-md-6"><canvas id="currentChart"></canvas></div>
      <div class="col-md-6"><canvas id="voltageChart"></canvas></div>
      <div class="col-md-6"><canvas id="tempChart"></canvas></div>
      <div class="col-md-6"><canvas id="vibChart"></canvas></div>
      <div class="col-md-6"><canvas id="rpmChart"></canvas></div>
    </div>
  </div>
  <script>
    const charts = {
      current: new Chart(document.getElementById('currentChart'), { type: 'line', data: { labels: [], datasets: [{ label: 'Current (A)', data: [], borderColor: 'blue' }] }, options: { responsive: true } }),
      voltage: new Chart(document.getElementById('voltageChart'), { type: 'line', data: { labels: [], datasets: [{ label: 'Voltage (V)', data: [], borderColor: 'green' }] }, options: { responsive: true } }),
      temp: new Chart(document.getElementById('tempChart'), { type: 'line', data: { labels: [], datasets: [{ label: 'Temp Body (°C)', data: [], borderColor: 'red' }, { label: 'Temp Bearing (°C)', data: [], borderColor: 'orange' }] }, options: { responsive: true } }),
      vib: new Chart(document.getElementById('vibChart'), { type: 'line', data: { labels: [], datasets: [{ label: 'Vibration (g)', data: [], borderColor: 'purple' }] }, options: { responsive: true } }),
      rpm: new Chart(document.getElementById('rpmChart'), { type: 'line', data: { labels: [], datasets: [{ label: 'RPM', data: [], borderColor: 'black' }] }, options: { responsive: true } })
    };
    setInterval(() => {
      fetch('/data').then(res => res.json()).then(data => {
        document.getElementById('uptime').textContent = data.uptime;
        document.getElementById('fault').textContent = data.fault;
        document.getElementById('avgCurrent').textContent = data.avgCurrent.toFixed(2);
        document.getElementById('avgVoltage').textContent = data.avgVoltage.toFixed(2);
        document.getElementById('avgTempBody').textContent = data.avgTempBody.toFixed(2);
        document.getElementById('avgTempBearing').textContent = data.avgTempBearing.toFixed(2);
        document.getElementById('avgVib').textContent = data.avgVib.toFixed(2);
        document.getElementById('avgRPM').textContent = data.avgRPM.toFixed(2);
        document.getElementById('minMaxTemp').textContent = `${data.minTempBody.toFixed(1)}/${data.maxTempBody.toFixed(1)}`;
        document.getElementById('minMaxRPM').textContent = `${data.minRPM.toFixed(1)}/${data.maxRPM.toFixed(1)}`;
        document.getElementById('relayStatus').textContent = `Relay: ${data.relayState ? 'ON' : 'OFF'}`;
        document.getElementById('ledStatus').textContent = `LED: ${['Normal', 'Warning', 'Critical'][data.ledStatus]}`;
        const time = new Date().toLocaleTimeString();
        charts.current.data.labels.push(time); charts.current.data.datasets[0].data.push(data.current_A); charts.current.update();
        charts.voltage.data.labels.push(time); charts.voltage.data.datasets[0].data.push(data.voltage_V); charts.voltage.update();
        charts.temp.data.labels.push(time); charts.temp.data.datasets[0].data.push(data.temp_body_C); charts.temp.data.datasets[1].data.push(data.temp_bearing_C); charts.temp.update();
        charts.vib.data.labels.push(time); charts.vib.data.datasets[0].data.push(data.vib_g); charts.vib.update();
        charts.rpm.data.labels.push(time); charts.rpm.data.datasets[0].data.push(data.rpm); charts.rpm.update();
        Object.values(charts).forEach(chart => {
          if (chart.data.labels.length > 50) {
            chart.data.labels.shift();
            chart.data.datasets.forEach(ds => ds.data.shift());
          }
        });
      });
    }, 5000);
    document.getElementById('toggleRelay').addEventListener('click', () => {
      fetch('/toggle', { method: 'POST' }).then(() => alert('Relay toggled!'));
    });
  </script>
</body>
</html>
)rawliteral";

// Utility functions (unchanged)
void IRAM_ATTR rpmISR() { rpmPulses++; }
float adxlToG(int adcRaw) { /* unchanged */ }
void connectWiFi() { /* unchanged */ }
void updateLEDs() { /* unchanged */ }

// ---------- NEW: Web Server Handlers ----------
void setupWebServer() {
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send_P(200, "text/html", index_html);
  });
  server.on("/data", HTTP_GET, [](AsyncWebServerRequest *request) {
    String json = "{";
    json += "\"current_A\":" + String(history[(historyIndex - 1 + MAX_HISTORY) % MAX_HISTORY].current_A, 3) + ",";
    json += "\"voltage_V\":" + String(history[(historyIndex - 1 + MAX_HISTORY) % MAX_HISTORY].voltage_V, 1) + ",";
    json += "\"temp_body_C\":" + String(history[(historyIndex - 1 + MAX_HISTORY) % MAX_HISTORY].temp_body_C, 1) + ",";
    json += "\"temp_bearing_C\":" + String(history[(historyIndex - 1 + MAX_HISTORY) % MAX_HISTORY].temp_bearing_C, 1) + ",";
    json += "\"vib_g\":" + String(history[(historyIndex - 1 + MAX_HISTORY) % MAX_HISTORY].vib_g, 3) + ",";
    json += "\"rpm\":" + String(history[(historyIndex - 1 + MAX_HISTORY) % MAX_HISTORY].rpm, 1) + ",";
    json += "\"fault\":\"" + currentFault + "\",";
    json += "\"uptime\":\"" + String(uptimeMillis / 1000) + "s\",";
    json += "\"avgCurrent\":" + String(avgCurrent, 2) + ",";
    json += "\"avgVoltage\":" + String(avgVoltage, 2) + ",";
    json += "\"avgTempBody\":" + String(avgTempBody, 2) + ",";
    json += "\"avgTempBearing\":" + String(avgTempBearing, 2) + ",";
    json += "\"avgVib\":" + String(avgVib, 2) + ",";
    json += "\"avgRPM\":" + String(avgRPM, 2) + ",";
    json += "\"minTempBody\":" + String(minTempBody, 1) + ",";
    json += "\"maxTempBody\":" + String(maxTempBody, 1) + ",";
    json += "\"minRPM\":" + String(minRPM, 1) + ",";
    json += "\"maxRPM\":" + String(maxRPM, 1) + ",";
    json += "\"relayState\":" + String(relayState) + ",";
    json += "\"ledStatus\":" + String(faultLevel);
    json += "}";
    request->send(200, "application/json", json);
  });
  server.on("/toggle", HTTP_POST, [](AsyncWebServerRequest *request) {
    relayState = !relayState;
    digitalWrite(PIN_RELAY, relayState ? LOW : HIGH);
    request->send(200, "text/plain", "Toggled");
  });
  server.begin();
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  pinMode(PIN_RELAY, OUTPUT);
  digitalWrite(PIN_RELAY, LOW);
  pinMode(PIN_TCRT_RPM, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(PIN_TCRT_RPM), rpmISR, FALLING);
  pinMode(PIN_LED_GREEN, OUTPUT);
  pinMode(PIN_LED_YELLOW, OUTPUT);
  pinMode(PIN_LED_RED, OUTPUT);
  Wire.begin();
  if (!ina219.begin()) Serial.println("INA219 ERROR!");
  ds18b20.begin();
  connectWiFi();
  ThingSpeak.begin(client);
  setupWebServer();  // NEW
  Serial.println("=== INFRA-NEX READY ===");
  Serial.println("ThingSpeak Channel ID: " + String(THINGSPEAK_CHANNEL_ID));
  Serial.println("Dashboard: http://" + WiFi.localIP().toString());
  Serial.println("ThingSpeak: https://thingspeak.com/channels/" + String(THINGSPEAK_CHANNEL_ID));
}

void loop() {
  static unsigned long lastUpdate = 0;
  if (millis() - lastUpdate < 20000) {
    updateLEDs();
    return;
  }
  lastUpdate = millis();
  uptimeMillis = millis();

  // Sensor readings (unchanged)
  float current_A = ina219.getCurrent_mA() / 1000.0;
  float busV = ina219.getBusVoltage_V();
  float voltage_V = busV + (ina219.getShuntVoltage_mV() / 1000);
  ds18b20.requestTemperatures();
  float tempBody = ds18b20.getTempCByIndex(0);
  float tempBear = ds18b20.getTempCByIndex(1);
  if (tempBody == DEVICE_DISCONNECTED_C) tempBody = -999;
  if (tempBear == DEVICE_DISCONNECTED_C) tempBear = -999;
  float gx = adxlToG(analogRead(PIN_ADXL_X));
  float gy = adxlToG(analogRead(PIN_ADXL_Y));
  float gz = adxlToG(analogRead(PIN_ADXL_Z));
  float vibMag = sqrt(gx*gx + gy*gy + gz*gz);
  float zmptRaw = analogRead(PIN_ZMPT_VOLT);
  float lineVoltage = (zmptRaw / 4095.0 * 3.3) * 85.0;
  unsigned long now = millis();
  if (now - lastRPMTime > 1000) {
    noInterrupts();
    uint32_t pulses = rpmPulses;
    rpmPulses = 0;
    interrupts();
    currentRPM = (pulses * 60.0) / ((now - lastRPMTime) / 1000.0);
    lastRPMTime = now;
  }

  // Fault detection (unchanged)
  faultLevel = 0;
  String faultMsg = "NORMAL";
  if (current_A > CURR_HIGH || tempBody > TEMP_CRIT || tempBear > TEMP_CRIT || vibMag > 
