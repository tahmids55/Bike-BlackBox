# ESP32 Bike Computer

ESP32-based bike computer with GPS tracking, Hall sensor speed measurement, MPU6050 tilt sensing, OLED display output, and a Flask web dashboard.

## Overview
This project combines embedded firmware, a local Python server, and a browser interface.

- The ESP32 reads GPS, Hall sensor, and MPU6050 data.
- The OLED shows live ride information on the device.
- The Flask server receives telemetry over Wi-Fi.
- The browser dashboard shows live gauges, maps, stats, logs, and display controls.
- Accident detection is based on tilt threshold and persistence timing.
- Accident alerts are sent to Telegram.

## Features
- GPS speed, coordinates, and satellite count
- Hall sensor RPM and wheel speed
- MPU6050 tilt angle and total acceleration
- SH1106 OLED display with multiple templates
- Live web dashboard with Socket.IO updates
- Full map and mini map views with route trail
- Trip statistics and 7-day JSON history
- Trip reset from the web interface
- Accident alert modal and Telegram notification
- Web-based OLED brightness and template control

## Main Files
- `esp32/bike_computer.ino` - main ESP32 firmware
- `server.py` - Flask backend and Socket.IO server
- `templates/index.html` - main web dashboard
- `static/js/app.js` - browser logic and live UI updates
- `static/js/display-control.js` - OLED control panel and preview
- `static/js/map.js` - Leaflet map handling
- `static/js/gauges.js` - animated speed gauges
- `history_7days.json` - saved history records

## Hardware Connections
| ESP32 Pin / Bus | Connected Device | Purpose |
|---|---|---|
| GPIO 4 | Hall sensor output | Pulse input for RPM and speed |
| GPIO 16 | GPS TX | ESP32 RX2 |
| GPIO 17 | GPS RX | ESP32 TX2 |
| GPIO 21 | MPU6050 SDA and OLED SDA | I2C data |
| GPIO 22 | MPU6050 SCL and OLED SCL | I2C clock |
| I2C 0x68 | MPU6050 | Tilt and acceleration sensing |
| I2C 0x3C | SH1106 OLED | Display controller |

## Software Stack
- Arduino IDE for ESP32 firmware
- Python 3 for the backend server
- Flask for routing and templates
- Flask-SocketIO for live updates
- HTML, CSS, and JavaScript for the browser UI
- Leaflet for mapping
- TinyGPS++ for GPS parsing
- ArduinoJson for JSON parsing on the ESP32

## How It Works
1. The ESP32 starts and initializes OLED, Wi-Fi, GPS, Hall sensor interrupt, and MPU6050.
2. GPS and sensor values are read in the main loop.
3. The ESP32 sends telemetry to the Flask server using HTTP JSON.
4. The server updates trip statistics, stores periodic history, and broadcasts live data.
5. The browser receives updates through Socket.IO and refreshes the dashboard.
6. If the tilt condition remains above the accident threshold, an accident alert is sent.
7. The server sends Telegram alerts and shows the accident event in the browser.

## API Routes
- `POST /update` - receive telemetry from the ESP32
- `POST /accident` - receive accident coordinates
- `GET /display` - send OLED settings to the ESP32
- `POST /display` - update OLED template, brightness, and custom message
- `GET /latest` - fallback telemetry endpoint
- `GET /api/history` - return saved history
- `POST /trip/reset` - reset trip statistics

## Project Structure
```text
server/
├── esp32/
│   └── bike_computer.ino
├── static/
│   ├── css/
│   ├── icons/
│   └── js/
├── templates/
│   ├── index.html
│   ├── map.html
│   └── sections/
├── server.py
├── requirements.txt
├── manifest.json
└── history_7days.json
```

## Installation
1. Install Python dependencies:
   ```bash
   pip install -r requirements.txt
   ```
2. Open `esp32/bike_computer.ino` in Arduino IDE.
3. Install the required Arduino libraries.
4. Update the Wi-Fi SSID, password, and server IP in the firmware.
5. Upload the firmware to the ESP32.
6. Start the Flask server:
   ```bash
   python server.py
   ```
7. Open the browser dashboard from the server address shown in the terminal.

## Libraries Used
### ESP32
- TinyGPS++
- WiFi.h
- HTTPClient.h
- Wire.h
- Adafruit_SH110X
- ArduinoJson

### Python
- Flask
- Flask-SocketIO
- requests

## Notes
- The project uses a local Flask server and does not use a database.
- History is stored in JSON format.
- The standalone `templates/map.html` file exists as a separate GPS tracker page.
- Some network credentials and server settings are hardcoded in the firmware and should be changed before deployment.
