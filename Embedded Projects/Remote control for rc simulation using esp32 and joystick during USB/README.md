# ESP32 RC Controller → vJoy → XOutput → PhoenixRC

A DIY RC controller using an ESP32, two dual-axis joystick modules,
Python, vJoy, XOutput, ViGEmBus, and the PhoenixRC emulator.

## Architecture

``` text
Dual Joysticks
      ↓
    ESP32
      ↓ USB Serial @ 115200
    Python
      ↓
    vJoy
      ↓
   XOutput
      ↓
   ViGEmBus
      ↓
Virtual Xbox Controller
      ↓
PhoenixRC Emulator
      ↓
   PhoenixRC
```

------------------------------------------------------------------------

## 1. Hardware

### Required

-   ESP32 DevKit V1 / ESP32-WROOM
-   2 × dual-axis joystick modules
-   USB cable
-   Jumper wires
-   Windows PC

### Wiring

  Physical Input         ESP32 GPIO Function
  -------------------- ------------ ----------
  Right Joystick VRx         GPIO 4 Aileron
  Right Joystick VRy         GPIO 2 Elevator
  Left Joystick VRx         GPIO 36 Rudder
  Left Joystick VRy         GPIO 39 Throttle

> **Electrical warning:** ESP32 ADC inputs must not receive more than
> 3.3 V. Power the joystick modules from 3.3 V when compatible, or use
> suitable voltage protection if their analog output can exceed 3.3 V.

------------------------------------------------------------------------

## 2. ESP32 Firmware

Upload the ESP32 firmware using Arduino IDE.

The ESP32 reads the four analog axes and sends CSV data through USB
Serial at approximately 50 Hz.

### Serial format

``` text
Aileron,Elevator,Rudder,Throttle
```

Example:

``` text
1898,1498,1914,1914
```

### Serial settings

``` text
Baud Rate: 115200
```

Open Serial Monitor and move all four joysticks. Make sure the values
change.

------------------------------------------------------------------------

## 3. Python Setup

Create/use the Conda environment:

``` bash
conda activate drone_env
```

Install the required packages:

``` bash
pip install pyserial pyvjoy
```

Check the ESP32 COM port in:

``` text
Device Manager
→ Ports (COM & LPT)
```

Then edit `esp32_vjoy.py`:

``` python
PORT = "COM3"
BAUD = 115200
VJOY_DEVICE_ID = 1
```

Change `COM3` if Windows assigned another port.

Run:

``` bash
python esp32_vjoy.py
```

Expected output:

``` text
Connecting to ESP32...
ESP32 connected!
Connecting to vJoy...
vJoy connected!
Controller is ready!

AIL: 1500 | ELE: 1500 | RUD: 1500 | THR: 1500
```

------------------------------------------------------------------------

## 4. Calibration

The current calibration values are:

  Axis         MIN   CENTER    MAX
  ---------- ----- -------- ------
  Aileron        0     1898   4095
  Elevator       0     1498   4095
  Rudder         0     1914   4095
  Throttle       0      ---   4095

Centered axes are mapped approximately to:

``` text
MIN    → 1000
CENTER → 1500
MAX    → 2000
```

Throttle:

``` text
0    → 1000
4095 → 2000
```

A ±20 deadzone is applied to Aileron, Elevator, and Rudder.

------------------------------------------------------------------------

## 5. vJoy Setup

Install **vJoy** and open:

``` text
Configure vJoy
```

Configure **Device 1** with:

``` text
X
Y
Z
Rx
```

Test vJoy with:

``` text
Win + R
→ joy.cpl
→ vJoy Device
→ Properties
```

The Python program should update the vJoy axes when the ESP32 joysticks
are moved.

------------------------------------------------------------------------

## 6. XOutput Setup

Install and run:

-   XOutput
-   ViGEmBus

XOutput converts the vJoy DirectInput device into a virtual Xbox
controller.

### Tested XOutput Mapping

The following mapping is the **tested working configuration for this
project**:

``` text
LX → X Axis - vJoy Device
LY → Y Axis - vJoy Device
RX → X Axis - vJoy Device
RY → Y Axis - vJoy Device
LT → -
RT → -
```

> **Important:** This mapping is intentionally kept exactly as tested
> with this setup. Do not replace it with a generic vJoy/XInput mapping
> unless you are changing the project configuration.

After starting XOutput, Windows should detect a virtual Xbox controller.

You can verify it using:

``` text
Win + R
→ joy.cpl
```

------------------------------------------------------------------------

## 7. Test the Virtual Xbox Controller

Use an online gamepad tester and verify that the virtual Xbox controller
responds to the joystick movements.

The important point is that the controller must be detected by Windows
before starting PhoenixRC.

------------------------------------------------------------------------

## 8. PhoenixRC Emulator

PhoenixRC normally expects its dedicated USB interface.

For this project, the virtual controller is passed to PhoenixRC using:

**PhoenixRC_emu_v0_3**

Download:

https://drive.google.com/file/d/1CJnjsWPz2PvqQB3dnD5NKRyAPcTBAKJj/view

### Basic procedure

1.  Connect the ESP32.
2.  Start the Python controller.
3.  Start XOutput.
4.  Make sure the virtual Xbox controller is detected.
5.  Extract and start `PhoenixRC_emu_v0_3`.
6.  Select/use the detected controller through the emulator.
7.  Start PhoenixRC through the emulator.
8.  Calibrate the transmitter in PhoenixRC.

------------------------------------------------------------------------

## 9. PhoenixRC Calibration

After PhoenixRC starts, use:

``` text
Setup New Transmitter
```

Move the controls through their full range and complete the calibration.

Then create/save the required control profile.

------------------------------------------------------------------------

## 10. Startup Procedure

Every time the controller is used:

``` text
1. Connect ESP32
        ↓
2. Start Python
        ↓
3. Start XOutput
        ↓
4. Verify virtual Xbox controller
        ↓
5. Start PhoenixRC Emulator
        ↓
6. Start PhoenixRC
        ↓
7. Calibrate / select control profile
        ↓
8. Fly ✈️
```

------------------------------------------------------------------------

## 11. Troubleshooting

### ESP32 is detected but values do not change

Check:

-   Joystick wiring
-   GPIO numbers
-   ESP32 firmware
-   Serial Monitor
-   Baud rate = `115200`

### Python cannot connect to ESP32

Check:

``` text
PORT = "COMx"
```

Make sure no other application is using the ESP32 COM port.

### vJoy does not respond

Check:

-   vJoy is enabled
-   Device 1 exists
-   X/Y/Z/Rx axes are enabled
-   `VJOY_DEVICE_ID = 1`
-   `joy.cpl` shows the vJoy device

### Xbox controller is not detected

Check:

-   XOutput is running
-   ViGEmBus is installed
-   vJoy is working first
-   Restart XOutput after changing the vJoy configuration

### PhoenixRC does not detect the controller

Check the complete chain:

``` text
ESP32
 ↓
Python
 ↓
vJoy
 ↓
XOutput
 ↓
Virtual Xbox Controller
 ↓
PhoenixRC Emulator
 ↓
PhoenixRC
```

Make sure every stage is working before troubleshooting the next one.

------------------------------------------------------------------------

## Project Status

``` text
ESP32 Joystick Input       ✅
Python Serial Reader       ✅
Python → vJoy               ✅
vJoy → XOutput              ✅
Virtual Xbox Controller     ✅
PhoenixRC Emulator          ✅
PhoenixRC Control           ✅
```

## Notes

This README documents the configuration tested with this project. Axis
mappings may differ on other PCs or different vJoy/XOutput
configurations.
