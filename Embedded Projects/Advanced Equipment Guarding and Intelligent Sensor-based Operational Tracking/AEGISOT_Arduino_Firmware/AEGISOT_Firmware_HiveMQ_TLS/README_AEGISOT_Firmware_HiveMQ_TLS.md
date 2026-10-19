# AEGISOT ESP32 Firmware — HiveMQ Cloud TLS

This sketch is a modified version of the provided AEGISOT firmware for an ESP32 DevKit V1 / ESP32-WROOM-32.

## Important security changes

- Uses `WiFiClientSecure` and TLS on HiveMQ Cloud port `8883`.
- Embeds the Let's Encrypt ISRG Root X1 root CA and verifies the broker certificate.
- Requests NTP time synchronization before attempting the TLS connection.
- Does not use `setInsecure()`.
- Replaces previously present Wi-Fi and MQTT credentials with placeholders. Fill them locally; do not upload credentials to GitHub or share screenshots that show them.
- Retains stop-on-boot, stop-on-network-loss, 5-second command lease, and INA219 software-current-trip logic.

## Before you upload

1. Open `AEGISOT_Firmware_HiveMQ_TLS.ino` in Arduino IDE.
2. Replace these placeholders near the top with your NEW/current credentials:
   - `CHANGE_ME_WIFI_SSID`
   - `CHANGE_ME_WIFI_PASSWORD`
   - `CHANGE_ME_MQTT_USERNAME`
   - `CHANGE_ME_MQTT_PASSWORD`
3. In HiveMQ Cloud → Access Management, make sure this MQTT user has permission to publish and subscribe to the required topics below.
4. Select the correct ESP32 board and COM/USB port.
5. Install these Arduino libraries with Library Manager:
   - PubSubClient
   - OneWire
   - DallasTemperature
   - Adafruit MPU6050
   - Adafruit Unified Sensor
   - Adafruit INA219
   - ArduinoJson (ArduinoJson 6.x is recommended for this sketch)
6. Use an ESP32 Arduino core version that provides `WiFiClientSecure` and `esp_arduino_version.h` (core 2.x or 3.x).
7. Compile and upload, then open Serial Monitor at `115200` baud.

The ESP32 generally requires a 2.4 GHz Wi-Fi network. The router's internet connection must allow NTP and outbound TCP port 8883.

## MQTT topics

Device ID: `aegisot-node-01`

- Telemetry publish: `factory/devices/aegisot-node-01/telemetry`
- Status publish: `factory/devices/aegisot-node-01/status`
- Commands subscribe: `factory/devices/aegisot-node-01/commands`

## First test: no motor power

1. Keep the motor's separate power disconnected, or disconnect the motor from the driver during the initial Wi-Fi/TLS test.
2. Upload the sketch and open Serial Monitor at 115200 baud.
3. Wait for messages indicating Wi-Fi connection, system clock synchronization, and `MQTT connected`.
4. In HiveMQ Cloud's MQTT Web Client / WebSocket client, subscribe to the telemetry and status topics listed above and confirm that messages arrive.
5. Do not send motor-enable commands until wiring and the motor driver/current sensor configuration have been reviewed.

## Command format

The firmware publishes the current `session_id` in the retained status topic. The dashboard/backend must read that *current* value and include it in every motor-enable/refresh command. A motor command is accepted only if the session ID matches, INA219 readings are valid, no overcurrent fault is latched, and PWM initialized successfully. A renewed valid enable command must arrive within 5 seconds or the motor stops.

Example STOP command (safe to use for topic testing):

```json
{"motor":{"enable":false}}
```

Example motor-enable schema — **do not use until the driver, motor current, and mechanism are safely tested**:

```json
{
  "session_id": "COPY_CURRENT_SESSION_ID_FROM_STATUS",
  "motor": {
    "enable": true,
    "direction": "forward",
    "speed_pct": 10
  }
}
```

The session ID above is a placeholder, not a valid token. Send no retained motor-enable messages.

## Hardware cautions

- TB6612FNG is still an assumption in this firmware. Confirm motor stall current and operating voltage against the actual driver's specification before connecting power.
- The 500 mA current trip is a software threshold, not a fuse or independent hardware protection. Use a current-limited bench supply during testing.
- Verify that the INA219 is wired in the actual motor-current path and that its breakout configuration is suitable before relying on readings.
- Encoder A/B signals must be 3.3 V-safe. Do not connect a 5 V push-pull output directly to an ESP32 GPIO.
- Set `ENCODER_COUNTS_PER_OUTPUT_REV` from the exact encoder datasheet and actual mechanical mounting. It remains `0`, so RPM and output-shaft revolutions are intentionally marked invalid.
- Confirm the DS18B20 wiring and 4.7 kΩ DATA-to-3.3 V pull-up if your probe requires it.

## Validation note

The code and embedded CA block were inspected statically, and the PEM certificate was parsed successfully. The sketch has **not** been compiled against your exact Arduino board package/libraries and has **not** been tested on physical hardware. Test compile, network connection, topic permissions, and sensor readings before enabling any actuator.

Root CA source: https://letsencrypt.org/certs/isrgrootx1.pem
HiveMQ Cloud TLS guidance: https://hivemq.atlassian.net/wiki/spaces/HCSP/pages/2566750258/Does+HiveMQ+Cloud+support+SSL-encrypted+TCP+connections
ESP32 TLS guidance: https://github.com/espressif/arduino-esp32/blob/master/libraries/NetworkClientSecure/README.md
