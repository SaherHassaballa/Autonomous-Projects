#include "I2Cdev.h"
#include <PID_v1.h>
#include "MPU6050_6Axis_MotionApps20.h"
#include <Wire.h>

// Motor pins for L298N
const int ENA = 5;
const int IN1 = 6;
const int IN2 = 4;
const int ENB = 10;
const int IN3 = 9;
const int IN4 = 11;

// MPU interrupt pin
const int INT_PIN = 2;

// Min PWM calibration
const int minPWM_A = 88;
const int minPWM_B = 88 ;

// MPU Variables
MPU6050 mpu;
bool dmpReady = false;
uint8_t fifoBuffer[64];
Quaternion q;
VectorFloat gravity;
float ypr[3];
volatile bool mpuInterrupt = false;

// PID
double input, output;
double setpoint = 180;
double Kp = 80.0, Ki = 25.0, Kd = 2.0;
PID pid(&input, &output, &setpoint, Kp, Ki, Kd, DIRECT);

double fallLimit = 25; 

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

  stopMotors();

  mpu.initialize();
  uint8_t devStatus = mpu.dmpInitialize();

  mpu.setXGyroOffset(26);
  mpu.setYGyroOffset(59);
  mpu.setZGyroOffset(15);
  mpu.setZAccelOffset(964);

  if (devStatus == 0) {
    mpu.setDMPEnabled(true);
    attachInterrupt(digitalPinToInterrupt(INT_PIN), dmpDataReady, RISING);

    pid.SetMode(AUTOMATIC);
    pid.SetSampleTime(10);
    pid.SetOutputLimits(-255, 255);

    dmpReady = true;
    Serial.println("DMP Ready!");
  } 
  else Serial.println("MPU ERROR!");
}

void loop() {
  if (!dmpReady || !mpuInterrupt) return;

  mpuInterrupt = false;

  mpu.getFIFOBytes(fifoBuffer, mpu.dmpGetFIFOPacketSize());
  mpu.dmpGetQuaternion(&q, fifoBuffer);
  mpu.dmpGetGravity(&gravity, &q);
  mpu.dmpGetYawPitchRoll(ypr, &q, &gravity);

  input = ypr[1] * 180 / M_PI + 180;

  pid.Compute();
  int speed = abs(output);

  if (abs(input - setpoint) < fallLimit) {
    if (output > 0) {
      speed = constrain(speed + minPWM_A, minPWM_A, 255);
      moveForward(speed);
    } else if (output < 0) {
      speed = constrain(speed + minPWM_B, minPWM_B, 255);
      moveBackward(speed);
    } else stopMotors();
  }
  else stopMotors();

  // 🔥 Send Data to MATLAB
  Serial.print(input);
  Serial.print(",");
  Serial.print(output);
  Serial.print(",");
  Serial.println(speed);
}

void moveForward(int p) {
  analogWrite(ENA, p);
  digitalWrite(IN1, HIGH);
  digitalWrite(IN2, LOW);

  analogWrite(ENB, p);
  digitalWrite(IN3, LOW);
  digitalWrite(IN4, HIGH);
}

void moveBackward(int p) {
  analogWrite(ENA, p);
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, HIGH);

  analogWrite(ENB, p);
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
