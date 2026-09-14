# Self-Balancing Robot 🤖

A two-wheel self-balancing robot built around an **Arduino Leonardo**,
**MPU6050 IMU**, **L298N motor driver**, and a **PID controller**.

The project demonstrates the complete closed-loop control process used
to keep a two-wheeled robot upright:

``` text
        MPU6050
           │
           ▼
   Angle / Orientation
           │
           ▼
      PID Controller
           │
           ▼
      Motor Command
           │
           ▼
      L298N Driver
           │
           ▼
       DC Motors
           │
           ▼
    Robot Movement
           │
           └──────────► MPU6050 feedback
```

The project also includes **MPU6050 calibration**, **motor minimum-PWM
calibration**, and **MATLAB-based monitoring/analysis**.

------------------------------------------------------------------------

## Project Overview

A self-balancing robot is an example of a **closed-loop control
system**. The robot continuously measures its inclination, compares it
with the desired upright position, and changes the motor speed and
direction to correct its position.

The basic control loop is:

\[ e(t) = `\theta`{=tex}*{setpoint} - `\theta`{=tex}*{measured} \]

The PID controller then calculates the required motor command:

\[ u(t) = K_p e(t) + K_i `\int `{=tex}e(t)dt +
K_d`\frac{de(t)}{dt}`{=tex} \]

In this implementation, the robot uses the **MPU6050** to estimate its
pitch angle and a PID controller to generate the motor command.

------------------------------------------------------------------------

## Main Features

-   Two-wheel self-balancing robot
-   Arduino Leonardo control board
-   MPU6050 6-axis IMU
-   MPU6050 Digital Motion Processor (DMP)
-   Quaternion-based orientation processing
-   Pitch angle estimation
-   PID-based balancing
-   L298N dual H-bridge motor driver
-   Independent minimum-PWM calibration for each motor and direction
-   Motor calibration tools
-   MPU6050 calibration tools
-   MATLAB serial monitoring
-   Real-time transmission of:
    -   Robot angle
    -   PID output
    -   Motor speed command

------------------------------------------------------------------------

## Hardware

  Component          Purpose
  ------------------ ---------------------------------
  Arduino Leonardo   Main microcontroller
  MPU6050            Accelerometer + gyroscope / IMU
  L298N              Dual DC motor driver
  2 × DC motors      Robot actuation
  Wheels             Robot movement and balancing
  Battery            Power source
  Chassis            Mechanical structure

### MPU6050

The MPU6050 contains:

-   3-axis accelerometer
-   3-axis gyroscope
-   Digital Motion Processor (DMP)
-   I²C communication interface

The accelerometer provides information related to gravity and linear
acceleration, while the gyroscope measures angular velocity.

The DMP is used in this project to process the motion data and provide
orientation information.

------------------------------------------------------------------------

## Software

### Arduino

The main firmware uses:

-   `Wire.h`
-   `I2Cdev.h`
-   `PID_v1.h`
-   `MPU6050_6Axis_MotionApps20.h`

### MATLAB

MATLAB is used to receive and visualize data sent by the Arduino through
the serial port.

The project contains:

``` text
balance.m
```

for MATLAB-side analysis/plotting.

------------------------------------------------------------------------

## Repository Structure

``` text
BalanceRobot/
│
├── balance_robot/
│   │
│   ├── calibration_mpu/
│   │   └── MPU6050 calibration code
│   │
│   ├── motor_calibration/
│   │   └── Motor minimum-PWM calibration
│   │
│   ├── mpu/
│   │   └── MPU6050 / DMP related code
│   │
│   ├── saher_top_balance_robot/
│   │   └── Main self-balancing robot firmware
│   │
│   ├── sketch_dec5a/
│   │   └── Earlier project sketch
│   │
│   ├── sketch_dec5b/
│   │   └── Earlier project sketch
│   │
│   ├── balance.m
│   │   └── MATLAB data acquisition / visualization
│   │
│   └── README.md
```

------------------------------------------------------------------------

## Pin Configuration

The current Arduino Leonardo firmware uses the following pin
configuration.

### Right Motor

``` text
ENA = 5    PWM
IN1 = 6
IN2 = 4
```

### Left Motor

``` text
ENB = 9    PWM
IN3 = 10
IN4 = 11
```

### MPU6050 Interrupt

``` text
INT = 2
```

The MPU6050 communicates with the Arduino through **I²C**.

------------------------------------------------------------------------

## Motor Control

The L298N controls the direction and speed of both DC motors.

### Right Motor

``` text
IN1 = HIGH
IN2 = LOW
```

→ Forward

``` text
IN1 = LOW
IN2 = HIGH
```

→ Backward

### Left Motor

The left motor uses the opposite logical direction in the current
mechanical configuration:

``` text
IN3 = LOW
IN4 = HIGH
```

→ Forward

``` text
IN3 = HIGH
IN4 = LOW
```

→ Backward

The PWM signals on `ENA` and `ENB` control motor speed.

------------------------------------------------------------------------

## Motor Minimum-PWM Calibration

DC motors often do not start moving at very low PWM values.

For example:

``` text
PWM = 40   → motor may not move
PWM = 80   → motor may still not move
PWM = 100  → motor starts moving
```

Therefore, each motor and direction was calibrated separately.

Current calibration values:

  Motor     Direction     Minimum PWM
  --------- ----------- -------------
  Motor A   Forward                95
  Motor A   Backward               96
  Motor B   Forward                92
  Motor B   Backward              105

The controller adds the calibrated minimum PWM to the PID command:

``` cpp
int pA = constrain(p + minPWM_A_fwd,
                   minPWM_A_fwd, 255);

int pB = constrain(p + minPWM_B_fwd,
                   minPWM_B_fwd, 255);
```

This helps overcome motor dead-zone and improve low-speed control.

------------------------------------------------------------------------

## MPU6050 Processing

The project uses the MPU6050's **Digital Motion Processor (DMP)**.

Initialization:

``` cpp
mpu.initialize();
uint8_t devStatus = mpu.dmpInitialize();
```

The DMP is then enabled:

``` cpp
mpu.setDMPEnabled(true);
```

An interrupt is used to indicate that new DMP data is available.

The data flow is approximately:

``` text
Accelerometer ─┐
               │
               ▼
             MPU6050
               │
Gyroscope ─────┤
               ▼
              DMP
               │
               ▼
           Quaternion
               │
               ▼
         Gravity Vector
               │
               ▼
        Yaw / Pitch / Roll
```

------------------------------------------------------------------------

## Pitch Angle

The project extracts the pitch angle using:

``` cpp
mpu.dmpGetQuaternion(&q, fifoBuffer);
mpu.dmpGetGravity(&gravity, &q);
mpu.dmpGetYawPitchRoll(ypr, &q, &gravity);
```

The pitch value is:

``` cpp
ypr[1]
```

It is converted from radians to degrees:

``` cpp
input = ypr[1] * 180.0 / M_PI + 180.0;
```

The `+180` offset matches the angle representation used by the PID
setpoint.

------------------------------------------------------------------------

## PID Controller

The current PID parameters are:

``` cpp
Kp = 35.0
Ki = 0.0
Kd = 1.5
```

The desired balancing position is:

``` cpp
setpoint = 180.0;
```

The controller is configured with:

``` cpp
pid.SetMode(AUTOMATIC);
pid.SetSampleTime(10);
pid.SetOutputLimits(-255, 255);
```

Therefore, the PID output can range from:

``` text
-255 → 0 → +255
```

The sign determines the motor direction, while the magnitude determines
the motor effort.

``` text
PID Output
    │
    ├── Positive → Forward
    │
    ├── Negative → Backward
    │
    └── Zero     → Stop
```

------------------------------------------------------------------------

## Fall Protection

The robot should only drive the motors when the estimated angle is
within a safe balancing range.

The current limit is:

``` cpp
double fallLimit = 25.0;
```

The condition is:

``` cpp
if (abs(input - setpoint) < fallLimit)
```

If the robot exceeds the limit:

``` cpp
stopMotors();
```

This prevents the motors from continuing to run aggressively when the
robot has already fallen.

------------------------------------------------------------------------

## MATLAB Monitoring

The Arduino sends three values through the serial port:

``` text
Angle,PID_Output,Motor_Speed
```

For example:

``` text
181.24,-32.51,32
```

The transmitted values are:

1.  **Angle**
2.  **PID output**
3.  **Motor speed command**

The MATLAB script can be used to monitor the robot response and analyze
the balancing behavior.

This is useful for:

-   PID tuning
-   Checking oscillations
-   Measuring settling behavior
-   Observing angle error
-   Comparing controller response
-   Debugging the system

------------------------------------------------------------------------

## Calibration

Before running the balancing controller, two important calibration
stages are included.

### 1. MPU6050 Calibration

The project contains an MPU calibration section for determining sensor
offsets.

The main firmware currently uses:

``` cpp
mpu.setXGyroOffset(26);
mpu.setYGyroOffset(59);
mpu.setZGyroOffset(15);
mpu.setZAccelOffset(946);
```

These values compensate for sensor bias.

Sensor calibration is important because gyro bias and accelerometer
offsets can significantly affect angle estimation and therefore the
stability of the controller.

### 2. Motor Calibration

Each motor was tested to determine the minimum PWM required for
movement.

Because the two motors are not perfectly identical, separate values are
used for:

-   Motor A forward
-   Motor A backward
-   Motor B forward
-   Motor B backward

------------------------------------------------------------------------

## How the Balancing System Works

The complete system can be viewed as a feedback loop:

``` text
                  Desired Angle
                       │
                       ▼
                  ┌─────────┐
                  │   PID   │
                  └────┬────┘
                       │
                       ▼
                  Motor Command
                       │
                       ▼
                  L298N Driver
                       │
                       ▼
                    Motors
                       │
                       ▼
                 Robot Motion
                       │
                       ▼
                  MPU6050 IMU
                       │
                       ▼
                 Pitch Estimate
                       │
                       └───────────────┐
                                       │
                                       ▼
                                     PID
```

This is a **closed-loop control system** because the output of the
physical system is measured again and fed back into the controller.

------------------------------------------------------------------------

## Control Concept

When the robot tilts away from the upright position:

``` text
             Tilt
              ↓
        MPU6050 detects it
              ↓
        Pitch angle changes
              ↓
        PID calculates error
              ↓
        PID changes output
              ↓
       Motors change speed
              ↓
     Robot moves underneath
              ↓
       Robot returns upright
```

The process repeats continuously.

------------------------------------------------------------------------

## Safety

When testing the robot:

-   Keep the robot securely supported during initial tests.
-   Keep hands and loose objects away from the wheels.
-   Verify motor direction before enabling balancing.
-   Start with conservative PID values.
-   Test the MPU6050 readings before connecting the motors.
-   Make sure the fall protection works.
-   Disconnect power before changing wiring.
-   Check the battery and motor-driver wiring carefully.

------------------------------------------------------------------------

## Possible Improvements

Future versions could improve the controller and hardware by adding:

-   Complementary filter or Kalman/EKF-based attitude estimation
-   Better motor driver with lower losses
-   Encoders for wheel-speed feedback
-   Cascade angle + velocity control
-   LQR control
-   Better mechanical balancing
-   Battery-voltage monitoring
-   Wireless telemetry
-   ROS 2 integration
-   Real-time MATLAB/Simulink control analysis
-   Improved PID auto-tuning
-   IMU temperature compensation

A particularly useful next step is adding **wheel encoders**. The
current controller primarily stabilizes the robot's body angle; encoders
would provide information about wheel motion and allow a higher-level
velocity/position loop.

------------------------------------------------------------------------

## Learning Goals

This project was developed to understand practical concepts in:

-   Embedded systems
-   Arduino programming
-   I²C communication
-   IMU sensors
-   Accelerometers
-   Gyroscopes
-   Sensor calibration
-   Digital Motion Processing
-   Quaternion-based orientation
-   PID control
-   Closed-loop control
-   DC motor control
-   PWM
-   Motor-driver interfaces
-   MATLAB data visualization
-   Real-time embedded feedback systems

------------------------------------------------------------------------

## Author

**Saher Hassaballa**

Engineering project focused on embedded systems, control, robotics, and
autonomous systems.

------------------------------------------------------------------------

## License

This project is intended primarily for educational and experimental
purposes.

