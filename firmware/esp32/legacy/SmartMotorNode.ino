#include <WiFi.h>
#include <PubSubClient.h>
#include <OneWire.h>
#include <DallasTemperature.h>

// WiFi credentials
const char* ssid = "Jayshree krishna";
const char* password = "Shreenathg@131";
// MQTT broker info
const char* mqtt_server = "broker.emqx.io";

WiFiClient espClient;
PubSubClient client(espClient);

#define RELAY_PIN  15
#define VIB_PIN    13
#define CURR_PIN   32
#define RPM_PIN    33
#define ONE_WIRE_BUS 4

OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

// Fault thresholds
float maxTemp = 60.0; // Celsius
float maxCurrent = 3.0; // Amps
int maxVibration = 1; // Vibration sensor digital output

void setup_wifi() {
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) delay(500);
}

void reconnect() {
  while (!client.connected()) {
    client.connect("MotorNodeDemo");
    delay(200);
  }
}

void setup() {
  pinMode(RELAY_PIN, OUTPUT);
  pinMode(VIB_PIN, INPUT);
  pinMode(RPM_PIN, INPUT);
  Serial.begin(115200);
  setup_wifi();
  client.setServer(mqtt_server, 1883);
  sensors.begin();
  digitalWrite(RELAY_PIN, HIGH); // Motor ON
}

void loop() {
  if (!client.connected()) reconnect();
  client.loop();
  
  sensors.requestTemperatures();
  float temp = sensors.getTempCByIndex(0);
  float current = analogRead(CURR_PIN) * (5.0 / 4095.0) / 0.185; // ACS712 5A
  int vibration = digitalRead(VIB_PIN);
  int rpm = pulseIn(RPM_PIN, HIGH); // Simplified RPM count

  String payload = "{";
  payload += "\"temp\":" + String(temp) + ",";
  payload += "\"current\":" + String(current) + ",";
  payload += "\"vibration\":" + String(vibration) + ",";
  payload += "\"rpm\":" + String(rpm) + "}";

  client.publish("motor/demo", (char*)payload.c_str());
  
  // Safety logic
  if (temp > maxTemp || current > maxCurrent || vibration == maxVibration) {
    digitalWrite(RELAY_PIN, LOW); // Motor OFF
    client.publish("motor/demo/alert", "Fault detected: Motor stopped!");
  } else {
    digitalWrite(RELAY_PIN, HIGH); // Normal ON
  }
  delay(1000);
}