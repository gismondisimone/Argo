import serial

s = serial.Serial('/dev/serial0', baudrate=115200, timeout=1)
print("In ascolto sulla seriale...")

while True:
    data = s.read(s.in_waiting or 1)
    if data:
        print(f"Ricevuto: {data}")