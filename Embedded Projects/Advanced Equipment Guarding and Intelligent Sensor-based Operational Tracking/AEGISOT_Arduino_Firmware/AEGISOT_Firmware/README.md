# AEGISOT ESP32 Firmware (Arduino IDE)

## Included behavior

- ESP32 DevKit V1 / ESP32-WROOM-32
- DS18B20 waterproof temperature probe (non-blocking conversion)
- MPU6050 acceleration and angular-rate telemetry over I2C
- INA219 bus voltage, current, shunt voltage and power telemetry over I2C
- Hall AB encoder quadrature count, counts/s, measured direction, and optional RPM / revolutions
- TB6612FNG Channel A motor control by MQTT commands
- JSON MQTT telemetry/status topics
- Safe boot state: motor off, TB6612 STBY low
- Motor stops when Wi-Fi/MQTT is lost or the 5 s command lease expires
- Motion commands require the current session_id, so a retained command from a prior MQTT session is rejected
- Current monitor trips motor above the configured 500 mA software threshold (supplementary protection only)

## 1. Install Arduino IDE libraries

In Arduino IDE: Sketch -> Include Library -> Manage Libraries, install:

1. OneWire
2. DallasTemperature
3. PubSubClient (Nick O'Leary)
4. ArduinoJson (Benoit Blanchon; compatible with v6 or v7)
5. Adafruit MPU6050
6. Adafruit Unified Sensor
7. Adafruit INA219

Install the ESP32 board platform by Espressif Systems from Boards Manager, then choose **ESP32 Dev Module**.

## 2. Configure the sketch

Open `AEGISOT_Firmware.ino`, then edit:

- `WIFI_SSID`, `WIFI_PASSWORD`
- `MQTT_HOST`, `MQTT_PORT`, `MQTT_USERNAME`, `MQTT_PASSWORD`
- `AEGISOT_DEVICE_ID` and the topic macros if they differ from your backend contract
- `ENCODER_COUNTS_PER_OUTPUT_REV` after checking the encoder datasheet and where it is mounted
- `ENCODER_REVERSE_DIRECTION` after checking whether manual forward rotation increases the count
- `MAX_MOTOR_SPEED_PCT` and `MOTOR_OVERCURRENT_LIMIT_MA` only after testing motor and driver limits

With values left as `CHANGE_ME`, the sketch builds but does not connect to Wi-Fi/MQTT. The motor remains stopped.

## 3. Pin map

| Device signal | ESP32 pin |
|---|---:|
| DS18B20 DATA | GPIO 4 |
| I2C SDA (MPU6050 + INA219) | GPIO 21 |
| I2C SCL (MPU6050 + INA219) | GPIO 22 |
| TB6612 PWMA | GPIO 25 |
| TB6612 AIN1 | GPIO 26 |
| TB6612 AIN2 | GPIO 27 |
| TB6612 STBY | GPIO 14 |
| Encoder A | GPIO 32 |
| Encoder B | GPIO 33 |

### Wiring / power cautions

- DS18B20 in 3-wire mode: VDD to 3.3 V, GND to GND, DATA to GPIO4; a 4.7 kΩ pull-up from DATA to 3.3 V is a common starting point. Verify the cable pinout from the supplier; wire colours are not universal.
- MPU6050 and INA219 share GPIO21/22 I2C. Power their logic side at 3.3 V unless the breakout documentation explicitly confirms a different safe arrangement. Typical addresses are MPU6050 0x68 and INA219 0x40.
- TB6612: VCC = 3.3 V logic, VM = separate regulated motor supply suitable for the motor (the selected motor is rated 1.5-3 V), GND = common ground, AO1/AO2 to the motor. Connect PWMA/AIN1/AIN2/STBY to the ESP32 pins listed above. Add a physical ~10 kΩ pull-down from STBY to GND so the driver stays disabled while the ESP32 boots.
- Put INA219 VIN+ on the positive motor-supply input and VIN- toward TB6612 VM, if the module's schematic matches the standard high-side breakout. This measures current drawn by the motor-driver supply, not a perfect instantaneous winding-current waveform. Confirm the module VIN/shunt wiring.
- The Adafruit INA219 library default calibration assumes a 0.1 Ω shunt for its 32 V / 2 A profile. Check the shunt resistor marking and module datasheet before relying on current values.
- Confirm the encoder supply/output type, logic high voltage, counts-per-revolution specification and installation. Never connect a 5 V encoder output directly to an ESP32 GPIO; use a suitable level interface if needed.
- Never power the motor from an ESP32 GPIO or 3.3 V pin. Use a current-limited bench supply for first motor tests, set to no more than the motor's verified voltage/current limits. The INA219/software trip is not fast enough to replace a current-limited supply or fuse.

## 4. MQTT topics

Default topics (change them to match the backend contract):

- `factory/devices/aegisot-node-01/telemetry`
- `factory/devices/aegisot-node-01/status`
- `factory/devices/aegisot-node-01/commands`

Telemetry publishes every second (not retained). Status is retained. **Publish motion commands with retain=false.** The session token also protects against stale retained commands.

## 5. Command payloads

First subscribe to the status topic and read the current `session_id`, for example `"A1B2C3D4"`. The actual token is generated at every successful MQTT connection and will differ.

### Run forward at 25% PWM

Publish to `.../commands` with retain **off**:

```json
{
  "session_id": "A1B2C3D4",
  "motor": {
    "enable": true,
    "speed_pct": 25,
    "direction": "forward"
  }
}
```

Replace the example session_id with the current value in the retained status message. The first bench test is capped at 40% PWM. The command must be refreshed within 5 seconds; otherwise the motor automatically stops.

### Reverse at 20% PWM

```json
{
  "session_id": "CURRENT_SESSION_ID",
  "motor": {
    "enable": true,
    "speed_pct": 20,
    "direction": "reverse"
  }
}
```

### Stop (session token not required)

```json
{
  "motor": { "enable": false }
}
```

### Clear an overcurrent latch while stopped

Send the following only after checking the hardware and when measured current has fallen below the clear threshold:

```json
{
  "motor": { "enable": false },
  "clear_fault": true
}
```

### Zero relative encoder position (only while stopped)

```json
{
  "encoder": { "zero": true }
}
```

The encoder has no assumed index/homing sensor, so position is incremental since boot or this zero command; it is not absolute physical position.

## 6. Important encoder note

The decoder counts valid transitions on both A and B channels (x4). `ENCODER_COUNTS_PER_OUTPUT_REV` must be the **decoded x4 count per revolution of the shaft whose position you want to report**. Manufacturers use PPR/CPR differently; do not set this value from a product title alone. Until it is set, telemetry still reports `position_counts`, `delta_counts`, `counts_per_second` and measured direction, while `rpm` and `position_output_revolutions` are `null`.

## 7. First test sequence

1. Leave the motor's power supply disconnected. Compile and upload.
2. Confirm Serial Monitor at 115200 and ensure the board boots without resets.
3. Confirm DS18B20, MPU6050 and INA219 are detected/read; check `valid` flags in telemetry.
4. Confirm MQTT connection and subscribe to the status/telemetry topics.
5. Check encoder count by turning the motor shaft by hand with motor power disconnected. Confirm A/B logic voltage is ESP32-safe.
6. Before motor power: verify the motor part/variant, stall current, driver limits, INA219 shunt, fuse and bench supply current limit. The RS PRO product listing includes a 960 mA current rating, so do not assume the TB6612FNG module is safe without checking these details.
7. Only after that, test a low-speed command with the motor unloaded and a current-limited supply. Watch current and temperature. Stop immediately if the current limit trips, the driver heats unusually, or wiring/supply voltage is abnormal.

## Data units

- DS18B20: degrees Celsius
- MPU6050 acceleration: m/s^2
- MPU6050 gyroscope: rad/s
- INA219 voltage: V; current: mA; power: mW
- Encoder: counts and counts/s; RPM/revolutions only when configured
