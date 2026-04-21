import serial
import time
from datetime import datetime

# CHANGE THIS to your actual Arduino port
ser = serial.Serial('COM9', 9600, timeout=1)

time.sleep(2)  # allow Arduino to reset

while True:
    now = int(datetime.now().timestamp())  # Unix time
    ser.write(f"{now}\n".encode())

    print(f"Sent: {now}")
    time.sleep(1)