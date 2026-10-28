/*
  AEGISOT Sensor + Motor Bench Test
  Board: ESP32 DevKit V1 / ESP32-WROOM-32
  Motor driver: Cytron MD10C Rev 3.0, Sign-Magnitude mode (PWM + DIR)

  Functions:
    - Tests INA219, MPU6050, DS18B20 and quadrature encoder.
    - Keyboard UP increases motor PWM by 5%; DOWN decreases by 5%.
    - '+'/'-' are fallbacks for Arduino Serial Monitor.
    - 's' or SPACE stops motor immediately.
    - 'f' selects forward, 'r' selects reverse (direction changes stop first).
    - '0' zeros the encoder counter while stopped.
    - 'h' prints help.

  IMPORTANT:
    This is a LOCAL SERIAL BENCH TEST. It does not use Wi-Fi/MQTT.
    Verify motor rated voltage and supply before use. Start at low PWM.
    Software current monitoring is supplementary, not a hardware fuse.
    Encoder signal pins must be 3.3V-safe for ESP32 GPIOs.
*/

#include <Arduino.h>
#include <Wire.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_INA219.h>
#include <esp_arduino_version.h>
#include <driver/gpio.h>
#include <math.h>

// ---------------- Pin configuration ----------------
static constexpr uint8_t PIN_MOTOR_PWM = 25;
static constexpr uint8_t PIN_MOTOR_DIR = 26;
static constexpr uint8_t PIN_ENCODER_A = 32;
static constexpr uint8_t PIN_ENCODER_B = 33;
static constexpr uint8_t PIN_I2C_SDA = 21;
static constexpr uint8_t PIN_I2C_SCL = 22;
static constexpr uint8_t PIN_DS18B20 = 4;

// Cytron MD10C Sign-Magnitude mode: PWM sets speed; DIR sets direction.
// DIR LOW/HIGH mapping follows the MD10C truth table; physical CW/CCW depends on motor wiring.
static constexpr uint8_t DIR_FORWARD_LEVEL = LOW;
static constexpr uint8_t DIR_REVERSE_LEVEL = HIGH;

static constexpr uint32_t PWM_FREQUENCY_HZ = 1000; // same as earlier successful bench test
static constexpr uint8_t PWM_RESOLUTION_BITS = 8;
static constexpr uint8_t PWM_CHANNEL_LEGACY = 0;
static constexpr uint8_t SPEED_STEP_PERCENT = 5;
static constexpr uint8_t MAX_SPEED_PERCENT = 100;

// Current protection: configurable conservative software trip.
// Verify actual motor stall/rated current and INA219 shunt/calibration before long tests.
static constexpr float CURRENT_LIMIT_MA = 1200.0f;
static constexpr uint8_t OVERCURRENT_SAMPLES_TO_TRIP = 3; // 3 consecutive samples, nominally 150 ms
static constexpr uint32_t POWER_SAMPLE_INTERVAL_MS = 50;
static constexpr uint32_t MPU_SAMPLE_INTERVAL_MS = 100;
static constexpr uint32_t TELEMETRY_INTERVAL_MS = 1000;
// Data collection: 5 Hz, independent from the slower human-readable telemetry.
static constexpr uint32_t CSV_SAMPLE_INTERVAL_MS = 200;
static constexpr uint8_t CSV_TARGET_HZ = 5;
static constexpr uint32_t CSV_TARGET_DURATION_MS = 5UL * 60UL * 1000UL;
static constexpr uint32_t CSV_TARGET_READINGS = 1500;
static constexpr uint32_t TEMPERATURE_INTERVAL_MS = 1000;
static constexpr uint32_t DS18B20_CONVERSION_MS = 200; // 10-bit resolution

// Leave zero until the exact x4 counts per output-shaft revolution is confirmed.
static constexpr int32_t ENCODER_COUNTS_PER_OUTPUT_REV = 0;

// ---------------- Sensors and state ----------------
OneWire oneWire(PIN_DS18B20);
DallasTemperature temperatureSensor(&oneWire);
Adafruit_MPU6050 mpu;
Adafruit_INA219 ina219;

bool mpuOk = false;
bool inaOk = false;
bool ds18b20Ok = false;
bool mpuDataValid = false;
bool powerDataValid = false;
bool temperatureValid = false;
bool temperatureConversionPending = false;
bool motorPwmReady = false;
bool motorRunning = false;
bool motorDirectionForward = true;
bool overcurrentFault = false;

// CSV logging is OFF by default and only starts after an explicit operator command.
bool csvLoggingActive = false;
String csvCondition = "none";
uint32_t csvLogStartedMs = 0;
uint32_t lastCsvSampleMs = 0;
uint32_t csvSampleCount = 0;
bool csvTargetReachedReported = false;
int32_t lastCsvEncoderCount = 0;
uint32_t lastCsvEncoderMs = 0;
const char* lastMotorStopReason = "boot";

uint8_t speedPercent = 0;
uint8_t duty8Bit = 0;
uint8_t overcurrentSamples = 0;

float temperatureC = NAN;
float accelX = NAN, accelY = NAN, accelZ = NAN;
float gyroX = NAN, gyroY = NAN, gyroZ = NAN;
float busVoltageV = NAN, shuntVoltageMv = NAN, currentMa = NAN, powerMw = NAN;

uint32_t lastPowerSampleMs = 0;
uint32_t lastMpuSampleMs = 0;
uint32_t lastTelemetryMs = 0;
uint32_t lastTemperatureRequestMs = 0;
uint32_t temperatureRequestStartedMs = 0;

portMUX_TYPE encoderMux = portMUX_INITIALIZER_UNLOCKED;
volatile int32_t encoderCount = 0;
volatile uint8_t encoderPreviousAB = 0;
int32_t lastTelemetryEncoderCount = 0;
uint32_t lastEncoderTelemetryMs = 0;
float encoderCountsPerSecond = 0.0f;

// Escape sequence parser for terminals that transmit ANSI arrow codes.
uint8_t arrowParserState = 0;
uint32_t arrowParserLastMs = 0;

// ---------------- PWM compatibility (Arduino-ESP32 2.x and 3.x) ----------------
void writeMotorPwm(uint8_t duty) {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWrite(PIN_MOTOR_PWM, duty); // Core 3.x uses pin, not channel
#else
  ledcWrite(PWM_CHANNEL_LEGACY, duty);
#endif
}

bool setupMotorPwm() {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  return ledcAttach(PIN_MOTOR_PWM, PWM_FREQUENCY_HZ, PWM_RESOLUTION_BITS);
#else
  ledcSetup(PWM_CHANNEL_LEGACY, PWM_FREQUENCY_HZ, PWM_RESOLUTION_BITS);
  ledcAttachPin(PIN_MOTOR_PWM, PWM_CHANNEL_LEGACY);
  return true;
#endif
}

uint8_t percentToDuty(uint8_t percent) {
  if (percent > 100) percent = 100;
  return (uint8_t)(((uint16_t)percent * 255U + 50U) / 100U);
}

void stopMotor(const char* reason) {
  lastMotorStopReason = reason;
  writeMotorPwm(0);
  duty8Bit = 0;
  speedPercent = 0;
  motorRunning = false;
  Serial.printf("MOTOR STOP | reason=%s\n", reason);
}

bool setMotorSpeedPercent(uint8_t percent) {
  if (!motorPwmReady) {
    stopMotor("pwm_not_ready");
    Serial.println("ERROR: motor command refused; PWM initialization failed.");
    return false;
  }
  if (!inaOk || !powerDataValid) {
    stopMotor("ina219_unavailable");
    Serial.println("ERROR: motor command refused; INA219 current measurement is unavailable.");
    return false;
  }
  if (overcurrentFault) {
    stopMotor("overcurrent_fault_latched");
    Serial.println("ERROR: current fault is latched. Resolve the cause and send 'c' to clear it.");
    return false;
  }
  if (percent > MAX_SPEED_PERCENT) percent = MAX_SPEED_PERCENT;
  if (percent == 0) {
    stopMotor("speed_zero");
    return true;
  }

  digitalWrite(PIN_MOTOR_DIR, motorDirectionForward ? DIR_FORWARD_LEVEL : DIR_REVERSE_LEVEL);
  const uint8_t duty = percentToDuty(percent);
  writeMotorPwm(duty);
  speedPercent = percent;
  duty8Bit = duty;
  motorRunning = true;

  Serial.printf("MOTOR COMMAND | direction=%s | speed=%u%% | duty=%u/255 | PWM=%lu Hz\n",
                motorDirectionForward ? "forward" : "reverse",
                speedPercent, duty8Bit, (unsigned long)PWM_FREQUENCY_HZ);
  return true;
}

// ---------------- Quadrature encoder (x4) ----------------
void IRAM_ATTR onEncoderChange() {
  portENTER_CRITICAL_ISR(&encoderMux);
  const uint8_t a = (uint8_t)gpio_get_level((gpio_num_t)PIN_ENCODER_A);
  const uint8_t b = (uint8_t)gpio_get_level((gpio_num_t)PIN_ENCODER_B);
  const uint8_t currentAB = (uint8_t)((a << 1) | b);
  const uint8_t transition = (uint8_t)((encoderPreviousAB << 2) | currentAB);
  int8_t delta = 0;
  switch (transition) {
    case 0b0001: case 0b0111: case 0b1110: case 0b1000: delta = 1; break;
    case 0b0010: case 0b1011: case 0b1101: case 0b0100: delta = -1; break;
    default: break;
  }
  encoderCount += delta;
  encoderPreviousAB = currentAB;
  portEXIT_CRITICAL_ISR(&encoderMux);
}

int32_t getEncoderCountSnapshot() {
  int32_t result;
  portENTER_CRITICAL(&encoderMux);
  result = encoderCount;
  portEXIT_CRITICAL(&encoderMux);
  return result;
}

void zeroEncoderIfStopped() {
  if (motorRunning) {
    Serial.println("Encoder zero refused: stop the motor first.");
    return;
  }
  portENTER_CRITICAL(&encoderMux);
  encoderCount = 0;
  encoderPreviousAB = (uint8_t)((digitalRead(PIN_ENCODER_A) << 1) | digitalRead(PIN_ENCODER_B));
  portEXIT_CRITICAL(&encoderMux);
  lastTelemetryEncoderCount = 0;
  lastEncoderTelemetryMs = millis();
  Serial.println("Encoder counter zeroed.");
}

// ---------------- Explicit CSV data collection ----------------
void startCsvLogging(const char* condition) {
  if (csvLoggingActive) {
    Serial.printf("LOG_REJECTED,%s,already_active\n", condition);
    return;
  }
  csvCondition = condition;
  csvLoggingActive = true;
  csvLogStartedMs = millis();
  lastCsvSampleMs = csvLogStartedMs;
  csvSampleCount = 0;
  csvTargetReachedReported = false;
  lastCsvEncoderCount = getEncoderCountSnapshot();
  lastCsvEncoderMs = csvLogStartedMs;
  Serial.printf("LOG_STARTED,%s,%u,%lu,%lu\n", csvCondition.c_str(),
                (unsigned)CSV_TARGET_HZ, (unsigned long)CSV_TARGET_READINGS,
                (unsigned long)CSV_SAMPLE_INTERVAL_MS);
  Serial.println("CSV logging is active; it will continue until the operator sends X.");
}

void stopCsvLogging(const char* reason) {
  if (!csvLoggingActive) {
    Serial.println("LOG_NOT_ACTIVE");
    return;
  }
  const String finishedCondition = csvCondition;
  const uint32_t elapsed = (uint32_t)(millis() - csvLogStartedMs);
  const uint32_t samples = csvSampleCount;
  csvLoggingActive = false;
  csvCondition = "none";
  Serial.printf("LOG_STOPPED,%s,%lu,%lu,%s\n", finishedCondition.c_str(),
                (unsigned long)elapsed, (unsigned long)samples, reason);
  Serial.println("CSV logging stopped. Motor state is unchanged; use SPACE/s to stop the motor.");
}

void printCsvFloat(float value, uint8_t decimals = 4) {
  if (isfinite(value)) Serial.print(value, decimals);
  else Serial.print("nan");
}

void serviceCsvLogging(uint32_t now) {
  if (!csvLoggingActive || (uint32_t)(now - lastCsvSampleMs) < CSV_SAMPLE_INTERVAL_MS) return;

  // One actual sample per service interval; do not fabricate catch-up samples after a delay.
  lastCsvSampleMs = now;
  const int32_t countNow = getEncoderCountSnapshot();
  const uint32_t encoderElapsed = (uint32_t)(now - lastCsvEncoderMs);
  const int32_t encoderDelta = countNow - lastCsvEncoderCount;
  const float encoderCps = encoderElapsed > 0
      ? (float)encoderDelta * 1000.0f / (float)encoderElapsed : NAN;
  const bool rpmValid = ENCODER_COUNTS_PER_OUTPUT_REV > 0 && isfinite(encoderCps);
  const float rpm = rpmValid
      ? encoderCps * 60.0f / (float)ENCODER_COUNTS_PER_OUTPUT_REV : NAN;

  // Machine-readable line parsed by keyboard_motor_test.py. No CSV is emitted when logging is off.
  Serial.print("CSV_DATA,");
  Serial.print((unsigned long)now); Serial.print(',');
  Serial.print((unsigned long)(now - csvLogStartedMs)); Serial.print(',');
  Serial.print(csvCondition); Serial.print(',');
  Serial.print(motorRunning ? 1 : 0); Serial.print(',');
  Serial.print(motorDirectionForward ? "forward" : "reverse"); Serial.print(',');
  Serial.print(speedPercent); Serial.print(',');
  Serial.print(duty8Bit); Serial.print(',');
  Serial.print(inaOk ? 1 : 0); Serial.print(',');
  Serial.print(powerDataValid ? 1 : 0); Serial.print(',');
  printCsvFloat(powerDataValid ? busVoltageV : NAN); Serial.print(',');
  printCsvFloat(powerDataValid ? shuntVoltageMv : NAN); Serial.print(',');
  printCsvFloat(powerDataValid ? currentMa : NAN, 3); Serial.print(',');
  printCsvFloat(powerDataValid ? powerMw : NAN, 3); Serial.print(',');
  Serial.print(ds18b20Ok ? 1 : 0); Serial.print(',');
  Serial.print(temperatureValid ? 1 : 0); Serial.print(',');
  printCsvFloat(temperatureValid ? temperatureC : NAN, 2); Serial.print(',');
  Serial.print(mpuOk ? 1 : 0); Serial.print(',');
  Serial.print(mpuDataValid ? 1 : 0); Serial.print(',');
  printCsvFloat(mpuDataValid ? accelX : NAN); Serial.print(',');
  printCsvFloat(mpuDataValid ? accelY : NAN); Serial.print(',');
  printCsvFloat(mpuDataValid ? accelZ : NAN); Serial.print(',');
  printCsvFloat(mpuDataValid ? gyroX : NAN); Serial.print(',');
  printCsvFloat(mpuDataValid ? gyroY : NAN); Serial.print(',');
  printCsvFloat(mpuDataValid ? gyroZ : NAN); Serial.print(',');
  Serial.print((long)countNow); Serial.print(',');
  Serial.print((long)encoderDelta); Serial.print(',');
  printCsvFloat(encoderCps, 2); Serial.print(',');
  Serial.print((long)ENCODER_COUNTS_PER_OUTPUT_REV); Serial.print(',');
  Serial.print(rpmValid ? 1 : 0); Serial.print(',');
  printCsvFloat(rpm, 2); Serial.print(',');
  Serial.print(overcurrentFault ? 1 : 0); Serial.print(',');
  Serial.println(lastMotorStopReason);

  lastCsvEncoderCount = countNow;
  lastCsvEncoderMs = now;
  ++csvSampleCount;
  if (!csvTargetReachedReported && csvSampleCount >= CSV_TARGET_READINGS) {
    csvTargetReachedReported = true;
    Serial.printf("LOG_TARGET_REACHED,%s,%lu,%lu\n", csvCondition.c_str(),
                  (unsigned long)csvSampleCount,
                  (unsigned long)(now - csvLogStartedMs));
  }
}

// ---------------- Sensor service routines ----------------
void configureMpu() {
  mpu.setAccelerometerRange(MPU6050_RANGE_4_G);
  mpu.setGyroRange(MPU6050_RANGE_500_DEG);
  mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);
}

void requestTemperatureConversion(uint32_t now) {
  if (!ds18b20Ok) return;
  temperatureSensor.requestTemperatures();
  temperatureRequestStartedMs = now;
  lastTemperatureRequestMs = now;
  temperatureConversionPending = true;
}

void serviceTemperature(uint32_t now) {
  if (temperatureConversionPending && (uint32_t)(now - temperatureRequestStartedMs) >= DS18B20_CONVERSION_MS) {
    const float value = temperatureSensor.getTempCByIndex(0);
    temperatureValid = isfinite(value) && value != DEVICE_DISCONNECTED_C && value >= -55.0f && value <= 125.0f;
    temperatureC = temperatureValid ? value : NAN;
    temperatureConversionPending = false;
    if (!temperatureValid) Serial.println("DS18B20 read invalid/disconnected.");
  }
  if (!temperatureConversionPending && (uint32_t)(now - lastTemperatureRequestMs) >= TEMPERATURE_INTERVAL_MS) {
    requestTemperatureConversion(now);
  }
}

void serviceMpu(uint32_t now) {
  if ((uint32_t)(now - lastMpuSampleMs) < MPU_SAMPLE_INTERVAL_MS) return;
  lastMpuSampleMs = now;
  if (!mpuOk) {
    mpuDataValid = false;
    return;
  }
  sensors_event_t accel, gyro, temp;
  mpuDataValid = mpu.getEvent(&accel, &gyro, &temp);
  if (mpuDataValid) {
    accelX = accel.acceleration.x; accelY = accel.acceleration.y; accelZ = accel.acceleration.z;
    gyroX = gyro.gyro.x; gyroY = gyro.gyro.y; gyroZ = gyro.gyro.z;
  }
}

void servicePower(uint32_t now) {
  if ((uint32_t)(now - lastPowerSampleMs) < POWER_SAMPLE_INTERVAL_MS) return;
  lastPowerSampleMs = now;
  if (!inaOk) {
    powerDataValid = false;
    return;
  }

  const float v = ina219.getBusVoltage_V();
  const float sv = ina219.getShuntVoltage_mV();
  const float c = ina219.getCurrent_mA();
  const float p = ina219.getPower_mW();
  powerDataValid = isfinite(v) && isfinite(sv) && isfinite(c) && isfinite(p) && v >= 0.0f;
  if (!powerDataValid) {
    if (motorRunning) stopMotor("ina219_read_invalid");
    return;
  }
  busVoltageV = v; shuntVoltageMv = sv; currentMa = c; powerMw = p;

  if (motorRunning && fabsf(currentMa) > CURRENT_LIMIT_MA) {
    if (overcurrentSamples < 255) ++overcurrentSamples;
    if (overcurrentSamples >= OVERCURRENT_SAMPLES_TO_TRIP) {
      overcurrentFault = true;
      stopMotor("overcurrent_trip");
      Serial.printf("SAFETY TRIP | current=%.1f mA | limit=%.1f mA | samples=%u\n",
                    fabsf(currentMa), CURRENT_LIMIT_MA, overcurrentSamples);
    }
  } else {
    overcurrentSamples = 0;
  }
}

void printHelp() {
  Serial.println();
  Serial.println("=== AEGISOT SENSOR + MOTOR TEST KEYS ===");
  Serial.println("UP arrow / 'u' / '+' : increase speed by 5% (max 100%)");
  Serial.println("DOWN arrow / 'd' / '-': decrease speed by 5% (0% = stop)");
  Serial.println("'s' or SPACE          : STOP motor immediately");
  Serial.println("'f'                   : select forward; motor stops before direction change");
  Serial.println("'r'                   : select reverse; motor stops before direction change");
  Serial.println("'0'                   : zero encoder count (only while stopped)");
  Serial.println("'c'                   : clear overcurrent fault if measured current is safe");
  Serial.println("'h' or '?'            : show this help");
  Serial.println("CSV data collection (only starts after Python confirmation):");
  Serial.println("Python key n: normal | o: overheated | v: vibration_issue | p: rpm_issue");
  Serial.println("Python key x: stop CSV logging; logging is otherwise OFF");
  Serial.printf("CSV target: %u Hz, %lu minutes, %lu readings per stage\n", (unsigned)CSV_TARGET_HZ,
                (unsigned long)(CSV_TARGET_DURATION_MS / 60000UL), (unsigned long)CSV_TARGET_READINGS);
  Serial.println("CSV records include sensor-valid flags; unavailable values are written as nan.");
  Serial.println("Note: Arduino IDE Serial Monitor may not transmit arrow keys; use the bundled Python terminal for CSV files.");
  Serial.println("========================================");
}

void increaseSpeed() {
  if (overcurrentFault) {
    Serial.println("Speed increase refused: overcurrent fault latched; resolve and press 'c'.");
    return;
  }
  uint16_t next = speedPercent + SPEED_STEP_PERCENT;
  if (next > MAX_SPEED_PERCENT) next = MAX_SPEED_PERCENT;
  setMotorSpeedPercent((uint8_t)next);
}

void decreaseSpeed() {
  if (!motorRunning || speedPercent == 0) {
    Serial.println("Motor already stopped (0%).");
    return;
  }
  int next = (int)speedPercent - SPEED_STEP_PERCENT;
  if (next < 0) next = 0;
  setMotorSpeedPercent((uint8_t)next);
}

void changeDirection(bool forward) {
  stopMotor("direction_change_requires_stop");
  motorDirectionForward = forward;
  digitalWrite(PIN_MOTOR_DIR, motorDirectionForward ? DIR_FORWARD_LEVEL : DIR_REVERSE_LEVEL);
  Serial.printf("Direction selected: %s. Speed reset to 0%%; press UP to start.\n",
                motorDirectionForward ? "forward" : "reverse");
}

void clearCurrentFault() {
  if (!inaOk || !powerDataValid) {
    Serial.println("Cannot clear fault: INA219 current reading unavailable.");
    return;
  }
  if (fabsf(currentMa) >= CURRENT_LIMIT_MA * 0.5f) {
    Serial.printf("Cannot clear fault: current %.1f mA is not yet low enough.\n", fabsf(currentMa));
    return;
  }
  overcurrentFault = false;
  overcurrentSamples = 0;
  Serial.println("Overcurrent fault cleared. Start again manually with UP.");
}

void handleKey(char c) {
  if (c == 0) return;
  switch (c) {
    case 'u': case 'U': case '+': increaseSpeed(); break;
    case 'd': case 'D': case '-': decreaseSpeed(); break;
    case 's': case 'S': case ' ': stopMotor("keyboard_stop"); break;
    case 'f': case 'F': changeDirection(true); break;
    case 'r': case 'R': changeDirection(false); break;
    case '0': zeroEncoderIfStopped(); break;
    case 'c': case 'C': clearCurrentFault(); break;
    case 'N': startCsvLogging("normal"); break;
    case 'O': startCsvLogging("overheated"); break;
    case 'V': startCsvLogging("vibration_issue"); break;
    case 'P': startCsvLogging("rpm_issue"); break;
    case 'X': stopCsvLogging("operator_stop"); break;
    case 'h': case 'H': case '?': printHelp(); break;
    case '\r': case '\n': case '\t': break;
    default: Serial.printf("Unknown key '%c' (0x%02X). Press h for help.\n", c, (uint8_t)c); break;
  }
}

void serviceSerialKeyboard(uint32_t now) {
  if (arrowParserState != 0 && (uint32_t)(now - arrowParserLastMs) > 100) arrowParserState = 0;
  while (Serial.available() > 0) {
    char c = (char)Serial.read();
    if (arrowParserState == 0) {
      if ((uint8_t)c == 0x1B) {
        arrowParserState = 1;
        arrowParserLastMs = now;
      } else {
        handleKey(c);
      }
    } else if (arrowParserState == 1) {
      arrowParserLastMs = now;
      if (c == '[') arrowParserState = 2;
      else {
        arrowParserState = 0;
        // Ignore an unsupported Escape sequence rather than accidentally treating it as a motor command.
      }
    } else {
      arrowParserState = 0;
      if (c == 'A') increaseSpeed();       // ANSI Up arrow
      else if (c == 'B') decreaseSpeed();  // ANSI Down arrow
    }
  }
}

void printTelemetry(uint32_t now) {
  const int32_t countNow = getEncoderCountSnapshot();
  const uint32_t elapsed = (lastEncoderTelemetryMs == 0) ? TELEMETRY_INTERVAL_MS : (uint32_t)(now - lastEncoderTelemetryMs);
  const int32_t delta = countNow - lastTelemetryEncoderCount;
  encoderCountsPerSecond = elapsed > 0 ? (float)delta * 1000.0f / (float)elapsed : 0.0f;

  Serial.println("\n--- AEGISOT TEST TELEMETRY ---");
  Serial.printf("MOTOR: state=%s direction=%s speed=%u%% duty=%u/255 pwm_ready=%s freq=%lu Hz\n",
                motorRunning ? "RUN_COMMAND" : "STOPPED",
                motorDirectionForward ? "forward" : "reverse",
                speedPercent, duty8Bit, motorPwmReady ? "true" : "false", (unsigned long)PWM_FREQUENCY_HZ);
  Serial.printf("FAULTS: overcurrent=%s current_limit=%.0f mA\n", overcurrentFault ? "true" : "false", CURRENT_LIMIT_MA);

  if (inaOk && powerDataValid) {
    Serial.printf("INA219: ok=true bus=%.3f V shunt=%.3f mV current=%.1f mA power=%.1f mW\n",
                  busVoltageV, shuntVoltageMv, currentMa, powerMw);
  } else {
    Serial.printf("INA219: ok=%s data_valid=%s\n", inaOk ? "true" : "false", powerDataValid ? "true" : "false");
  }
  if (ds18b20Ok && temperatureValid) Serial.printf("DS18B20: ok=true temp=%.2f C\n", temperatureC);
  else Serial.printf("DS18B20: ok=%s data_valid=%s\n", ds18b20Ok ? "true" : "false", temperatureValid ? "true" : "false");
  if (mpuOk && mpuDataValid) {
    Serial.printf("MPU6050: ok=true accel=[%.3f, %.3f, %.3f] m/s^2 gyro=[%.4f, %.4f, %.4f] rad/s\n",
                  accelX, accelY, accelZ, gyroX, gyroY, gyroZ);
  } else {
    Serial.printf("MPU6050: ok=%s data_valid=%s\n", mpuOk ? "true" : "false", mpuDataValid ? "true" : "false");
  }
  Serial.printf("ENCODER: count=%ld delta=%ld cps=%.1f measured_direction=%s counts_per_rev=%ld rpm=",
                (long)countNow, (long)delta, encoderCountsPerSecond,
                delta > 0 ? "positive" : (delta < 0 ? "negative" : "stopped"),
                (long)ENCODER_COUNTS_PER_OUTPUT_REV);
  if (ENCODER_COUNTS_PER_OUTPUT_REV > 0) {
    const float rpm = encoderCountsPerSecond * 60.0f / (float)ENCODER_COUNTS_PER_OUTPUT_REV;
    Serial.printf("%.2f\n", rpm);
  } else {
    Serial.println("INVALID (configure counts/rev first)");
  }
  Serial.println("------------------------------");

  lastTelemetryEncoderCount = countNow;
  lastEncoderTelemetryMs = now;
}

void setup() {
  // Force motor output low immediately during boot.
  pinMode(PIN_MOTOR_PWM, OUTPUT);
  digitalWrite(PIN_MOTOR_PWM, LOW);
  pinMode(PIN_MOTOR_DIR, OUTPUT);
  digitalWrite(PIN_MOTOR_DIR, DIR_FORWARD_LEVEL);

  Serial.begin(115200);
  delay(300);
  Serial.println();
  Serial.println("============================================");
  Serial.println("AEGISOT Sensor + Motor Bench Test");
  Serial.println("Boot safety state: STOP, PWM=0");
  Serial.println("Driver: Cytron MD10C Rev 3.0 (PWM + DIR)");
  Serial.println("============================================");

  motorPwmReady = setupMotorPwm();
  writeMotorPwm(0);
  Serial.printf("PWM init: %s | PWM GPIO=%u | DIR GPIO=%u | frequency=%lu Hz | resolution=%u-bit\n",
                motorPwmReady ? "OK" : "FAILED", PIN_MOTOR_PWM, PIN_MOTOR_DIR,
                (unsigned long)PWM_FREQUENCY_HZ, PWM_RESOLUTION_BITS);
  if (!motorPwmReady) Serial.println("FATAL: motor control disabled because PWM failed to initialize.");

  pinMode(PIN_ENCODER_A, INPUT_PULLUP);
  pinMode(PIN_ENCODER_B, INPUT_PULLUP);
  encoderPreviousAB = (uint8_t)((digitalRead(PIN_ENCODER_A) << 1) | digitalRead(PIN_ENCODER_B));
  attachInterrupt(digitalPinToInterrupt(PIN_ENCODER_A), onEncoderChange, CHANGE);
  attachInterrupt(digitalPinToInterrupt(PIN_ENCODER_B), onEncoderChange, CHANGE);

  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  mpuOk = mpu.begin(0x68, &Wire);
  if (mpuOk) {
    configureMpu();
    Serial.println("MPU6050: detected at 0x68");
  } else {
    Serial.println("MPU6050: NOT FOUND (check I2C wiring/address)");
  }

  inaOk = ina219.begin(&Wire);
  if (inaOk) Serial.println("INA219: detected (Adafruit default 32V/2A calibration; verify breakout/shunt)");
  else Serial.println("INA219: NOT FOUND (motor control will be refused for safety)");

  temperatureSensor.begin();
  temperatureSensor.setResolution(10);
  temperatureSensor.setWaitForConversion(false);
  ds18b20Ok = temperatureSensor.getDeviceCount() > 0;
  if (ds18b20Ok) {
    Serial.printf("DS18B20: found %d device(s) on GPIO%u\n", temperatureSensor.getDeviceCount(), PIN_DS18B20);
    requestTemperatureConversion(millis());
  } else {
    Serial.printf("DS18B20: NOT FOUND on GPIO%u (check 4.7k pull-up and wiring)\n", PIN_DS18B20);
  }

  // Take one initial INA219 sample before accepting motor changes.
  lastPowerSampleMs = millis() - POWER_SAMPLE_INTERVAL_MS;
  servicePower(millis());
  lastMpuSampleMs = millis() - MPU_SAMPLE_INTERVAL_MS;
  serviceMpu(millis());
  printHelp();
  Serial.println("Ready. The motor starts at 0%; press UP repeatedly to increase speed.");
  Serial.println("CSV logging starts OFF and requires an explicit condition + confirmation in the Python terminal.");
  Serial.println("Begin at low speed. Use SPACE/s for immediate motor stop.");
}

void loop() {
  const uint32_t now = millis();
  serviceSerialKeyboard(now);
  servicePower(now);
  serviceMpu(now);
  serviceTemperature(now);
  serviceCsvLogging(now);

  if ((uint32_t)(now - lastTelemetryMs) >= TELEMETRY_INTERVAL_MS) {
    lastTelemetryMs = now;
    printTelemetry(now);
  }
  delay(1);
}
