"""
Bike Computer – Flask Server with WebSocket + Telegram Alerts
"""
from flask import Flask, request, jsonify, render_template, send_from_directory
from flask_socketio import SocketIO
from datetime import datetime
import requests as http_requests
import math
import time
import threading
import json
import os

app = Flask(__name__)
app.config['SECRET_KEY'] = 'bike-computer-2026'
socketio = SocketIO(app, cors_allowed_origins="*", async_mode='threading')

# ==================== Configuration ====================
TELEGRAM_BOT_TOKEN = "8995507243:AAFsRcJeDsz8Xqnf-M6CjSvpr8WLR-aoKp8"
TELEGRAM_CHAT_IDS = ["6390356621", "5854737853"]

# ==================== Data Stores ====================
telemetry = {
    "lat": 0.0, "lng": 0.0,
    "gps_speed": 0.0, "satellites": 0,
    "hall_speed": 0.0, "rpm": 0.0,
    "tilt_angle": 0.0, "total_accel": 0.0,
    "status": "safe",
    "timestamp": "",
    "esp32_connected": False,
    "last_seen": None
}

display_config = {
    "template": "default",
    "brightness": 255,
    "custom_message": ""
}

trip_stats = {
    "distance_km": 0.0,
    "max_gps_speed": 0.0,
    "max_hall_speed": 0.0,
    "speed_sum": 0.0,
    "speed_count": 0,
    "accident_count": 0,
    "start_time": None,
    "last_lat": None,
    "last_lng": None,
    "trail": []
}

HISTORY_FILE = "history_7days.json"
last_history_save = 0

def load_history():
    if os.path.exists(HISTORY_FILE):
        try:
            with open(HISTORY_FILE, 'r') as f:
                return json.load(f)
        except Exception:
            return []
    return []

history_data = load_history()

def save_history():
    # Keep only last 7 days (480 records/day * 7 = 3360 records)
    global history_data
    if len(history_data) > 3360:
        history_data = history_data[-3360:]
    try:
        with open(HISTORY_FILE, 'w') as f:
            json.dump(history_data, f)
    except Exception as e:
        print(f"Error saving history: {e}")

# ==================== Helpers ====================
def haversine(lat1, lon1, lat2, lon2):
    R = 6371
    dlat = math.radians(lat2 - lat1)
    dlon = math.radians(lon2 - lon1)
    a = (math.sin(dlat / 2) ** 2 +
         math.cos(math.radians(lat1)) * math.cos(math.radians(lat2)) *
         math.sin(dlon / 2) ** 2)
    return R * 2 * math.atan2(math.sqrt(a), math.sqrt(1 - a))


def send_telegram(message):
    url = f"https://api.telegram.org/bot{TELEGRAM_BOT_TOKEN}/sendMessage"
    for chat_id in TELEGRAM_CHAT_IDS:
        payload = {"chat_id": chat_id, "text": message, "parse_mode": "HTML"}
        try:
            http_requests.post(url, json=payload, timeout=5)
            print(f"Telegram alert sent to {chat_id} ✓")
        except Exception as e:
            print(f"Telegram send failed for {chat_id}: {e}")


def trip_stats_summary():
    elapsed = 0
    if trip_stats['start_time']:
        elapsed = int(time.time() - trip_stats['start_time'])
    avg = 0.0
    if trip_stats['speed_count'] > 0:
        avg = trip_stats['speed_sum'] / trip_stats['speed_count']
    return {
        "distance_km": round(trip_stats['distance_km'], 2),
        "max_gps_speed": round(trip_stats['max_gps_speed'], 1),
        "max_hall_speed": round(trip_stats['max_hall_speed'], 1),
        "avg_speed": round(avg, 1),
        "accident_count": trip_stats['accident_count'],
        "elapsed_seconds": elapsed,
        "trail": trip_stats['trail'][-500:]  # last 500 points
    }


def update_trip(lat, lng, gps_speed, hall_speed):
    # Start timer if ANY speed is detected
    if trip_stats['start_time'] is None and (gps_speed > 0 or hall_speed > 0):
        trip_stats['start_time'] = time.time()
        
    # Only calculate GPS distance and trail if we have valid coordinates
    if lat != 0 or lng != 0:
        if trip_stats['last_lat'] is not None:
            d = haversine(trip_stats['last_lat'], trip_stats['last_lng'], lat, lng)
            if d < 0.5:
                trip_stats['distance_km'] += d
        trip_stats['last_lat'] = lat
        trip_stats['last_lng'] = lng
        
        trip_stats['trail'].append([lat, lng, gps_speed])
        if len(trip_stats['trail']) > 2000:
            trip_stats['trail'] = trip_stats['trail'][-1500:]

    # Always update max speeds
    if gps_speed > trip_stats['max_gps_speed']:
        trip_stats['max_gps_speed'] = gps_speed
    if hall_speed > trip_stats['max_hall_speed']:
        trip_stats['max_hall_speed'] = hall_speed
        
    # Update average speed metrics based on whichever speed is higher (to ensure we capture indoor hall testing)
    current_highest_speed = max(gps_speed, hall_speed)
    if current_highest_speed > 0:
        trip_stats['speed_sum'] += current_highest_speed
        trip_stats['speed_count'] += 1

    # Save to 7-day history every 3 minutes
    global last_history_save
    current_time = time.time()
    if current_time - last_history_save >= 180:
        history_entry = {
            "timestamp": datetime.now().strftime("%Y-%m-%d %H:%M:%S"),
            "gps_speed": round(gps_speed, 1),
            "hall_speed": round(hall_speed, 1),
            "lat": lat,
            "lng": lng
        }
        history_data.append(history_entry)
        save_history()
        last_history_save = current_time


# ==================== Connection watchdog ====================
def esp32_watchdog():
    while True:
        time.sleep(5)
        if telemetry['last_seen'] and (time.time() - telemetry['last_seen'] > 10):
            if telemetry['esp32_connected']:
                telemetry['esp32_connected'] = False
                socketio.emit('telemetry_update', {**telemetry, 'trip': trip_stats_summary()})

threading.Thread(target=esp32_watchdog, daemon=True).start()


# ==================== Routes ====================
@app.route('/')
def index():
    return render_template('index.html')


@app.route('/manifest.json')
def manifest():
    return send_from_directory('.', 'manifest.json')

@app.route('/api/history', methods=['GET'])
def get_history():
    return jsonify(history_data)


@app.route('/update', methods=['POST'])
def update():
    """Receive telemetry from ESP32 (GPS + Hall + MPU)."""
    data = request.get_json()
    if not data:
        return jsonify({"status": "error"}), 400

    telemetry['lat'] = data.get('lat', 0.0)
    telemetry['lng'] = data.get('lng', 0.0)
    telemetry['gps_speed'] = data.get('speed', 0.0)
    telemetry['satellites'] = data.get('satellites', 0)
    telemetry['hall_speed'] = data.get('hall_speed', 0.0)
    telemetry['rpm'] = data.get('rpm', 0.0)
    telemetry['tilt_angle'] = data.get('tilt_angle', 0.0)
    telemetry['total_accel'] = data.get('total_accel', 0.0)
    telemetry['timestamp'] = datetime.now().strftime("%H:%M:%S")
    telemetry['esp32_connected'] = True
    telemetry['last_seen'] = time.time()

    # Trust the ESP32's status – it has persistence logic for accident detection
    # The server should not override with instant threshold checks
    status = data.get('status', 'safe')
    old_status = telemetry['status']
    telemetry['status'] = status

    update_trip(telemetry['lat'], telemetry['lng'],
                telemetry['gps_speed'], telemetry['hall_speed'])

    socketio.emit('telemetry_update', {**telemetry, 'trip': trip_stats_summary()})
    if status != old_status:
        socketio.emit('status_change', {"status": status})

    print(f"[{telemetry['timestamp']}] GPS:{telemetry['gps_speed']:.1f} "
          f"Hall:{telemetry['hall_speed']:.1f} Tilt:{telemetry['tilt_angle']:.0f}° "
          f"Acc:{telemetry['total_accel']:.2f}g  [{status.upper()}]")

    return jsonify({"status": "ok", "display": display_config})


@app.route('/accident', methods=['POST'])
def accident():
    """Receive accident alert → Telegram + WebSocket."""
    data = request.get_json() or {}
    lat = data.get('lat', telemetry['lat'])
    lng = data.get('lng', telemetry['lng'])
    ts = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    maps_link = f"https://maps.google.com/?q={lat},{lng}"

    telemetry['status'] = 'accident'
    trip_stats['accident_count'] += 1

    msg = (
        "🚨 <b>ACCIDENT DETECTED!</b>\n"
        "━━━━━━━━━━━━━━━━━━━\n"
        f"📍 Location: {lat:.6f}, {lng:.6f}\n"
        f"🗺️ <a href='{maps_link}'>Open in Google Maps</a>\n"
        f"🕐 Time: {ts}\n"
        "━━━━━━━━━━━━━━━━━━━\n"
        "⚠️ Please check on the rider immediately!"
    )
    threading.Thread(target=send_telegram, args=(msg,), daemon=True).start()

    socketio.emit('accident_alert', {
        "lat": lat, "lng": lng, "timestamp": ts, "maps_url": maps_link
    })
    socketio.emit('status_change', {"status": "accident"})

    print(f"*** ACCIDENT ALERT *** {lat:.6f},{lng:.6f}")
    return jsonify({"status": "alert_sent"})


@app.route('/display', methods=['GET'])
def get_display():
    """ESP32 polls this to get current display template."""
    return jsonify(display_config)


@app.route('/display', methods=['POST'])
def set_display():
    """Web app sets the active display template."""
    data = request.get_json()
    if data:
        display_config['template'] = data.get('template', display_config['template'])
        display_config['brightness'] = data.get('brightness', display_config['brightness'])
        display_config['custom_message'] = data.get('custom_message', display_config['custom_message'])
        socketio.emit('display_changed', display_config)
    return jsonify(display_config)


@app.route('/latest')
def latest():
    """Fallback JSON polling endpoint."""
    return jsonify({**telemetry, 'trip': trip_stats_summary()})


@app.route('/trip/reset', methods=['POST'])
def reset_trip():
    trip_stats.update({
        "distance_km": 0.0, "max_gps_speed": 0.0, "max_hall_speed": 0.0,
        "speed_sum": 0.0, "speed_count": 0, "accident_count": 0,
        "start_time": None, "last_lat": None, "last_lng": None, "trail": []
    })
    socketio.emit('telemetry_update', {**telemetry, 'trip': trip_stats_summary()})
    return jsonify({"status": "reset"})


# ==================== WebSocket Events ====================
@socketio.on('connect')
def on_connect():
    socketio.emit('telemetry_update', {**telemetry, 'trip': trip_stats_summary()})
    socketio.emit('display_changed', display_config)


app.config['TEMPLATES_AUTO_RELOAD'] = True
if __name__ == '__main__':
    print("=" * 50)
    print("  🏍️  Bike Computer Server")
    print("  http://0.0.0.0:5000")
    print("=" * 50)
    socketio.run(app, host='0.0.0.0', port=5000, debug=True, allow_unsafe_werkzeug=True)