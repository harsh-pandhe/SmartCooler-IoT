#include <Arduino.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <DHT.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WebServer.h> // Switched from AsyncWebServer to standard WebServer

// ================= CONFIGURATION =================
const char* ssid = "SmartCooler_WiFi";
const char* password = "password123";

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
WebServer server(80); // Standard WebServer instance
Preferences preferences;

// ================= VARIABLES =================
float setTemp = 20.0;
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

// ================= WEB UI =================
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
	<title>Cooler Dash</title>
	<meta name="viewport" content="width=device-width, initial-scale=1">
	<style>
		body { font-family: sans-serif; background: #0f172a; color: white; text-align: center; padding: 20px; }
		.grid { display: grid; grid-template-columns: 1fr 1fr; gap: 15px; max-width: 400px; margin: 0 auto; }
		.card { background: #1e293b; padding: 20px; border-radius: 15px; border: 1px solid #334155; }
		.label { font-size: 0.8rem; color: #94a3b8; text-transform: uppercase; margin-bottom: 5px; }
		.val { font-size: 1.8rem; font-weight: bold; color: #38bdf8; }
		.status { margin-top: 20px; padding: 15px; border-radius: 15px; background: #1e293b; }
		.btn {
			display: block;
			width: 100%;
			max-width: 400px;
			margin: 20px auto;
			padding: 15px;
			background: #3b82f6;
			border: none;
			border-radius: 10px;
			color: white;
			font-weight: bold;
			cursor: pointer;
		}
		.relay-on { color: #4ade80; }
		.relay-off { color: #f87171; }
	</style>
</head>
<body>
	<h1>Cooler Control</h1>
	<div class="grid">
		<div class="card"><div class="label">Water</div><div class="val" id="wtemp">--</div></div>
		<div class="card"><div class="label">Room</div><div class="val" id="rtemp">--</div></div>
		<div class="card"><div class="label">Humidity</div><div class="val" id="hum">--</div></div>
		<div class="card"><div class="label">Target</div><div class="val" id="set">--</div></div>
	</div>
	<div class="status">
		Mode: <span id="mode">--</span> |
		Relay: <span id="relay">--</span>
	</div>
	<button class="btn" onclick="fetch('/toggle').then(() => location.reload())">TOGGLE AUTO/MANUAL</button>
	<script>
		function updateData() {
			fetch('/data').then(r => r.json()).then(d => {
				document.getElementById('wtemp').innerText = d.w + "C";
				document.getElementById('rtemp').innerText = d.r + "C";
				document.getElementById('hum').innerText = d.h + "%";
				document.getElementById('set').innerText = d.s + "C";
				document.getElementById('mode').innerText = d.m ? "AUTO" : "MANUAL";
				document.getElementById('relay').innerText = d.re ? "ACTIVE" : "IDLE";
				document.getElementById('relay').className = d.re ? "relay-on" : "relay-off";
			}).catch(e => {});
		}
		setInterval(updateData, 2000);
		updateData();
	</script>
</body>
</html>
)rawliteral";

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

// ================= WEB HANDLERS =================
void handleRoot() {
	server.send(200, "text/html", index_html);
}

void handleToggle() {
	autoMode = !autoMode;
	saveSettings();
	server.send(200, "text/plain", "OK");
}

void handleData() {
	String json = "{";
	json += "\"w\":" + String(waterTemp, 1) + ",";
	json += "\"r\":" + String(roomTemp, 1) + ",";
	json += "\"h\":" + String(humidity, 0) + ",";
	json += "\"s\":" + String(setTemp, 1) + ",";
	json += "\"m\":" + String(autoMode) + ",";
	json += "\"re\":" + String(relayState);
	json += "}";
	server.send(200, "application/json", json);
}

// ================= CORE LOGIC =================
void handleButtons() {
	if (!autoMode) {
		if (digitalRead(BTN_UP) == LOW) {
			setTemp += 0.5;
			beep(40);
			delay(150);
		}
		if (digitalRead(BTN_DOWN) == LOW) {
			setTemp -= 0.5;
			beep(40);
			delay(150);
		}
	}

	if (digitalRead(BTN_MODE) == LOW) {
		if (!modeHeld) {
			pressStart = millis();
			modeHeld = true;
		}

		unsigned long held = millis() - pressStart;
		if (held > 5000 && held < 5200) beep(100);

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
	if (waterTemp < -50 || waterTemp > 90) {
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

	relayState = digitalRead(RELAY_PIN);
	digitalWrite(LED_RELAY, relayState);
}

// ================= SETUP =================
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
	lcd.clear();
	lcd.print("Cooler Booting...");

	waterSensor.begin();
	dht.begin();

	WiFi.softAP(ssid, password);

	// Set up Web Server Routes
	server.on("/", handleRoot);
	server.on("/toggle", handleToggle);
	server.on("/data", handleData);
	server.begin();

	beep(200);
}

void loop() {
	server.handleClient(); // Essential for standard WebServer

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

	float target = setTemp;
	if (autoMode && !isnan(roomTemp) && !isnan(humidity)) {
		if (humidity > 70 || roomTemp > 32) target = setTemp - 3.0;
		else if (humidity < 40 && roomTemp < 20) target = setTemp + 2.0;
	}

	controlRelay(target);

	digitalWrite(LED_MANUAL, !autoMode);
	digitalWrite(LED_AUTO, autoMode);

	static unsigned long lastLCDUpdate = 0;
	if (millis() - lastLCDUpdate > 500) {
		lcd.setCursor(0,0);
		lcd.print(autoMode ? "AUTO " : "MAN  ");
		lcd.print("W:"); lcd.print(waterTemp, 1); lcd.print("  ");
		lcd.setCursor(0,1);
		lcd.print("S:"); lcd.print(setTemp, 1);
		lcd.print(" H:"); lcd.print(isnan(humidity) ? 0 : humidity, 0); lcd.print("% ");
		lastLCDUpdate = millis();
	}
}
