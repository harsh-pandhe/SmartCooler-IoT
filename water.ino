#include <Arduino.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <DHT.h>
#include <Preferences.h> 
#include <WiFi.h>
#include <WiFiClientSecure.h>       // For HTTPS connection
#include <HTTPClient.h>             // For sending data to Express Server
#include <PubSubClient.h>           // For receiving commands via MQTT

#include "secrets.h"

// ================= CREDENTIALS =================
// Replace with your local network details
// WIFI_SSID and WIFI_PASSWORD are defined in secrets.h


// Server Configuration (Node.js Express Server)
// IMPORTANT: Replace XX with your computer's actual local IP address
// Server Configuration (Node.js Express Server)
const char* server_url = "https://smart-cooler-io-t.vercel.app/api/update"; 

// MQTT Configuration
const char* mqtt_server = "broker.hivemq.com"; 
const char* mqtt_topic_cmd = "hydrochill/command";

// ================= HARDWARE PINS =================
#define ONE_WIRE_BUS 16
#define DHTPIN 15
#define DHTTYPE DHT22
#define BTN_UP 32
#define BTN_DOWN 33
#define BTN_MODE 25
#define RELAY_PIN 13
#define LED_MANUAL 26
#define LED_AUTO 27
#define LED_RELAY 14
#define BUZZER 12

// ================= GLOBAL OBJECTS =================
LiquidCrystal_I2C lcd(0x27, 16, 2);
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature waterSensor(&oneWire);
DHT dht(DHTPIN, DHTTYPE);
Preferences preferences; 

WiFiClient espClient;
PubSubClient mqttClient(espClient);

// ================= VARIABLES =================
float setTemp = 20.0;     // Manual Setpoint
float autoTarget = 20.0;  // Calculated Auto Target
float waterTemp = 0;
float roomTemp = 0;
float humidity = 0;
bool autoMode = false;
bool relayState = false;

unsigned long lastRelayChange = 0;
const unsigned long COMP_DELAY = 30000; 
const float HYSTERESIS = 0.5;

unsigned long pressStart = 0;
bool modeHeld = false;

unsigned long lastCloudSync = 0;
const unsigned long SYNC_INTERVAL = 5000; 

// ================= HELPERS =================
void beep(int ms = 80) {
  digitalWrite(BUZZER, HIGH);
  delay(ms);
  digitalWrite(BUZZER, LOW);
}

void saveSettings() {
  preferences.begin("cooler", false);
  preferences.putFloat("setTemp", setTemp);
  preferences.putBool("autoMode", autoMode);
  preferences.end();
}

void loadSettings() {
  preferences.begin("cooler", true);
  setTemp = preferences.getFloat("setTemp", 20.0);
  autoMode = preferences.getBool("autoMode", false);
  preferences.end();
  if (isnan(setTemp) || setTemp < 5 || setTemp > 45) setTemp = 20.0;
}

// ================= NETWORKING =================
void setupWiFi() {
  lcd.clear();
  lcd.print("WiFi Connect...");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  int retry = 0;
  while (WiFi.status() != WL_CONNECTED && retry < 20) {
    delay(500);
    lcd.setCursor(retry % 16, 1);
    lcd.print(".");
    retry++;
  }
  if(WiFi.status() == WL_CONNECTED) {
    lcd.clear();
    lcd.print("WiFi Connected!");
    delay(1000);
  }
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String message;
  for (int i = 0; i < length; i++) message += (char)payload[i];
  
  if (message.startsWith("SET:")) {
    setTemp = message.substring(4).toFloat();
    saveSettings();
    beep(100);
  } else if (message == "MODE:TOGGLE") {
    autoMode = !autoMode;
    saveSettings();
    beep(200);
  }
}

void syncWithServer() {
  if (WiFi.status() != WL_CONNECTED) return;

  WiFiClientSecure client;
  client.setInsecure(); // Skip certificate validation for simplicity

  HTTPClient http;
  http.begin(client, server_url);
  http.addHeader("Content-Type", "application/json");

  float safeWater = isnan(waterTemp) ? 0.0 : waterTemp;
  float safeRoom = isnan(roomTemp) ? 0.0 : roomTemp;
  float safeHum = isnan(humidity) ? 0.0 : humidity;

  String json = "{";
  json += "\"waterTemp\":" + String(safeWater, 1) + ",";
  json += "\"roomTemp\":" + String(safeRoom, 1) + ",";
  json += "\"humidity\":" + String(safeHum, 0) + ",";
  json += "\"target\":" + String(autoMode ? autoTarget : setTemp, 1) + ",";
  json += "\"manualSet\":" + String(setTemp, 1) + ",";
  json += "\"autoMode\":" + String(autoMode ? "true" : "false") + ",";
  json += "\"relay\":" + String(relayState ? "true" : "false");
  json += "}";

  http.POST(json);
  http.end();
}

// ================= CORE LOGIC =================
void handleButtons() {
  if (!autoMode) {
    if (digitalRead(BTN_UP) == LOW) { setTemp += 0.5; beep(40); delay(150); }
    if (digitalRead(BTN_DOWN) == LOW) { setTemp -= 0.5; beep(40); delay(150); }
  }

  if (digitalRead(BTN_MODE) == LOW) {
    if (!modeHeld) { pressStart = millis(); modeHeld = true; }
    unsigned long held = millis() - pressStart;
    if (held > 10000) {
      autoMode = !autoMode;
      saveSettings();
      beep(400);
      modeHeld = false; 
      delay(1000);
    }
  } else {
    modeHeld = false;
  }
}

void controlRelay(float target) {
  unsigned long now = millis();
  if (isnan(waterTemp) || waterTemp < -50 || waterTemp > 90) {
    digitalWrite(RELAY_PIN, LOW);
    return;
  }
  if (waterTemp > (target + HYSTERESIS)) {
    if (digitalRead(RELAY_PIN) == LOW && (now - lastRelayChange > COMP_DELAY)) {
      digitalWrite(RELAY_PIN, HIGH);
      lastRelayChange = now;
    }
  } else if (waterTemp < (target - HYSTERESIS)) {
    if (digitalRead(RELAY_PIN) == HIGH) {
      digitalWrite(RELAY_PIN, LOW);
      lastRelayChange = now;
    }
  }
  relayState = (digitalRead(RELAY_PIN) == HIGH);
  digitalWrite(LED_RELAY, relayState);
}

void setup() {
  Serial.begin(115200);
  loadSettings();

  pinMode(BTN_UP, INPUT_PULLUP);
  pinMode(BTN_DOWN, INPUT_PULLUP);
  pinMode(BTN_MODE, INPUT_PULLUP);
  pinMode(RELAY_PIN, OUTPUT);
  pinMode(LED_MANUAL, OUTPUT);
  pinMode(LED_AUTO, OUTPUT);
  pinMode(LED_RELAY, OUTPUT);
  pinMode(BUZZER, OUTPUT);

  digitalWrite(RELAY_PIN, LOW);
  Wire.begin(21, 22);
  lcd.init();
  lcd.backlight();
  setupWiFi();
  waterSensor.begin();
  dht.begin();

  mqttClient.setServer(mqtt_server, 1883);
  mqttClient.setCallback(mqttCallback);
  beep(200);
}

void loop() {
  if (!mqttClient.connected()) {
    if (mqttClient.connect("ESP32_Cooler_Client")) {
      mqttClient.subscribe(mqtt_topic_cmd);
    }
  }
  mqttClient.loop();

  static unsigned long lastSensorRead = 0;
  if (millis() - lastSensorRead > 2000) {
    waterSensor.requestTemperatures();
    float tempRead = waterSensor.getTempCByIndex(0);
    if (tempRead > -100) waterTemp = tempRead;
    roomTemp = dht.readTemperature();
    humidity = dht.readHumidity();
    lastSensorRead = millis();
  }

  handleButtons();

  // --- REFINED AUTO MODE LOGIC ---
  if (!isnan(roomTemp) && !isnan(humidity)) {
    if (roomTemp > 28.0 || humidity > 75.0) {
      autoTarget = 12.0; // Warm/Rainy -> Cold Water
    } else if (roomTemp < 18.0) {
      autoTarget = 24.0; // Cold -> Room Temp Water
    } else {
      autoTarget = 20.0; // Normal
    }
  }

  float activeTarget = autoMode ? autoTarget : setTemp;
  controlRelay(activeTarget);

  digitalWrite(LED_MANUAL, !autoMode);
  digitalWrite(LED_AUTO, autoMode);

  static unsigned long lastLCDUpdate = 0;
  if (millis() - lastLCDUpdate > 500) {
    lcd.setCursor(0,0);
    lcd.print(autoMode ? "AUTO " : "MAN  ");
    lcd.print("T:"); lcd.print(activeTarget, 1); lcd.print(" ");
    lcd.setCursor(0,1);
    lcd.print("W:"); lcd.print(isnan(waterTemp) ? 0.0 : waterTemp, 1);
    lcd.print(" H:"); lcd.print(isnan(humidity) ? 0 : humidity, 0); lcd.print("%");
    lastLCDUpdate = millis();
  }

  if (millis() - lastCloudSync > SYNC_INTERVAL) {
    syncWithServer();
    lastCloudSync = millis();
  }
}