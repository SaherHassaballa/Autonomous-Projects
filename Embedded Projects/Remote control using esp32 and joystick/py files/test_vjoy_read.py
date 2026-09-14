import pygame
import time

pygame.init()
pygame.joystick.init()

count = pygame.joystick.get_count()

print("Joysticks found:", count)

if count == 0:
    print("No joystick detected!")
    input("Press Enter to exit...")
    exit()

for i in range(count):
    js = pygame.joystick.Joystick(i)
    js.init()

    print(f"{i}: {js.get_name()}")
    print("Axes:", js.get_numaxes())

print("\nReading vJoy...")
print("Move your joysticks.\n")

js = pygame.joystick.Joystick(0)

while True:

    pygame.event.pump()

    axes = []

    for i in range(js.get_numaxes()):
        axes.append(round(js.get_axis(i), 3))

    print(axes, end="\r")

    time.sleep(0.05)