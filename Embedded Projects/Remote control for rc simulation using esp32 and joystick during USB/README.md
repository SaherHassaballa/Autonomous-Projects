# ESP32 RC Controller → vJoy → XOutput → PhoenixRC

A DIY RC controller based on an ESP32, two dual-axis joystick modules, Python, vJoy, XOutput, ViGEmBus, and the PhoenixRC emulator.

---

# 1. Project Idea

The physical joysticks are read by the ESP32 and sent to the PC through USB Serial.

```text
Physical Joysticks
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

# 2. Physical Joystick Mapping

The two joystick modules provide four analog axes.

## Right Joystick

| Joystick Axis | ESP32 GPIO | RC Function |
|---|---:|---|
| VRx | GPIO 4 | **Aileron** |
| VRy | GPIO 2 | **Elevator** |

## Left Joystick

| Joystick Axis | ESP32 GPIO | RC Function |
|---|---:|---|
| VRx | GPIO 36 | **Rudder** |
| VRy | GPIO 39 | **Throttle** |

So the controller is:

```text
             RIGHT JOYSTICK
          ┌──────────────────┐
          │                  │
          │   X → Aileron    │
          │   Y → Elevator   │
          │                  │
          └──────────────────┘


              LEFT JOYSTICK
          ┌──────────────────┐
          │                  │
          │   X → Rudder     │
          │   Y → Throttle   │
          │                  │
          └──────────────────┘
```

### RC meaning

- **Aileron** → Roll the aircraft left/right
- **Elevator** → Pitch the aircraft up/down
- **Rudder** → Yaw the aircraft left/right
- **Throttle** → Motor/engine power

---

# 3. ESP32 → Python Data

The ESP32 reads the four ADC channels and sends:

```text
Aileron,Elevator,Rudder,Throttle
```

Example:

```text
1898,1498,1914,1914
```

Serial settings:

```text
Baud = 115200
```

The ESP32 sends new values approximately every 20 ms (~50 Hz).

---

# 4. Calibration

The tested calibration values are:

| Function | MIN | CENTER | MAX |
|---|---:|---:|---:|
| Aileron | 0 | 1898 | 4095 |
| Elevator | 0 | 1498 | 4095 |
| Rudder | 0 | 1914 | 4095 |
| Throttle | 0 | — | 4095 |

Centered RC axes are converted approximately as:

```text
ADC MIN     → 1000
ADC CENTER  → 1500
ADC MAX     → 2000
```

Throttle:

```text
ADC 0       → 1000
ADC 4095    → 2000
```

A ±20 deadzone is applied to:

```text
Aileron
Elevator
Rudder
```

---

# 5. Python → vJoy Mapping

The Python program writes the RC functions to vJoy.

```text
Aileron  → vJoy X
Elevator → vJoy Y
Throttle → vJoy Z
Rudder   → vJoy X Rotation (Rx)
```

In code:

```python
joystick.data.wAxisX     = v_ail
joystick.data.wAxisY     = v_ele
joystick.data.wAxisZ     = v_thr
joystick.data.wAxisXRot  = v_rud
```

Therefore, at the vJoy stage:

```text
vJoy X      = Aileron
vJoy Y      = Elevator
vJoy Z      = Throttle
vJoy Rx     = Rudder
```

---

# 6. vJoy Setup

Install vJoy and configure **Device 1**.

Enable these axes:

```text
X
Y
Z
Rx
```

Test:

```text
Win + R
→ joy.cpl
→ vJoy Device
→ Properties
```

The vJoy device should respond when the ESP32 joysticks are moved.

---

# 7. XOutput — What We Did

XOutput is used as a bridge between the vJoy device and a virtual Xbox/XInput controller.

```text
vJoy
  ↓
XOutput
  ↓
ViGEmBus
  ↓
Virtual Xbox Controller
```

### Important

The following is the **tested XOutput configuration used in this project**.

From the working configuration:

| XOutput XInput Control | Source |
|---|---|
| **LX** | X Axis - vJoy Device |
| **LY** | Y Axis - vJoy Device |
| **RX** | X Axis - vJoy Device |
| **RY** | Y Axis - vJoy Device |
| **LT** | Not configured |
| **RT** | Not configured |

In XOutput this appears as:

```text
LX → X Axis - vJoy Device
LY → Y Axis - vJoy Device
RX → X Axis - vJoy Device
RY → Y Axis - vJoy Device

LT → -
RT → -
```

### Why did we do this?

We used XOutput to take the axes coming from the vJoy device and expose them as an **XInput/Xbox controller**.

The important distinction is:

```text
RC FUNCTION MAPPING
        ↓
Aileron / Elevator / Rudder / Throttle
        ↓
       vJoy
        ↓
XOutput XInput Mapping
        ↓
Xbox Controller
```

The XOutput mapping is a **software translation layer**. It does not change what the physical joystick represents.

---

# 8. ViGEmBus

ViGEmBus provides the virtual Xbox controller used by XOutput.

After XOutput is running, Windows should see a virtual Xbox controller.

Check:

```text
Win + R
→ joy.cpl
```

---

# 9. Test the Xbox Controller

Before starting PhoenixRC, verify that the virtual controller is working.

Test the axes with an online gamepad/XInput tester.

The purpose of this test is to confirm:

```text
ESP32
 ↓
Python
 ↓
vJoy
 ↓
XOutput
 ↓
Virtual Xbox Controller
```

is working correctly.

---

# 10. PhoenixRC Emulator

PhoenixRC normally expects its dedicated USB interface.

For this project, the virtual controller is passed to PhoenixRC using:

**PhoenixRC_emu_v0_3**

Download:

https://drive.google.com/file/d/1CJnjsWPz2PvqQB3dnD5NKRyAPcTBAKJj/view

### Procedure

1. Connect the ESP32.
2. Start the Python controller.
3. Start XOutput.
4. Confirm that the virtual Xbox controller appears.
5. Extract `PhoenixRC_emu_v0_3`.
6. Start the PhoenixRC emulator.
7. Select/use the detected controller.
8. Start PhoenixRC through the emulator.
9. Calibrate the transmitter in PhoenixRC.

---

# 11. PhoenixRC Controls

The four RC functions remain:

```text
Aileron
Elevator
Rudder
Throttle
```

During PhoenixRC transmitter setup, move each control through its full range and assign/calibrate the corresponding function.

---

# 12. Complete Startup Procedure

```text
1. Connect ESP32
        ↓
2. Start Python
        ↓
3. Check vJoy
        ↓
4. Start XOutput
        ↓
5. Check Virtual Xbox Controller
        ↓
6. Start PhoenixRC Emulator
        ↓
7. Start PhoenixRC
        ↓
8. Calibrate transmitter
        ↓
9. Fly ✈️
```

---

# 13. Troubleshooting

## ESP32 is detected but values do not change

Check:

- GPIO wiring
- Arduino firmware
- Serial Monitor
- Baud rate = 115200
- Joystick power

## Python cannot connect

Check:

```python
PORT = "COMx"
```

Make sure another program is not using the COM port.

## vJoy does not move

Check:

- vJoy is enabled
- Device 1 exists
- X/Y/Z/Rx are enabled
- `VJOY_DEVICE_ID = 1`
- `joy.cpl` shows vJoy

## XOutput does not show the expected controller

Check:

- vJoy is already working
- XOutput is running
- ViGEmBus is installed
- The correct vJoy device is selected

## PhoenixRC does not detect the controller

Check the chain one stage at a time:

```text
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

Do not troubleshoot PhoenixRC until the virtual Xbox controller works correctly in Windows.

---

# 14. Final Mapping Summary

## Physical Hardware

```text
Right VRx → Aileron
Right VRy → Elevator

Left VRx  → Rudder
Left VRy  → Throttle
```

## Python → vJoy

```text
Aileron  → X
Elevator → Y
Throttle → Z
Rudder   → Rx
```

## Tested XOutput Configuration

```text
LX → X Axis - vJoy Device
LY → Y Axis - vJoy Device
RX → X Axis - vJoy Device
RY → Y Axis - vJoy Device

LT → Not configured
RT → Not configured
```

## Final System

```text
Joysticks
   ↓
ESP32
   ↓
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

## Project Status

```text
ESP32 Joystick Reading       ✅
Serial Communication         ✅
Python → vJoy                 ✅
vJoy → XOutput                ✅
Virtual Xbox Controller       ✅
PhoenixRC Emulator            ✅
PhoenixRC                     ✅
```

> This README documents the configuration tested with this project. Axis behavior can differ if the vJoy or XOutput configuration is changed.
