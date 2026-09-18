import serial
import struct
import time

s = serial.Serial('/dev/serial0', baudrate=115200, timeout=1)
rx_buffer = bytearray()
HEADER = b'\xaa%\x01'
FRAME_LENGTH = 35

print("In ascolto sulla seriale...")

while True:
    if s.in_waiting > 0:
        rx_buffer.extend(s.read(s.in_waiting))
        
        while len(rx_buffer) >= FRAME_LENGTH:
            header_index = rx_buffer.find(HEADER)
            
            if header_index == -1:
                # Scarta i dati inutili mantenendo gli ultimi byte per non spezzare l'header
                rx_buffer = rx_buffer[-2:]
                break
            
            if header_index > 0:
                rx_buffer = rx_buffer[header_index:]
            
            if len(rx_buffer) < FRAME_LENGTH:
                break
                
            frame = rx_buffer[:FRAME_LENGTH]
            rx_buffer = rx_buffer[FRAME_LENGTH:]
            
            print(f"Frame TWR Ricevuto! Data: {frame.hex()}")

    time.sleep(0.001)