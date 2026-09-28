# ESP32 RC Controller → PhoenixRC

A DIY RC controller using an ESP32, dual-axis joystick modules, vJoy, Python, XOutput, and the PhoenixRC emulator.

## Architecture

```text
Joysticks
    ↓
  ESP32
    ↓ USB Serial
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

---

## 1. Hardware

Required:

* ESP32 DevKit / ESP32-WROOM
* 2 × Dual-axis joystick modules
* USB cable
* Jumper wires
* Windows PC

### Joystick Mapping

| Physical Input | ESP32 GPIO | Function |
| -------------- | ---------: | -------- |
| Right VRx      |     GPIO 4 | Aileron  |
| Right VRy      |     GPIO 2 | Elevator |
| Left VRx       |    GPIO 36 | Rudder   |
| Left VRy       |    GPIO 39 | Throttle |

> ⚠️ ESP32 ADC inputs must not receive more than 3.3 V.

---

## 2. ESP32 Firmware

Upload the ESP32 firmware using Arduino IDE.

The ESP32 sends the four analog values through USB Serial:

```text
Aileron,Elevator,Rudder,Throttle
```

Example:

```text
1898,1498,1914,1914
```

Serial settings:

```text
Baud Rate: 115200
```

Verify the values change when moving the four joysticks.

---

## 3. Python Environment

Activate the Conda environment:

```bash
conda activate drone_env
```

Install the required packages:

```bash
pip install pyserial pyvjoy
```

Change the COM port in `esp32_vjoy.py` if necessary:

```python
PORT = "COM3"
BAUD = 115200
```

Run:

```bash
python esp32_vjoy.py
```

Expected:

```text
Connecting to ESP32...
ESP32 connected!
Connecting to vJoy...
vJoy connected!
Controller is ready!
```

---

## 4. vJoy Setup

Install and configure **vJoy**.

Open:

```text
Configure vJoy
```

Configure Device 1 with:

```text
X
Y
Z
Rx
```

Test the device:

```text
Win + R
→ joy.cpl
→ vJoy Device
→ Properties
```

Move the joysticks and verify the axes respond.

---

## 5. XOutput

Install **XOutput** and **ViGEmBus**.

XOutput converts the vJoy DirectInput device into a virtual Xbox controller.

Recommended mapping:

```text
vJoy X       → Xbox LX
vJoy Y       → Xbox LY
vJoy Rx      → Xbox RX
vJoy Z       → Xbox RT
```

Therefore:

```text
Aileron  → LX
Elevator → LY
Rudder   → RX
Throttle → RT
```

Start the XOutput controller.

Verify that Windows detects:

```text
Xbox 360 Controller
```

You can test it using an online gamepad tester.

---

## 6. PhoenixRC Emulator

PhoenixRC normally expects its own USB interface. The emulator allows a Windows joystick/controller to be used with PhoenixRC.

### Emulator

Download:

[PhoenixRC_emu_v0_3.zip](https://drive.google.com/file/d/1CJnjsWPz2PvqQB3dnD5NKRyAPcTBAKJj/view?utm_source=chatgpt.com)

Extract the emulator files.

The emulator is designed to let PhoenixRC use a joystick recognized by Windows instead of the original Phoenix USB interface.

### Basic setup

1. Make sure the virtual Xbox controller is running.
2. Make sure Windows detects the controller.
3. Install/copy the Phoenix emulator files according to the emulator package instructions.
4. Run the emulator launcher.
5. Select the detected controller/joystick.
6. Launch PhoenixRC through the emulator.

The emulator launcher is intended to detect the Windows joystick and pass it to PhoenixRC as the expected interface.

---

## 7. PhoenixRC Calibration

After PhoenixRC starts:

```text
System
   ↓
Setup New Transmitter
```

Move the four controls through their full ranges and follow the calibration wizard.

Configure:

```text
Aileron
Elevator
Rudder
Throttle
```

Then save the transmitter/control profile.

---

## 8. Complete Startup Procedure

Every time:

```text
1. Connect ESP32
        ↓
2. Start Python
        ↓
3. Start XOutput
        ↓
4. Verify Xbox Controller
        ↓
5. Start PhoenixRC Emulator
        ↓
6. Select the controller
        ↓
7. Launch PhoenixRC
        ↓
8. Calibrate transmitter
        ↓
9. Fly 🚁✈️
```

### Troubleshooting

If PhoenixRC does not detect the controller:

* Check that ESP32 values are changing.
* Check `joy.cpl` and verify vJoy.
* Check that XOutput is running.
* Verify the virtual Xbox controller is detected by Windows.
* Make sure the correct controller is selected in the emulator.
* Restart PhoenixRC after changing controller settings.

> **Important:** PhoenixRC should be launched through the emulator when using a generic/virtual controller. The normal PhoenixRC setup expects its dedicated USB interface.
