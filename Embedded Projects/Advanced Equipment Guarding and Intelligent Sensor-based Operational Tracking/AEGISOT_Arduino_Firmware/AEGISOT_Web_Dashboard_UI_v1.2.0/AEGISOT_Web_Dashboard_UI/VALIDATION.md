# Validation record

Performed in this environment:

- `python3 -m py_compile aegisot_web_bridge.py` passed.
- `node --check` on the JavaScript extracted from `index.html` passed when Node.js is available.
- HTTP smoke test for `/api/state` returned JSON and `/api/stage/start` safely returned HTTP 409 when serial was deliberately disabled.
- The dashboard runs with the Python standard library plus `pyserial`; it does not use MQTT.

Not performed:

- No ESP32 was connected during validation.
- No physical motor, sensor, USB serial, or real CSV acquisition test was performed.
- Firmware compile was not performed in this package-validation step.

The first hardware run must verify that the bridge receives `LOG_STARTED`, `CSV_DATA`, and `LOG_STOPPED` from the exact included reference sketch and that actual values appear in the dashboard. The web bridge does not modify firmware or bypass firmware safety checks.


## Optional MQTT publisher addition

- `aegisot_web_bridge.py` remains the only process that opens the ESP32 serial port.
- Added optional MQTT publication in a separate `mqtt_publisher.py` module. MQTT is only started when `--mqtt-config` is supplied.
- No firmware source is changed by this feature, and the publisher does not subscribe to any topic or send commands to the ESP32.
- Publishing schema includes aggregate telemetry, status, motor, individual sensor, recording, event, and availability topics.
- Requires real broker credentials for end-to-end network validation; no credentials or live broker were available in this build environment.
