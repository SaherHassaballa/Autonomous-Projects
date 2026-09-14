import pyvjoy
import time

print("Connecting to vJoy...")

j = pyvjoy.VJoyDevice(1)

print("Connected!")
print("Testing using vJoy data structure...\n")


def set_axes(x, y, z, rx):

    j.data.wAxisX = x
    j.data.wAxisY = y
    j.data.wAxisZ = z
    j.data.wAxisXRot = rx

    result = j.update()

    print(
        f"X={x:5d} | "
        f"Y={y:5d} | "
        f"Z={z:5d} | "
        f"Rx={rx:5d} | "
        f"Update={result}"
    )


try:

    while True:

        # CENTER
        set_axes(
            16384,
            16384,
            16384,
            16384
        )
        time.sleep(3)


        # MIN
        set_axes(
            1,
            1,
            1,
            1
        )
        time.sleep(3)


        # MAX
        set_axes(
            32768,
            32768,
            32768,
            32768
        )
        time.sleep(3)


except KeyboardInterrupt:

    print("\nStopping...")

    set_axes(
        16384,
        16384,
        16384,
        16384
    )

    print("Returned to center.")