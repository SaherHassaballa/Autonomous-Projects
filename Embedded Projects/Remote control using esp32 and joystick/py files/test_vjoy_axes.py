import serial
import time
import pyvjoy


# =========================================================
# CONFIGURATION
# =========================================================

PORT = "COM3"
BAUD = 115200

VJOY_DEVICE_ID = 1


# =========================================================
# JOYSTICK CALIBRATION
# =========================================================

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


# =========================================================
# FUNCTIONS
# =========================================================

def map_centered(value, minimum, center, maximum):

    if value <= center:

        output = (
            1000
            + (value - minimum) * 500
            / (center - minimum)
        )

    else:

        output = (
            1500
            + (value - center) * 500
            / (maximum - center)
        )

    return int(max(1000, min(2000, output)))


def map_throttle(value):

    output = 1000 + (value * 1000 / 4095)

    return int(max(1000, min(2000, output)))


def apply_deadzone(value, center=1500, zone=20):

    if abs(value - center) <= zone:
        return center

    return value


def rc_to_vjoy(value):

    return int(
        (value - 1000) * 32767 / 1000
    )


# =========================================================
# START
# =========================================================

print("Connecting to ESP32...")

ser = serial.Serial(
    PORT,
    BAUD,
    timeout=1
)

time.sleep(2)

print("ESP32 connected!")


print("Connecting to vJoy...")

joystick = pyvjoy.VJoyDevice(VJOY_DEVICE_ID)

print("vJoy connected!")
print("Controller is ready!")
print()


# =========================================================
# MAIN LOOP
# =========================================================

try:

    while True:

        line = ser.readline().decode(
            "utf-8",
            errors="ignore"
        ).strip()

        if not line:
            continue


        # -------------------------------------------------
        # Parse ESP32 data
        # -------------------------------------------------

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


        # -------------------------------------------------
        # RAW → RC 1000-2000
        # -------------------------------------------------

        ail = map_centered(
            raw_ail,
            AIL_MIN,
            AIL_CENTER,
            AIL_MAX
        )

        ele = map_centered(
            raw_ele,
            ELE_MIN,
            ELE_CENTER,
            ELE_MAX
        )

        rud = map_centered(
            raw_rud,
            RUD_MIN,
            RUD_CENTER,
            RUD_MAX
        )

        thr = map_throttle(raw_thr)


        # -------------------------------------------------
        # DEADZONE
        # -------------------------------------------------

        ail = apply_deadzone(ail)
        ele = apply_deadzone(ele)
        rud = apply_deadzone(rud)


        # -------------------------------------------------
        # RC → vJoy
        # -------------------------------------------------

        v_ail = rc_to_vjoy(ail)
        v_ele = rc_to_vjoy(ele)
        v_rud = rc_to_vjoy(rud)
        v_thr = rc_to_vjoy(thr)


        # -------------------------------------------------
        # SEND TO vJoy
        #
        # pyvjoy:
        # set_axis(axis, value)
        # -------------------------------------------------

        # AILERON → X
        joystick.set_axis(
            pyvjoy.HID_USAGE_X,
            v_ail
        )


        # ELEVATOR → Y
        joystick.set_axis(
            pyvjoy.HID_USAGE_Y,
            v_ele
        )


        # THROTTLE → Z
        joystick.set_axis(
            pyvjoy.HID_USAGE_Z,
            v_thr
        )


        # RUDDER → Rx
        joystick.set_axis(
            pyvjoy.HID_USAGE_RX,
            v_rud
        )


        # Update vJoy
        joystick.update()


        # -------------------------------------------------
        # DISPLAY
        # -------------------------------------------------

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
        joystick.reset()
        joystick.update()
    except:
        pass

    ser.close()

    print("Controller stopped.")


except Exception as e:

    print("\n\nERROR:")
    print(type(e).__name__)
    print(repr(e))

    try:
        joystick.reset()
    except:
        pass

    ser.close()