import serial
import time

PORT = "COM3"
BAUD = 115200

ser = serial.Serial(PORT, BAUD, timeout=1)
time.sleep(2)

print("\n==============================")
print("      RC CALIBRATION")
print("==============================\n")


def read_values():
    while True:
        line = ser.readline().decode("utf-8", errors="ignore").strip()

        if not line:
            continue

        try:
            values = line.split(",")

            if len(values) != 4:
                continue

            return [int(x) for x in values]

        except ValueError:
            continue


def calibrate_axis(name, index):
    print(f"\n--- {name} ---")
    print("Move ONLY this axis from one extreme to the other.")
    print("You have 5 seconds.")
    input("Press ENTER to start...")

    minimum = 4095
    maximum = 0

    start = time.time()

    while time.time() - start < 5:

        values = read_values()
        value = values[index]

        minimum = min(minimum, value)
        maximum = max(maximum, value)

        print(
            f"\rValue: {value:4d} | "
            f"MIN: {minimum:4d} | "
            f"MAX: {maximum:4d}",
            end=""
        )

    print()

    return minimum, maximum


# --------------------------------
# Aileron
# --------------------------------

ail_min, ail_max = calibrate_axis(
    "AILERON", 0
)

# --------------------------------
# Elevator
# --------------------------------

ele_min, ele_max = calibrate_axis(
    "ELEVATOR", 1
)

# --------------------------------
# Rudder
# --------------------------------

rud_min, rud_max = calibrate_axis(
    "RUDDER", 2
)

# --------------------------------
# Throttle
# --------------------------------

thr_min, thr_max = calibrate_axis(
    "THROTTLE", 3
)


print("\n")
print("==============================")
print("     CALIBRATION RESULT")
print("==============================")

print(f"\nAILERON")
print(f"MIN = {ail_min}")
print(f"CENTER = 1898")
print(f"MAX = {ail_max}")

print(f"\nELEVATOR")
print(f"MIN = {ele_min}")
print(f"CENTER = 1498")
print(f"MAX = {ele_max}")

print(f"\nRUDDER")
print(f"MIN = {rud_min}")
print(f"CENTER = 1914")
print(f"MAX = {rud_max}")

print(f"\nTHROTTLE")
print(f"MIN = {thr_min}")
print(f"MAX = {thr_max}")

ser.close()