import serial # type: ignore
import struct
import time

def get_serial_port():
    while True:
        try:
            s = serial.Serial('/dev/serial0', baudrate=115200, timeout=1)
            print("Connected to serial port.")
            return s
        except Exception as e:
            print(f"Waiting for serial port... ({e})")
            time.sleep(2)

ser = get_serial_port()
rx_buffer = bytearray()
HEADER = b'\xaa%\x01'
FRAME_LENGTH = 35

def print_distances(samples):
    if not samples:
        return
    print("Average Base Station Distances:")
    for i in range(len(samples[0])):
        valid_distances = [s[i] for s in samples if s[i] is not None and s[i] > 0]
        if valid_distances:
            avg = sum(valid_distances) / len(valid_distances)
            print(f"  BS{i}: {avg:.3f}m")
        else:
            print(f"  BS{i}: Not visible")
    print("-" * 30)

distance_samples = []
last_sample_time = time.time()

while True:
    try:
        if ser.in_waiting > 0:
            rx_buffer.extend(ser.read(ser.in_waiting))
            
            while len(rx_buffer) >= FRAME_LENGTH:
                header_index = rx_buffer.find(HEADER)
                
                if header_index == -1:
                    rx_buffer = rx_buffer[-2:]
                    break
                
                if header_index > 0:
                    rx_buffer = rx_buffer[header_index:]
                
                if len(rx_buffer) < FRAME_LENGTH:
                    break
                    
                frame = rx_buffer[:FRAME_LENGTH]
                rx_buffer = rx_buffer[FRAME_LENGTH:]
                
                distances = []
                for i in range(2):
                    offset = 3 + (i * 4)
                    if offset + 4 <= len(frame):
                        raw = struct.unpack('<I', frame[offset:offset+4])[0]
                        if raw > 0:
                            distances.append((raw / 1000.0) - 0.20)
                        else:
                            distances.append(None)
                    else:
                        distances.append(None)

                now = time.time()
                if (now - last_sample_time) >= 0.1:
                    distance_samples.append(distances)
                    last_sample_time = now

                    if len(distance_samples) >= 5:
                        print_distances(distance_samples)
                        distance_samples = []

    except (OSError, serial.SerialException) as e:
        print(f"\n[Hardware Reset Detected] {e}")
        try:
            ser.close()
        except Exception:
            pass
        time.sleep(1)
        ser = get_serial_port()
        rx_buffer.clear()

    time.sleep(0.001)