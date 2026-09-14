import serial
import time

PORT = "COM3"
BAUD = 115200

ser = serial.Serial(PORT, BAUD, timeout=1)

time.sleep(2)

print("Connected!")
print("Reading joystick data...\n")

while True:

    line = ser.readline().decode("utf-8", errors="ignore").strip()

    if not line:
        continue

    try:
        values = line.split(",")

        if len(values) != 4:
            continue

        aileron = int(values[0])
        elevator = int(values[1])
        rudder = int(values[2])
        throttle = int(values[3])

        print(
            f"AIL: {aileron:4d} | "
            f"ELE: {elevator:4d} | "
            f"RUD: {rudder:4d} | "
            f"THR: {throttle:4d}"
        )

    except ValueError:
        pass