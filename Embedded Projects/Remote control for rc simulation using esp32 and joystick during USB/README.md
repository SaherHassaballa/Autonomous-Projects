# ESP32 RC Controller → vJoy → XOutput

A DIY RC controller built with an **ESP32**, two dual-axis joystick modules, **vJoy**, **Python**, **XOutput**, and **ViGEmBus**.

The goal is to convert physical joystick movements into a standard **Xbox/XInput controller** for Windows games and RC flight simulators.

## Architecture

```text
Dual Joysticks
      ↓
    ESP32
      ↓ USB Serial @ 115200
 Python Controller
      ↓
    vJoy
      ↓
  XOutput
      ↓
  ViGEmBus
      ↓
Virtual Xbox 360 Controller
      ↓
Games / RC Simulators
```

---

## 1. Hardware

### Required

- ESP32 DevKit V1 / ESP32-WROOM-DA
- 2 × dual-axis joystick modules
- USB cable
- Jumper wires
- Windows PC

### Joystick Mapping

| Physical Input | ESP32 GPIO | Function | vJoy | XOutput |
|---|---:|---|---|---|
| Right joystick VRx | GPIO 4 | Aileron | X | LX |
| Right joystick VRy | GPIO 2 | Elevator | Y | LY |
| Left joystick VRx | GPIO 36 | Rudder | Rx | RX |
| Left joystick VRy | GPIO 39 | Throttle | Z | RT |
| SW | Unused | — | — | — |

### Wiring

```text
RIGHT JOYSTICK
VRx → GPIO 4  → Aileron
VRy → GPIO 2  → Elevator

LEFT JOYSTICK
VRx → GPIO 36 → Rudder
VRy → GPIO 39 → Throttle
```

### Electrical Warning

ESP32 ADC pins must not receive more than **3.3 V**.

Prefer powering the joystick modules from **3.3 V** when compatible. If a joystick is powered from 5 V and its analog output can reach 5 V, use an appropriate voltage divider.

---

# 2. ESP32 Firmware

The ESP32 reads the four analog axes and sends CSV data over USB serial at approximately 50 Hz.

```cpp
#define AILERON_PIN   4
#define ELEVATOR_PIN  2
#define RUDDER_PIN    36
#define THROTTLE_PIN  39

void setup() {
  Serial.begin(115200);
  analogReadResolution(12);
  delay(1000);
}

void loop() {
  int aileron  = analogRead(AILERON_PIN);
  int elevator = analogRead(ELEVATOR_PIN);
  int rudder   = analogRead(RUDDER_PIN);
  int throttle = analogRead(THROTTLE_PIN);

  Serial.print(aileron);
  Serial.print(",");
  Serial.print(elevator);
  Serial.print(",");
  Serial.print(rudder);
  Serial.print(",");
  Serial.println(throttle);

  delay(20);
}
```

Example output:

```text
1898,1498,1914,1914
```

Format:

```text
Aileron,Elevator,Rudder,Throttle
```

---

# 3. Arduino IDE Setup

1. Install Arduino IDE.
2. Install ESP32 board support.
3. Select the ESP32 board.
4. Select its COM port.
5. Upload the firmware.
6. Open Serial Monitor.
7. Set baud rate to:

```text
115200
```

8. Move all four axes and verify that the values change.

The tested ADC range is:

```text
MIN ≈ 0
MAX ≈ 4095
```

---

# 4. Calibration

Current calibration values:

```text
Aileron:
MIN    = 0
CENTER = 1898
MAX    = 4095

Elevator:
MIN    = 0
CENTER = 1498
MAX    = 4095

Rudder:
MIN    = 0
CENTER = 1914
MAX    = 4095

Throttle:
MIN = 0
MAX = 4095
```

Centered axes are mapped as:

```text
MIN    → 1000
CENTER → 1500
MAX    → 2000
```

Throttle:

```text
0    → 1000
4095 → 2000
```

A ±20 deadzone is applied around the 1500 center of Aileron, Elevator, and Rudder.

---

# 5. Python Environment

The working environment is:

```text
drone_env
```

Activate it:

```bash
conda activate drone_env
```

Install the required packages:

```bash
pip install pyserial pyvjoy
```

Current serial configuration:

```python
PORT = "COM3"
BAUD = 115200
```

Change `COM3` if Windows assigns another port.

---

# 6. vJoy Setup

Install **vJoy 2.1.9**.

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

Enable vJoy.

Mapping:

```text
vJoy X  → Aileron
vJoy Y  → Elevator
vJoy Z  → Throttle
vJoy Rx → Rudder
```

Test it using:

```text
Win + R
→ joy.cpl
```

Open `vJoy Device → Properties` and verify that the four axes move.

---

# 7. Python ESP32 → vJoy

Save the following as:

```text
esp32_vjoy.py
```

```python
import serial
import time
import pyvjoy

PORT = "COM3"
BAUD = 115200
VJOY_DEVICE_ID = 1

AIL_MIN = 0
AIL_CENTER = 1898
AIL_MAX = 4095

ELE_MIN = 0
ELE_CENTER = 1498
ELE_MAX = 4095

RUD_MIN = 0
RUD_CENTER = 1914
RUD_MAX = 4095

THR_MIN = 0
THR_MAX = 4095


def map_centered(value, minimum, center, maximum):
    if value <= center:
        output = 1000 + (value - minimum) * 500 / (center - minimum)
    else:
        output = 1500 + (value - center) * 500 / (maximum - center)

    return int(max(1000, min(2000, output)))


def map_throttle(value):
    output = 1000 + (value * 1000 / 4095)
    return int(max(1000, min(2000, output)))


def apply_deadzone(value, center=1500, zone=20):
    if abs(value - center) <= zone:
        return center
    return value


def rc_to_vjoy(value):
    return int((value - 1000) * 32767 / 1000)


print("Connecting to ESP32...")

ser = serial.Serial(PORT, BAUD, timeout=1)
time.sleep(2)

print("ESP32 connected!")
print("Connecting to vJoy...")

joystick = pyvjoy.VJoyDevice(VJOY_DEVICE_ID)

print("vJoy connected!")

joystick.data.wAxisX = 16384
joystick.data.wAxisY = 16384
joystick.data.wAxisZ = 16384
joystick.data.wAxisXRot = 16384
joystick.update()

print("Controller is ready!")
print()

try:
    while True:
        line = ser.readline().decode("utf-8", errors="ignore").strip()

        if not line:
            continue

        values = line.split(",")

        if len(values) != 4:
            continue

        try:
            raw_ail = int(values[0])
            raw_ele = int(values[1])
            raw_rud = int(values[2])
            raw_thr = int(values[3])
        except ValueError:
            continue

        ail = map_centered(raw_ail, AIL_MIN, AIL_CENTER, AIL_MAX)
        ele = map_centered(raw_ele, ELE_MIN, ELE_CENTER, ELE_MAX)
        rud = map_centered(raw_rud, RUD_MIN, RUD_CENTER, RUD_MAX)
        thr = map_throttle(raw_thr)

        ail = apply_deadzone(ail)
        ele = apply_deadzone(ele)
        rud = apply_deadzone(rud)

        v_ail = rc_to_vjoy(ail)
        v_ele = rc_to_vjoy(ele)
        v_rud = rc_to_vjoy(rud)
        v_thr = rc_to_vjoy(thr)

        joystick.data.wAxisX = v_ail
        joystick.data.wAxisY = v_ele
        joystick.data.wAxisZ = v_thr
        joystick.data.wAxisXRot = v_rud
        joystick.update()

        print(
            f"AIL: {ail:4d} | "
            f"ELE: {ele:4d} | "
            f"RUD: {rud:4d} | "
            f"THR: {thr:4d}",
            end="\r"
        )

except KeyboardInterrupt:
    print("\n\nStopping controller...")

    try:
        joystick.data.wAxisX = 16384
        joystick.data.wAxisY = 16384
        joystick.data.wAxisZ = 16384
        joystick.data.wAxisXRot = 16384
        joystick.update()
    except:
        pass

    ser.close()
    print("Controller stopped.")

except Exception as e:
    print("\n\nERROR:")
    print(type(e).__name__)
    print(repr(e))
    ser.close()
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

AIL: 1500 | ELE: 1500 | RUD: 1500 | THR: 1467
```

The working implementation updates vJoy using `joystick.data` followed by `joystick.update()`.

---

# 8. XOutput

vJoy provides a virtual DirectInput device.

Some games prefer **XInput**, so XOutput converts the vJoy input into a virtual Xbox 360 controller.

Install/run XOutput and configure:

```text
vJoy X         → LX
vJoy Y         → LY
vJoy X Rotation → RX
vJoy Z         → RT
```

Therefore:

```text
Aileron  → LX
Elevator → LY
Rudder   → RX
Throttle → RT
```

Start XOutput.

Windows should expose:

```text
Xbox 360 Controller
```

---

# 9. ViGEmBus

XOutput uses ViGEmBus to create the virtual Xbox controller.

After installation/restart, verify in Device Manager that the Xbox virtual controller is working.

The tested system showed:

```text
Xbox 360 Controller for Windows
```

with:

```text
This device is working properly.
```

---

# 10. Test the XInput Controller

Use:

```text
https://www.gamepadtester.com/
```

The controller should appear as something similar to:

```text
Xbox 360 Controller (XInput STANDARD GAMEPAD)
```

Verify:

```text
LX → Aileron
LY → Elevator
RX → Rudder
RT → Throttle
```

If all four axes move correctly, the complete software pipeline is working.

---

# 11. HidHide

Some games may see both:

```text
vJoy Device
Xbox 360 Controller
```

This can cause input conflicts.

HidHide can hide `vJoy Device` from games while allowing XOutput to continue accessing it.

Open:

```text
HidHide Configuration Client
```

### Applications

Add:

```text
XOutput.exe
```

### Devices

Hide:

```text
vJoy Device
```

Do **not** hide:

```text
Xbox 360 Controller
```

Enable:

```text
Enable device hiding
```

The intended result is:

```text
Game
 ↓
Xbox 360 Controller

XOutput
 ↓
vJoy
```

Restart the game after changing HidHide settings.

---

# 12. RC Simulator Testing

## Recommended: PicaSim

PicaSim is an RC flight simulator and is particularly suitable for testing this controller with fixed-wing aircraft.

Official website:

```text
https://www.rowlhouse.co.uk/PicaSim/
```

Configure:

```text
Right X → Aileron
Right Y → Elevator
Left X  → Rudder
Left Y  → Throttle
```

This is a better test for the project than a normal game because the actual target is RC aircraft control.

---

# 13. Phoenix RC

Phoenix RC's standard control-profile wizard expects its USB interface hardware.

Therefore, a generic:

```text
ESP32 → vJoy → XOutput
```

controller is not automatically accepted by the Phoenix setup wizard.

If Phoenix RC is required, use the appropriate genuine/compatible Phoenix USB interface.

For a generic USB/XInput controller workflow, PicaSim or another simulator with standard controller support is easier.

---

# 14. Complete Startup Procedure

Every time the controller is used:

### 1. Connect ESP32

Connect ESP32 through USB.

### 2. Start Python

```bash
conda activate drone_env
python esp32_vjoy.py
```

### 3. Start XOutput

Start the XOutput virtual controller.

### 4. Verify Windows

Open:

```text
Win + R
→ joy.cpl
```

Confirm the Xbox 360 controller exists.

### 5. Test XInput

Open:

```text
https://www.gamepadtester.com/
```

Verify all four axes.

### 6. Start the simulator/game

Launch PicaSim or another compatible application.

---

# 15. Troubleshooting

## ESP32 does not connect

Check:

- USB cable
- COM port
- `BAUD = 115200`
- Arduino Serial Monitor is closed
- Correct ESP32 board is selected

Find the port in:

```text
Device Manager
→ Ports (COM & LPT)
```

---

## COM3 is busy

Close:

- Arduino Serial Monitor
- Arduino Serial Plotter
- Other serial terminals

Then run:

```bash
python esp32_vjoy.py
```

---

## vJoy does not move

Open:

```text
joy.cpl
```

Check `vJoy Device`.

If the axes do not move:

1. Confirm Device 1 is enabled.
2. Confirm X/Y/Z/Rx are enabled.
3. Confirm Python is running.
4. Confirm the ESP32 is sending data.

---

## XOutput does not detect vJoy

Check:

1. vJoy works in `joy.cpl`.
2. XOutput is running.
3. The correct input device is selected.
4. ViGEmBus is installed.
5. Restart Windows if necessary.

---

## Game does not respond

First test:

```text
Gamepad Tester
```

If it detects:

```text
Xbox 360 Controller
```

then XOutput/ViGEm is working.

For games that still ignore input:

1. Make sure XOutput is running.
2. Make sure HidHide is not hiding the Xbox controller.
3. Hide only `vJoy Device`.
4. Add `XOutput.exe` to HidHide Applications.
5. Restart the game.

---

# 16. Project Status

### Completed

- [x] ESP32 joystick reading
- [x] Four analog axes
- [x] ADC calibration
- [x] Center calibration
- [x] Deadzone
- [x] ESP32 → PC serial communication
- [x] Python serial parsing
- [x] Python → vJoy
- [x] Dynamic vJoy movement
- [x] XOutput
- [x] ViGEmBus
- [x] Virtual Xbox 360 controller
- [x] Gamepad Tester detection
- [x] Four-axis XInput mapping

### Next Steps

- [ ] Final RC simulator configuration
- [ ] Build a physical transmitter enclosure
- [ ] Add switches/buttons
- [ ] Add trim controls
- [ ] Add configurable channel reversing
- [ ] Add expo/deadzone configuration
- [ ] Build wireless version using nRF24L01
- [ ] Add failsafe/signal-loss handling
- [ ] Design a custom PCB

---

# 17. Future Wireless Architecture

The future version can replace USB serial with an nRF24L01 wireless link:

```text
Joystick Modules
      ↓
ESP32 Transmitter
      ↓
nRF24L01
      ↓
nRF24L01
      ↓
ESP32 Receiver
      ↓
USB / Serial
      ↓
Python
      ↓
vJoy
      ↓
XOutput
      ↓
Virtual Xbox Controller
```

Possible future features:

- Wireless operation
- Battery monitoring
- Low-battery warning
- Signal-loss failsafe
- ARM switch
- Flight-mode switch
- Trim buttons
- OLED display
- Custom RC transmitter enclosure
- Custom PCB

---

# 18. Development Philosophy

Debug the system one layer at a time:

```text
1. Joysticks
      ↓
2. ESP32 ADC
      ↓
3. Serial
      ↓
4. Python
      ↓
5. vJoy
      ↓
6. XOutput
      ↓
7. XInput
      ↓
8. Simulator
```

If a higher layer fails, verify the previous layer before changing the entire system.

This makes troubleshooting much easier and prevents multiple unknown variables from being introduced at once.

---

## Project Goal

Build a low-cost, customizable RC transmitter interface using embedded hardware and open software components, with a future path toward a **wireless RC controller** suitable for flight simulation and experimentation.
