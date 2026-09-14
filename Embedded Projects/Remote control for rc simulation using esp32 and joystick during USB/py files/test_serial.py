import serial
import time

PORT = "COM3"
BAUDRATE = 115200

print("Starting...")

try:
    ser = serial.Serial(PORT, BAUDRATE, timeout=1)
    print(f"Connected to {PORT}")

    time.sleep(2)

    while True:
        if ser.in_waiting:
            data = ser.readline().decode("utf-8", errors="ignore").strip()
            print(data)

except serial.SerialException as e:
    print("Serial error:")
    print(e)

except KeyboardInterrupt:
    print("\nStopped by user")

finally:
    try:
        ser.close()
    except:
        pass