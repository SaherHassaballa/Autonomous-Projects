import serial
import time

PORT = "COM3"
BAUD = 115200

# =========================
# Joystick calibration
# =========================

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


# =========================
# Mapping functions
# =========================

def map_centered(value, minimum, center, maximum):
    """
    Map:
        minimum -> 1000
        center  -> 1500
        maximum -> 2000
    """

    if value <= center:
        output = 1000 + (value - minimum) * 500 / (center - minimum)
    else:
        output = 1500 + (value - center) * 500 / (maximum - center)

    return int(max(1000, min(2000, output)))


def map_throttle(value):
    """
    Map:
        0    -> 1000
        4095 -> 2000
    """

    output = 1000 + value * 1000 / 4095

    return int(max(1000, min(2000, output)))


# =========================
# Deadzone
# =========================

def deadzone(value, center=1500, zone=20):

    if abs(value - center) <= zone:
        return center

    return value


# =========================
# Serial
# =========================

ser = serial.Serial(PORT, BAUD, timeout=1)

time.sleep(2)

print("Connected!")
print("Reading mapped RC values...\n")

while True:

    line = ser.readline().decode("utf-8", errors="ignore").strip()

    if not line:
        continue

    try:

        values = line.split(",")

        if len(values) != 4:
            continue

        raw_ail = int(values[0])
        raw_ele = int(values[1])
        raw_rud = int(values[2])
        raw_thr = int(values[3])

        # Mapping
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

        # Deadzone for centered axes
        ail = deadzone(ail)
        ele = deadzone(ele)
        rud = deadzone(rud)

        print(
            f"AIL: {ail:4d} | "
            f"ELE: {ele:4d} | "
            f"RUD: {rud:4d} | "
            f"THR: {thr:4d}"
        )

    except ValueError:
        pass