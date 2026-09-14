import pyvjoy

print("=== vJoy Diagnostic ===")

try:
    print("Creating device...")

    j = pyvjoy.VJoyDevice(1)

    print("Device created: OK")

    print("Testing reset...")

    result = j.reset()

    print("Reset result:", result)

    print("Testing X axis...")

    result = j.set_axis(
        pyvjoy.HID_USAGE_X,
        16384
    )

    print("X result:", result)

    print("Updating device...")

    result = j.update()

    print("Update result:", result)

    print("\nSUCCESS!")

except Exception as e:

    print("\nFAILED")
    print("Exception type:", type(e).__name__)
    print("Exception:", repr(e))