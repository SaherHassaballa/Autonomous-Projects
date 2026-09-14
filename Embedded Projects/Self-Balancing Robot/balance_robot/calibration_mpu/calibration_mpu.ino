#include "I2Cdev.h"
#include "MPU6050_6Axis_MotionApps20.h"
#include <Wire.h>

MPU6050 mpu;

void setup() {
  Serial.begin(115200);
  Wire.begin();

  Serial.println("Initializing MPU6050...");
  mpu.initialize();

  Serial.println("Calibrating... Keep the robot steady!");
  delay(2000);

  // Perform calibration
  mpu.CalibrateAccel(6);
  mpu.CalibrateGyro(6);

  Serial.println("\nCalibration Finished!");
  Serial.println("Use these offsets in your balancing code:\n");

  Serial.print("XGyro Offset = "); Serial.println(mpu.getXGyroOffset());
  Serial.print("YGyro Offset = "); Serial.println(mpu.getYGyroOffset());
  Serial.print("ZGyro Offset = "); Serial.println(mpu.getZGyroOffset());
  Serial.print("ZAccel Offset = "); Serial.println(mpu.getZAccelOffset());

  Serial.println("\nCopy these values to your main code!");
}

void loop() {}
