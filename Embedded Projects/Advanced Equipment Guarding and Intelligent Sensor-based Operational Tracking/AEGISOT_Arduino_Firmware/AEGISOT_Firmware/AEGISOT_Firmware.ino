/*
  AEGISOT - ESP32 Hardware Firmware (Arduino IDE)
  Board: ESP32 DevKit V1 / ESP32-WROOM-32
  Motor driver assumption: TB6612FNG, channel A

  Safety design:
    - Motor is stopped at boot.
    - Motor is stopped if Wi-Fi/MQTT is lost or command lease expires.
    - Motor commands that enable motion require the current MQTT session_id.
    - Stop commands do not require session_id.
    - INA219 current monitoring trips the motor when the configured limit is exceeded.
    - Initial bench cap is 40% PWM and 500 mA current trip. Verify both for your actual motor/driver.

  IMPORTANT:
    - Configure Wi-Fi and MQTT settings below.
    - Confirm motor stall current and TB6612FNG suitability before applying motor power.
    - Set ENCODER_COUNTS_PER_OUTPUT_REV from the encoder's exact datasheet.
    - MQTT uses plain TCP (port 1883 by default); use only on a trusted lab network.
*/

#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_INA219.h>
#include <ArduinoJson.h>
#include <esp_arduino_version.h>
#include <esp_system.h>
#include <driver/gpio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>
#include <math.h>
#include <string.h>

// -----------------------------------------------------------------------------
// 1) USER CONFIGURATION - EDIT THESE VALUES BEFORE CONNECTING TO MQTT
// -----------------------------------------------------------------------------
static const char* WIFI_SSID     = "CHANGE_ME";
static const char* WIFI_PASSWORD = "CHANGE_ME";

// Broker address on your own LAN/server; do not use a public broker for actuator control.
static const char* MQTT_HOST = "CHANGE_ME";
static constexpr uint16_t MQTT_PORT = 1883;
static const char* MQTT_USERNAME = "";  // Leave empty only if broker allows anonymous access.
static const char* MQTT_PASSWORD = "";

#define AEGISOT_DEVICE_ID "aegisot-node-01"
#define AEGISOT_TOPIC_BASE "factory/devices/" AEGISOT_DEVICE_ID
static const char* TOPIC_TELEMETRY = AEGISOT_TOPIC_BASE "/telemetry";
static const char* TOPIC_STATUS    = AEGISOT_TOPIC_BASE "/status";
static const char* TOPIC_COMMANDS  = AEGISOT_TOPIC_BASE "/commands";

// Encoder setting: enter the number of decoded x4 counts per ONE output-shaft revolution.
// Keep 0 until the exact encoder specification / mounting position is confirmed.
static constexpr int32_t ENCODER_COUNTS_PER_OUTPUT_REV = 0;
static constexpr bool ENCODER_REVERSE_DIRECTION = false;

// Conservative prototype limits. Re-validate against the exact motor/driver specs.
// 40% PWM limits commanded duty during initial bench tests; do not raise without testing.
static constexpr uint8_t MAX_MOTOR_SPEED_PCT = 40;
// The official RS PRO listing shows a 960 mA current rating for its listed variant(s).
// 500 mA is an intentionally conservative SOFTWARE trip for bench testing, not a hardware fuse.
// It may stop the motor if its legitimate startup/load current exceeds this value.
static constexpr float MOTOR_OVERCURRENT_LIMIT_MA = 500.0f;
static constexpr uint32_t COMMAND_LEASE_TIMEOUT_MS = 5000;

// -----------------------------------------------------------------------------
// 2) PIN MAP - ESP32-WROOM-32
// -----------------------------------------------------------------------------
static constexpr uint8_t PIN_DS18B20 = 4;
static constexpr uint8_t PIN_I2C_SDA = 21;
static constexpr uint8_t PIN_I2C_SCL = 22;

// TB6612FNG Channel A
static constexpr uint8_t PIN_MOTOR_PWMA = 25;
static constexpr uint8_t PIN_MOTOR_AIN1 = 26;
static constexpr uint8_t PIN_MOTOR_AIN2 = 27;
static constexpr uint8_t PIN_MOTOR_STBY = 14;

// Hall encoder A/B; quadrature decode counts all valid A/B edges (x4).
static constexpr uint8_t PIN_ENCODER_A = 32;
static constexpr uint8_t PIN_ENCODER_B = 33;

static constexpr uint32_t PWM_FREQUENCY_HZ = 20000;
static constexpr uint8_t PWM_RESOLUTION_BITS = 8;
static constexpr uint8_t PWM_CHANNEL_LEGACY = 0;

static constexpr uint32_t TELEMETRY_INTERVAL_MS = 1000;
static constexpr uint32_t MPU_SAMPLE_INTERVAL_MS = 100;
static constexpr uint32_t POWER_SAMPLE_INTERVAL_MS = 100;
static constexpr uint32_t TEMPERATURE_INTERVAL_MS = 1000;
static constexpr uint32_t DS18B20_10BIT_CONVERSION_MS = 200;
static constexpr uint32_t I2C_RETRY_INTERVAL_MS = 5000;
static constexpr uint32_t WIFI_RETRY_INTERVAL_MS = 10000;
static constexpr uint32_t MQTT_RETRY_INTERVAL_MS = 3000;

// -----------------------------------------------------------------------------
// 3) OBJECTS AND STATE
// -----------------------------------------------------------------------------
WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

OneWire oneWire(PIN_DS18B20);
DallasTemperature temperatureSensor(&oneWire);
Adafruit_MPU6050 mpu;
Adafruit_INA219 ina219;

bool mpuOk = false;
bool inaOk = false;
bool temperatureValid = false;
bool mpuDataValid = false;
bool powerDataValid = false;
bool temperatureConversionPending = false;
bool motorRunning = false;
bool motorDirectionForward = true;
bool overcurrentFaultLatched = false;
bool wifiAttempted = false;
bool mqttAttempted = false;

float temperatureC = NAN;
float accelX = NAN, accelY = NAN, accelZ = NAN;
float gyroX = NAN, gyroY = NAN, gyroZ = NAN;
float busVoltageV = NAN, shuntVoltageMv = NAN, currentMa = NAN, powerMw = NAN;
uint8_t appliedSpeedPct = 0;

char bootId[9] = "00000000";
char sessionId[9] = "00000000";
String mqttClientId;

uint32_t lastWifiAttemptMs = 0;
uint32_t lastMqttAttemptMs = 0;
uint32_t lastI2CRetryMs = 0;
uint32_t lastMpuSampleMs = 0;
uint32_t lastPowerSampleMs = 0;
uint32_t lastTelemetryMs = 0;
uint32_t lastTemperatureRequestMs = 0;
uint32_t temperatureRequestStartedMs = 0;
uint32_t lastDriveCommandMs = 0;

// Encoder counter is updated in GPIO ISRs.
portMUX_TYPE encoderMux = portMUX_INITIALIZER_UNLOCKED;
volatile int32_t encoderCount = 0;
volatile uint8_t encoderPreviousAB = 0;
int32_t lastTelemetryEncoderCount = 0;
uint32_t lastEncoderTelemetryMs = 0;
float encoderCountsPerSecond = 0.0f;

// -----------------------------------------------------------------------------
// 4) MOTOR DRIVER HELPERS
// -----------------------------------------------------------------------------
void writeMotorPwm(uint8_t duty) {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWrite(PIN_MOTOR_PWMA, duty);
#else
  ledcWrite(PWM_CHANNEL_LEGACY, duty);
#endif
}

bool setupMotorPwm() {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  return ledcAttach(PIN_MOTOR_PWMA, PWM_FREQUENCY_HZ, PWM_RESOLUTION_BITS);
#else
  ledcSetup(PWM_CHANNEL_LEGACY, PWM_FREQUENCY_HZ, PWM_RESOLUTION_BITS);
  ledcAttachPin(PIN_MOTOR_PWMA, PWM_CHANNEL_LEGACY);
  return true;
#endif
}

void motorStop() {
  // Keep the driver in standby; no active motor drive at boot or on failures.
  writeMotorPwm(0);
  digitalWrite(PIN_MOTOR_AIN1, LOW);
  digitalWrite(PIN_MOTOR_AIN2, LOW);
  digitalWrite(PIN_MOTOR_STBY, LOW);
  motorRunning = false;
  appliedSpeedPct = 0;
}

void motorStart(bool forward, uint8_t speedPct) {
  if (speedPct == 0) {
    motorStop();
    return;
  }
  if (speedPct > MAX_MOTOR_SPEED_PCT) speedPct = MAX_MOTOR_SPEED_PCT;

  // Set direction before enabling the bridge.
  digitalWrite(PIN_MOTOR_AIN1, forward ? HIGH : LOW);
  digitalWrite(PIN_MOTOR_AIN2, forward ? LOW : HIGH);
  digitalWrite(PIN_MOTOR_STBY, HIGH);
  const uint8_t duty = (uint16_t)speedPct * 255U / 100U;
  writeMotorPwm(duty);

  motorDirectionForward = forward;
  appliedSpeedPct = speedPct;
  motorRunning = true;
}

const char* commandedDirectionLabel() {
  if (!motorRunning) return "stopped";
  return motorDirectionForward ? "forward" : "reverse";
}

// -----------------------------------------------------------------------------
// 5) ENCODER QUADRATURE DECODER (x4)
// -----------------------------------------------------------------------------
void IRAM_ATTR onEncoderChange() {
  portENTER_CRITICAL_ISR(&encoderMux);
  const uint8_t a = (uint8_t)gpio_get_level((gpio_num_t)PIN_ENCODER_A);
  const uint8_t b = (uint8_t)gpio_get_level((gpio_num_t)PIN_ENCODER_B);
  const uint8_t currentAB = (uint8_t)((a << 1) | b);
  const uint8_t transition = (uint8_t)((encoderPreviousAB << 2) | currentAB);
  int8_t delta = 0;

  switch (transition) {
    // One quadrature direction.
    case 0b0001: case 0b0111: case 0b1110: case 0b1000:
      delta = 1;
      break;
    // Opposite quadrature direction.
    case 0b0010: case 0b1011: case 0b1101: case 0b0100:
      delta = -1;
      break;
    default:
      // Same state or invalid two-bit jump: do not count.
      break;
  }

  if (ENCODER_REVERSE_DIRECTION) delta = -delta;
  encoderCount += delta;
  encoderPreviousAB = currentAB;
  portEXIT_CRITICAL_ISR(&encoderMux);
}

int32_t getEncoderCountSnapshot() {
  int32_t snapshot;
  portENTER_CRITICAL(&encoderMux);
  snapshot = encoderCount;
  portEXIT_CRITICAL(&encoderMux);
  return snapshot;
}

bool zeroEncoderPosition() {
  if (motorRunning) return false;
  portENTER_CRITICAL(&encoderMux);
  encoderCount = 0;
  encoderPreviousAB = (uint8_t)((gpio_get_level((gpio_num_t)PIN_ENCODER_A) << 1) |
                                gpio_get_level((gpio_num_t)PIN_ENCODER_B));
  portEXIT_CRITICAL(&encoderMux);
  lastTelemetryEncoderCount = 0;
  encoderCountsPerSecond = 0.0f;
  return true;
}

// -----------------------------------------------------------------------------
// 6) SENSOR INITIALIZATION AND SAMPLING
// -----------------------------------------------------------------------------
void configureMpuAfterBegin() {
  mpu.setAccelerometerRange(MPU6050_RANGE_4_G);
  mpu.setGyroRange(MPU6050_RANGE_500_DEG);
  mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);
}

void tryInitializeI2CDevices(uint32_t now) {
  if (!mpuOk) {
    mpuOk = mpu.begin(0x68, &Wire);
    if (mpuOk) {
      configureMpuAfterBegin();
      Serial.println("MPU6050: detected at I2C address 0x68");
    }
  }
  if (!inaOk) {
    // Adafruit INA219 library defaults to 32 V / 2 A calibration for a 0.1-ohm shunt.
    // Verify the shunt marking / breakout specification before relying on readings.
    inaOk = ina219.begin(&Wire);
    if (inaOk) Serial.println("INA219: detected (default 32V/2A calibration)");
  }
  lastI2CRetryMs = now;
}

void startTemperatureConversion(uint32_t now) {
  temperatureSensor.requestTemperatures();
  temperatureRequestStartedMs = now;
  lastTemperatureRequestMs = now;
  temperatureConversionPending = true;
}

void serviceTemperature(uint32_t now) {
  if (temperatureConversionPending &&
      (uint32_t)(now - temperatureRequestStartedMs) >= DS18B20_10BIT_CONVERSION_MS) {
    const float value = temperatureSensor.getTempCByIndex(0);
    temperatureValid = isfinite(value) && value != DEVICE_DISCONNECTED_C && value >= -55.0f && value <= 125.0f;
    temperatureC = temperatureValid ? value : NAN;
    temperatureConversionPending = false;
  }

  if (!temperatureConversionPending &&
      (uint32_t)(now - lastTemperatureRequestMs) >= TEMPERATURE_INTERVAL_MS) {
    startTemperatureConversion(now);
  }
}

void serviceMpuSampling(uint32_t now) {
  if ((uint32_t)(now - lastMpuSampleMs) < MPU_SAMPLE_INTERVAL_MS) return;
  lastMpuSampleMs = now;

  if (!mpuOk) {
    mpuDataValid = false;
    return;
  }

  sensors_event_t accelEvent, gyroEvent, tempEvent;
  mpuDataValid = mpu.getEvent(&accelEvent, &gyroEvent, &tempEvent);
  if (mpuDataValid) {
    accelX = accelEvent.acceleration.x;
    accelY = accelEvent.acceleration.y;
    accelZ = accelEvent.acceleration.z;
    gyroX = gyroEvent.gyro.x;
    gyroY = gyroEvent.gyro.y;
    gyroZ = gyroEvent.gyro.z;
  }
}

void servicePowerSampling(uint32_t now) {
  if ((uint32_t)(now - lastPowerSampleMs) < POWER_SAMPLE_INTERVAL_MS) return;
  lastPowerSampleMs = now;

  if (!inaOk) {
    powerDataValid = false;
    return;
  }

  const float newBusVoltage = ina219.getBusVoltage_V();
  const float newShuntVoltage = ina219.getShuntVoltage_mV();
  const float newCurrent = ina219.getCurrent_mA();
  const float newPower = ina219.getPower_mW();
  powerDataValid = isfinite(newBusVoltage) && isfinite(newShuntVoltage) &&
                   isfinite(newCurrent) && isfinite(newPower);

  if (powerDataValid) {
    busVoltageV = newBusVoltage;
    shuntVoltageMv = newShuntVoltage;
    currentMa = newCurrent;
    powerMw = newPower;

    // Software protection is supplementary; a current-limited bench supply is still required.
    if (motorRunning && fabsf(currentMa) > MOTOR_OVERCURRENT_LIMIT_MA) {
      motorStop();
      overcurrentFaultLatched = true;
      Serial.printf("SAFETY TRIP: INA219 current %.1f mA exceeds %.1f mA\n",
                    fabsf(currentMa), MOTOR_OVERCURRENT_LIMIT_MA);
    }
  }
}

// -----------------------------------------------------------------------------
// 7) MQTT STATUS, TELEMETRY, AND COMMANDS
// -----------------------------------------------------------------------------
void createRandomToken(char token[9]) {
  const uint32_t value = esp_random();
  snprintf(token, 9, "%08lX", (unsigned long)value);
}

void publishStatus(const char* status, const char* reason) {
  if (!mqttClient.connected()) return;

  StaticJsonDocument<384> doc;
  doc["device_id"] = AEGISOT_DEVICE_ID;
  doc["status"] = status;
  doc["reason"] = reason;
  doc["boot_id"] = bootId;
  doc["session_id"] = sessionId;
  doc["motor_enabled"] = motorRunning;
  doc["motor_direction"] = commandedDirectionLabel();
  doc["motor_speed_pct"] = appliedSpeedPct;
  doc["overcurrent_fault"] = overcurrentFaultLatched;
  doc["max_speed_pct"] = MAX_MOTOR_SPEED_PCT;
  doc["current_limit_ma"] = MOTOR_OVERCURRENT_LIMIT_MA;
  doc["command_timeout_ms"] = COMMAND_LEASE_TIMEOUT_MS;

  String payload;
  serializeJson(doc, payload);
  mqttClient.publish(TOPIC_STATUS, payload.c_str(), true); // retained status is okay; commands are protected by session_id.
}

void publishTelemetry(uint32_t now) {
  if (!mqttClient.connected()) return;

  StaticJsonDocument<1024> doc;
  doc["device_id"] = AEGISOT_DEVICE_ID;
  doc["firmware_version"] = "0.1.0";
  doc["uptime_ms"] = now;
  doc["boot_id"] = bootId;
  doc["session_id"] = sessionId;

  JsonObject motor = doc.createNestedObject("motor");
  motor["enabled"] = motorRunning;
  motor["commanded_direction"] = commandedDirectionLabel();
  motor["speed_pct"] = appliedSpeedPct;
  motor["max_speed_pct"] = MAX_MOTOR_SPEED_PCT;
  motor["overcurrent_fault"] = overcurrentFaultLatched;
  motor["current_limit_ma"] = MOTOR_OVERCURRENT_LIMIT_MA;

  JsonObject temp = doc.createNestedObject("temperature");
  temp["valid"] = temperatureValid;
  if (temperatureValid) temp["celsius"] = temperatureC;
  else temp["celsius"] = nullptr;

  JsonObject motion = doc.createNestedObject("mpu6050");
  motion["valid"] = mpuDataValid;
  if (mpuDataValid) {
    JsonObject accel = motion.createNestedObject("acceleration_m_s2");
    accel["x"] = accelX; accel["y"] = accelY; accel["z"] = accelZ;
    JsonObject gyro = motion.createNestedObject("gyro_rad_s");
    gyro["x"] = gyroX; gyro["y"] = gyroY; gyro["z"] = gyroZ;
  } else {
    motion["acceleration_m_s2"] = nullptr;
    motion["gyro_rad_s"] = nullptr;
  }

  JsonObject power = doc.createNestedObject("ina219");
  power["valid"] = powerDataValid;
  if (powerDataValid) {
    power["bus_voltage_v"] = busVoltageV;
    power["shunt_voltage_mv"] = shuntVoltageMv;
    power["current_ma"] = currentMa;
    power["power_mw"] = powerMw;
  } else {
    power["bus_voltage_v"] = nullptr;
    power["shunt_voltage_mv"] = nullptr;
    power["current_ma"] = nullptr;
    power["power_mw"] = nullptr;
  }

  const int32_t countNow = getEncoderCountSnapshot();
  const uint32_t elapsed = (lastEncoderTelemetryMs == 0) ? TELEMETRY_INTERVAL_MS :
                           (uint32_t)(now - lastEncoderTelemetryMs);
  const int32_t delta = countNow - lastTelemetryEncoderCount;
  encoderCountsPerSecond = elapsed > 0 ? (float)delta * 1000.0f / (float)elapsed : 0.0f;

  JsonObject encoder = doc.createNestedObject("encoder");
  encoder["position_counts"] = countNow; // relative count since boot or an explicit zero command
  encoder["delta_counts"] = delta;
  encoder["counts_per_second"] = encoderCountsPerSecond;
  encoder["measured_direction"] = (delta > 0) ? "forward" : ((delta < 0) ? "reverse" : "stopped");
  encoder["counts_per_output_rev_configured"] = ENCODER_COUNTS_PER_OUTPUT_REV;
  if (ENCODER_COUNTS_PER_OUTPUT_REV > 0) {
    encoder["rpm"] = encoderCountsPerSecond * 60.0f / (float)ENCODER_COUNTS_PER_OUTPUT_REV;
    encoder["position_output_revolutions"] = (float)countNow / (float)ENCODER_COUNTS_PER_OUTPUT_REV;
    encoder["rpm_valid"] = true;
  } else {
    encoder["rpm"] = nullptr;
    encoder["position_output_revolutions"] = nullptr;
    encoder["rpm_valid"] = false;
  }

  char payload[1024];
  const size_t length = serializeJson(doc, payload, sizeof(payload));
  if (length > 0 && length < sizeof(payload)) {
    if (!mqttClient.publish(TOPIC_TELEMETRY, (const uint8_t*)payload, (unsigned int)length, false)) {
      Serial.println("MQTT telemetry publish failed");
    }
  } else {
    Serial.println("Telemetry JSON exceeded buffer; not published");
  }

  lastTelemetryEncoderCount = countNow;
  lastEncoderTelemetryMs = now;
}

void handleMqttCommand(char* topic, byte* payload, unsigned int length) {
  if (strcmp(topic, TOPIC_COMMANDS) != 0) return;

  // Reject oversized/invalid commands and fail safe to motor stop.
  char message[512];
  if (length == 0 || length >= sizeof(message)) {
    motorStop();
    publishStatus("rejected", "command_payload_size_invalid");
    return;
  }
  memcpy(message, payload, length);
  message[length] = '\0';

  StaticJsonDocument<512> doc;
  const DeserializationError error = deserializeJson(doc, message);
  if (error) {
    motorStop();
    publishStatus("rejected", "command_json_invalid");
    Serial.printf("Rejected malformed command JSON: %s\n", error.c_str());
    return;
  }

  // Zeroing is allowed only while stopped; this is a relative encoder, not an absolute encoder.
  if (doc["encoder"]["zero"] | false) {
    if (!zeroEncoderPosition()) {
      motorStop();
      publishStatus("rejected", "encoder_zero_requires_motor_stopped");
      return;
    }
    publishStatus("stopped", "encoder_position_zeroed");
  }

  // If there's no motor object, do not interpret this message as a motor command.
  if (!doc["motor"].is<JsonObjectConst>()) return;

  const bool enable = doc["motor"]["enable"] | false;
  if (!enable) {
    motorStop(); // STOP is always accepted, even if session_id is absent.
    if (doc["clear_fault"] | false) {
      if (powerDataValid && fabsf(currentMa) < MOTOR_OVERCURRENT_LIMIT_MA * 0.8f) {
        overcurrentFaultLatched = false;
      } else {
        publishStatus("fault", "cannot_clear_fault_current_not_safe");
        return;
      }
    }
    publishStatus(overcurrentFaultLatched ? "fault" : "stopped",
                  overcurrentFaultLatched ? "fault_latched" : "remote_stop");
    return;
  }

  // Each MQTT connection gets a new token. Old retained motion commands therefore fail this check.
  const char* receivedSession = doc["session_id"] | "";
  if (receivedSession[0] == '\0' || strcmp(receivedSession, sessionId) != 0) {
    motorStop();
    publishStatus("rejected", "session_id_missing_or_stale");
    return;
  }

  if (overcurrentFaultLatched) {
    motorStop();
    publishStatus("fault", "overcurrent_fault_latched");
    return;
  }
  if (!inaOk || !powerDataValid) {
    motorStop();
    publishStatus("rejected", "ina219_current_measurement_unavailable");
    return;
  }

  int speedPct = doc["motor"]["speed_pct"] | 0;
  const char* direction = doc["motor"]["direction"] | "";
  if (speedPct <= 0) {
    motorStop();
    publishStatus("stopped", "zero_speed_command");
    return;
  }
  if (strcmp(direction, "forward") != 0 && strcmp(direction, "reverse") != 0) {
    motorStop();
    publishStatus("rejected", "direction_must_be_forward_or_reverse");
    return;
  }

  if (speedPct > MAX_MOTOR_SPEED_PCT) speedPct = MAX_MOTOR_SPEED_PCT;
  motorStart(strcmp(direction, "forward") == 0, (uint8_t)speedPct);
  lastDriveCommandMs = millis();
  publishStatus("running", "command_accepted");
  Serial.printf("Motor command accepted: %s at %u%% PWM (session %s)\n",
                commandedDirectionLabel(), appliedSpeedPct, sessionId);
}

// -----------------------------------------------------------------------------
// 8) NETWORK SERVICES
// -----------------------------------------------------------------------------
bool wifiConfigurationReady() {
  return WIFI_SSID[0] != '\0' && strcmp(WIFI_SSID, "CHANGE_ME") != 0;
}

bool mqttConfigurationReady() {
  return MQTT_HOST[0] != '\0' && strcmp(MQTT_HOST, "CHANGE_ME") != 0;
}

void serviceWiFi(uint32_t now) {
  if (WiFi.status() == WL_CONNECTED) return;

  if (motorRunning) motorStop();
  if (mqttClient.connected()) mqttClient.disconnect();
  if (!wifiConfigurationReady()) return;

  if (!wifiAttempted || (uint32_t)(now - lastWifiAttemptMs) >= WIFI_RETRY_INTERVAL_MS) {
    wifiAttempted = true;
    lastWifiAttemptMs = now;
    Serial.printf("Connecting to Wi-Fi SSID: %s\n", WIFI_SSID);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  }
}

bool connectMqtt() {
  motorStop();
  createRandomToken(sessionId); // Rotate before subscribing so stale retained drive commands are rejected.

  const uint64_t mac = ESP.getEfuseMac();
  char suffix[9];
  snprintf(suffix, sizeof(suffix), "%08lX", (unsigned long)(mac & 0xFFFFFFFFULL));
  mqttClientId = String("aegisot-") + AEGISOT_DEVICE_ID + "-" + suffix;

  const char* willPayload = "{\"device_id\":\"" AEGISOT_DEVICE_ID "\",\"status\":\"offline\"}";
  Serial.printf("Connecting to MQTT broker %s:%u ...\n", MQTT_HOST, MQTT_PORT);

  bool connected = false;
  if (MQTT_USERNAME[0] != '\0') {
    connected = mqttClient.connect(mqttClientId.c_str(), MQTT_USERNAME, MQTT_PASSWORD,
                                   TOPIC_STATUS, 1, true, willPayload);
  } else {
    connected = mqttClient.connect(mqttClientId.c_str(), TOPIC_STATUS, 1, true, willPayload);
  }

  if (!connected) {
    motorStop();
    Serial.printf("MQTT connect failed, state=%d\n", mqttClient.state());
    return false;
  }

  Serial.println("MQTT connected");
  publishStatus("online", "mqtt_connected"); // publishes this session_id before subscription processing
  if (!mqttClient.subscribe(TOPIC_COMMANDS, 1)) {
    motorStop();
    Serial.println("Failed to subscribe to command topic; motor remains stopped");
    publishStatus("offline", "command_subscription_failed");
    mqttClient.disconnect();
    return false;
  }
  publishStatus("stopped", "command_topic_subscribed");
  return true;
}

void serviceMqtt(uint32_t now) {
  if (WiFi.status() != WL_CONNECTED || !mqttConfigurationReady()) {
    if (motorRunning) motorStop();
    return;
  }

  if (!mqttClient.connected()) {
    if (motorRunning) motorStop();
    if (!mqttAttempted || (uint32_t)(now - lastMqttAttemptMs) >= MQTT_RETRY_INTERVAL_MS) {
      mqttAttempted = true;
      lastMqttAttemptMs = now;
      connectMqtt();
    }
    return;
  }

  mqttClient.loop();
}

void serviceSafety(uint32_t now) {
  if (motorRunning && (WiFi.status() != WL_CONNECTED || !mqttClient.connected())) {
    motorStop();
    return;
  }
  if (motorRunning && (uint32_t)(now - lastDriveCommandMs) > COMMAND_LEASE_TIMEOUT_MS) {
    motorStop();
    publishStatus("stopped", "command_lease_expired");
    Serial.println("Motor stopped: command lease expired (no fresh command received)");
  }
}

// -----------------------------------------------------------------------------
// 9) SETUP / LOOP
// -----------------------------------------------------------------------------
void setup() {
  // Put TB6612 into standby as early as possible. Add a physical ~10k pulldown on STBY.
  pinMode(PIN_MOTOR_STBY, OUTPUT);
  digitalWrite(PIN_MOTOR_STBY, LOW);
  pinMode(PIN_MOTOR_AIN1, OUTPUT);
  pinMode(PIN_MOTOR_AIN2, OUTPUT);
  digitalWrite(PIN_MOTOR_AIN1, LOW);
  digitalWrite(PIN_MOTOR_AIN2, LOW);
  pinMode(PIN_MOTOR_PWMA, OUTPUT);
  digitalWrite(PIN_MOTOR_PWMA, LOW);
  setupMotorPwm();
  motorStop();

  Serial.begin(115200);
  delay(250);
  Serial.println();
  Serial.println("====================================");
  Serial.println("AEGISOT Firmware 0.1.0 - ESP32-WROOM");
  Serial.println("Motor safe state: STOP / STBY LOW");
  Serial.println("====================================");
  createRandomToken(bootId);

  // Encoder pins use internal pull-ups as a starting point. Verify output type and voltage.
  pinMode(PIN_ENCODER_A, INPUT_PULLUP);
  pinMode(PIN_ENCODER_B, INPUT_PULLUP);
  encoderPreviousAB = (uint8_t)((digitalRead(PIN_ENCODER_A) << 1) | digitalRead(PIN_ENCODER_B));
  attachInterrupt(digitalPinToInterrupt(PIN_ENCODER_A), onEncoderChange, CHANGE);
  attachInterrupt(digitalPinToInterrupt(PIN_ENCODER_B), onEncoderChange, CHANGE);

  temperatureSensor.begin();
  temperatureSensor.setResolution(10); // lower resolution = shorter conversion time
  temperatureSensor.setWaitForConversion(false);
  startTemperatureConversion(millis());

  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  tryInitializeI2CDevices(millis());

  mqttClient.setServer(MQTT_HOST, MQTT_PORT);
  mqttClient.setCallback(handleMqttCommand);
  mqttClient.setBufferSize(1024);
  mqttClient.setKeepAlive(15);
  mqttClient.setSocketTimeout(2);

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  if (!wifiConfigurationReady()) {
    Serial.println("CONFIG REQUIRED: edit WIFI_SSID and WIFI_PASSWORD in the sketch.");
  }
  if (!mqttConfigurationReady()) {
    Serial.println("CONFIG REQUIRED: edit MQTT_HOST (and credentials if required) in the sketch.");
  }
  Serial.printf("Topics:\n  telemetry: %s\n  status:    %s\n  commands:  %s\n",
                TOPIC_TELEMETRY, TOPIC_STATUS, TOPIC_COMMANDS);
  Serial.println("Motor commands are disabled until MQTT connects and a valid session_id is supplied.");
}

void loop() {
  const uint32_t now = millis();

  serviceWiFi(now);
  serviceMqtt(now);
  serviceTemperature(now);
  serviceMpuSampling(now);
  servicePowerSampling(now);

  if (!mpuOk || !inaOk) {
    if ((uint32_t)(now - lastI2CRetryMs) >= I2C_RETRY_INTERVAL_MS) {
      tryInitializeI2CDevices(now);
    }
  }

  serviceSafety(now);

  if ((uint32_t)(now - lastTelemetryMs) >= TELEMETRY_INTERVAL_MS) {
    lastTelemetryMs = now;
    publishTelemetry(now);
  }
}
