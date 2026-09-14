#include "I2Cdev.h"
#include <PID_v1.h>
#include "MPU6050_6Axis_MotionApps20.h"
#include <Wire.h>

// --------- PIN CONFIG ---------
// L298N motor pins (Leonardo)
const int ENA = 5;   // Right motor (A) PWM
const int IN1 = 6;
const int IN2 = 4;

const int ENB = 9;   // Left motor (B) PWM
const int IN3 = 10;
const int IN4 = 11;

// MPU6050 interrupt pin
const int INT_PIN = 2;

// --------- MOTOR MIN PWM (Calibration Results) ---------
const int minPWM_A_fwd = 95;
const int minPWM_A_bwd = 96;
const int minPWM_B_fwd = 92;
const int minPWM_B_bwd = 105;

// --------- MPU6050 & DMP ---------
MPU6050 mpu;
bool dmpReady = false;
uint8_t mpuIntStatus;
uint16_t packetSize;
uint16_t fifoCount;
uint8_t fifoBuffer[64];

Quaternion q;
VectorFloat gravity;
float ypr[3];
volatile bool mpuInterrupt = false;

// --------- PID ---------
double input, output;
double setpoint = 180.0;
double Kp = 35.0, Ki = 0.0
, Kd = 1.5;
PID pid(&input, &output, &setpoint, Kp, Ki, Kd, DIRECT);
double fallLimit = 25.0;

void dmpDataReady() { mpuInterrupt = true; }

void setup() {
  Serial.begin(115200);
  Wire.begin();
  
  pinMode(ENA, OUTPUT);
  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);
  pinMode(ENB, OUTPUT);
  pinMode(IN3, OUTPUT);
  pinMode(IN4, OUTPUT);

  // MPU Init
  mpu.initialize();
  uint8_t devStatus = mpu.dmpInitialize();

  mpu.setXGyroOffset(26);
  mpu.setYGyroOffset(59);
  mpu.setZGyroOffset(15);
  mpu.setZAccelOffset(946);

  if (devStatus == 0) {
    mpu.setDMPEnabled(true);
    attachInterrupt(digitalPinToInterrupt(INT_PIN), dmpDataReady, RISING);
    packetSize = mpu.dmpGetFIFOPacketSize();
    dmpReady = true;

    pid.SetMode(AUTOMATIC);
    pid.SetSampleTime(10);
    pid.SetOutputLimits(-255, 255);

    Serial.println("DMP Ready! Start balancing...");
  } else {
    Serial.println("MPU Error!");
  }
}

void loop() {
  if (!dmpReady) return;
  if (!mpuInterrupt && fifoCount < packetSize) return;

  mpuInterrupt = false;
  mpuIntStatus = mpu.getIntStatus();
  fifoCount = mpu.getFIFOCount();

  if ((mpuIntStatus & 0x10) || fifoCount == 1024) {
    mpu.resetFIFO();
    fifoCount = 0;
    return;
  }

  if (mpuIntStatus & 0x02) {
    while (fifoCount < packetSize) fifoCount = mpu.getFIFOCount();
    mpu.getFIFOBytes(fifoBuffer, packetSize);
    fifoCount -= packetSize;

    mpu.dmpGetQuaternion(&q, fifoBuffer);
    mpu.dmpGetGravity(&gravity, &q);
    mpu.dmpGetYawPitchRoll(ypr, &q, &gravity);

    input = ypr[1] * 180.0 / M_PI + 180.0;
    pid.Compute();

    int baseSpeed = abs(output);

    if (abs(input - setpoint) < fallLimit) {
      if (output > 0) moveForward(baseSpeed);
      else if (output < 0) moveBackward(baseSpeed);
      else stopMotors();
    } else {
      stopMotors();
    }

    // === SEND DATA TO MATLAB ===
    Serial.print(input);   // Angle
    Serial.print(",");
    Serial.print(output);  // PID Output
    Serial.print(",");
    Serial.println(baseSpeed); // Motor Speed
  }
}

// --------- MOTOR CONTROL ---------
void moveForward(int p) {
  int pA = constrain(p + minPWM_A_fwd, minPWM_A_fwd, 255);
  int pB = constrain(p + minPWM_B_fwd, minPWM_B_fwd, 255);

  analogWrite(ENA, pA);
  digitalWrite(IN1, HIGH);
  digitalWrite(IN2, LOW);

  analogWrite(ENB, pB);
  digitalWrite(IN3, LOW);
  digitalWrite(IN4, HIGH);
}

void moveBackward(int p) {
  int pA = constrain(p + minPWM_A_bwd, minPWM_A_bwd, 255);
  int pB = constrain(p + minPWM_B_bwd, minPWM_B_bwd, 255);

  analogWrite(ENA, pA);
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, HIGH);

  analogWrite(ENB, pB);
  digitalWrite(IN3, HIGH);
  digitalWrite(IN4, LOW);
}

void stopMotors() {
  analogWrite(ENA, 0);
  analogWrite(ENB, 0);
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, LOW);
  digitalWrite(IN3, LOW);
  digitalWrite(IN4, LOW);
}