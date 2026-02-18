# SmartCooler-IoT

SmartCooler-IoT is an ESP32-based cooler controller with:
- water temperature control using a relay,
- room temperature + humidity monitoring,
- local LCD + button controls,
- a built-in Wi-Fi access point and web dashboard.

The project is implemented in [water.ino](water.ino).

## Features

- **Manual mode**: adjust target temperature using hardware buttons.
- **Auto mode**: dynamically shifts target temperature based on room temperature/humidity.
- **Relay protection**: compressor delay (`30s`) and hysteresis (`±0.5°C`) to reduce rapid switching.
- **Persistent settings**: target temperature and mode are saved in ESP32 NVS (`Preferences`).
- **Cloud Dashboard**: Syncs data to Firebase/Vercel for remote monitoring.
- **LCD status view**: continuous local status feedback.

## Hardware

### Main Components

- ESP32 dev board
- DS18B20 (water temperature, OneWire)
- DHT22 (room temperature + humidity)
- 16x2 I2C LCD (address `0x27`)
- Relay module (for compressor/cooling control)
- 3 push buttons (`UP`, `DOWN`, `MODE`)
- 3 indicator LEDs (`MANUAL`, `AUTO`, `RELAY`)
- Buzzer

### Pin Mapping

| Signal | GPIO |
|---|---|
| DS18B20 (OneWire) | `16` |
| DHT22 Data | `15` |
| BTN_UP | `32` |
| BTN_DOWN | `33` |
| BTN_MODE | `25` |
| Relay Control | `13` |
| LED_MANUAL | `26` |
| LED_AUTO | `27` |
| LED_RELAY | `14` |
| Buzzer | `12` |
| I2C SDA | `21` |
| I2C SCL | `22` |

## Software Dependencies

Install these Arduino libraries before uploading:

- `LiquidCrystal_I2C`
- `OneWire`
- `DallasTemperature`
- `DHT sensor library` (and `Adafruit Unified Sensor`)

Built-in ESP32 core libraries used:

- `WiFi.h`
- `WebServer.h`
- `Preferences.h`

## Build & Upload

1. Open [water.ino](water.ino) in Arduino IDE / PlatformIO.
2. Select an ESP32 board.
3. Install required libraries.
4. Build and upload.
5. Open Serial Monitor at `115200` baud (optional).

## Network & Web UI

### Connectivity
The ESP32 connects to the configured Wi-Fi network and actively syncs telemetry to the cloud backend (hosted on **Vercel**).

### Cloud Dashboard
- **URL**: `https://smart-cooler-io-t.vercel.app/`
- View live water temperature, room stats, and cooler status from anywhere.
- **API Endpoint**: `POST /api/update` (used by ESP32 to push data).

### Wi-Fi Configuration
Update `WIFI_SSID` and `WIFI_PASSWORD` in `water.ino` to match your local network.


## Control Logic Summary

- Default setpoint: `20.0°C`.
- Manual mode: `UP/DOWN` change setpoint by `0.5°C`.
- Long press (`>10s`) on `MODE` toggles mode and saves settings.
- Auto mode adjusts effective target:
	- if humidity `>70%` or room temp `>32°C`: target = `setTemp - 3.0`
	- if humidity `<40%` and room temp `<20°C`: target = `setTemp + 2.0`
- Relay control:
	- turn ON if `waterTemp > target + 0.5`
	- turn OFF if `waterTemp < target - 0.5`
	- enforce minimum OFF→ON delay of `30s`

## Notes

- Current Wi-Fi credentials are hardcoded for local development.
- Change `ssid` / `password` in [water.ino](water.ino) before deploying outside lab/demo setups.

## Troubleshooting

- **No LCD output**: verify I2C wiring and LCD address (`0x27`).
- **Sensor values are NaN or 0**: check DHT22/DS18B20 wiring and power.
- **Relay not toggling**: confirm module input polarity and GPIO `13` logic.
- **Can’t open dashboard**: reconnect to `SmartCooler_WiFi`, then browse to `192.168.4.1`.
