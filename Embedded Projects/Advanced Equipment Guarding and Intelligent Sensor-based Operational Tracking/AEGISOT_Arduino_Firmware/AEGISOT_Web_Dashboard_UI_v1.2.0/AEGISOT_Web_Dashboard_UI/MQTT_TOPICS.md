# AEGISOT MQTT topic contract (publish-only)

Default base topic: `factory/devices/aegisot-node-01`

Subscribe from another client to `factory/devices/aegisot-node-01/#` to receive everything.

| Topic | Payload purpose |
|---|---|
| `{base}/telemetry` | Full snapshot and telemetry values |
| `{base}/status` | Serial connection, recording/stage, motor status and fault |
| `{base}/motor` | Motor running/direction/speed/PWM/current/voltage |
| `{base}/sensors/ina219` | Voltage/current/power readings and validity |
| `{base}/sensors/temperature` | DS18B20 temperature and validity |
| `{base}/sensors/mpu6050` | Acceleration and gyro axes, vibration estimate and validity |
| `{base}/sensors/encoder` | Encoder count/delta/counts per second/CPR/RPM/RPM validity |
| `{base}/recording` | Stage, elapsed time, sample counters and target |
| `{base}/events` | New bridge events as independent JSON messages |
| `{base}/availability` | Retained online/offline last-will status |

## Operational guarantees

- Only Python `aegisot_web_bridge.py` is modified for integration; firmware is untouched.
- MQTT publishing is opt-in (`--mqtt-config mqtt_config.json`) and does not run in the existing launch command.
- The MQTT client subscribes to no topics and does not accept MQTT motor commands.
- All sensor values come from the existing bridge's serial parser. The publisher does not synthesize sensor readings.
- JSON `null` represents unavailable values.
- TLS certificate verification remains enabled when `tls` is `true`.
