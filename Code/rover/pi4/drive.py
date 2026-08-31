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

def calc_distances(samples):
    if not samples:
        return None, None
    left = None
    right = None
    for i in range(len(samples[0])):
        valid_distances = [s[i] for s in samples if s[i] is not None and s[i] > 0]
        if valid_distances:
            avg = sum(valid_distances) / len(valid_distances)
            if i == 0: left = f"{avg:.2f}"
            if i == 1: right = f"{avg:.2f}"
        else:
            if i == 0: left = None
            if i == 1: right = None

    return left, right

distance_samples = []
last_sample_time = time.time()
                
mode = None

while mode == None:
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

                distance_samples.append(distances)
                if len(distance_samples) >= 5:
                    print("Configuring...")
                    ogleft, ogright = calc_distances(distance_samples)
                    print(f"Left: {ogleft}, Right: {ogright}")
                    print("Turning 45 deg left")
                    time.sleep(2)
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

                    distance_samples.append(distances)

                    left, right = calc_distances(distance_samples)
                    print(f"Left: {left}, Right: {right}")
                    if left is not None and ogleft is not None:
                        if float(left) < float(ogleft):
                            print("Remote in front")
                            mode = "front"
                            break
                        elif float(left) > float(ogleft):
                            print("Remote in back")
                            mode = "back"
                            break
                        else:
                            print("Error in calculation. Please stay still during configuration.")
                    else:
                        print("One or both distances are not visible.")
                    distance_samples = []

                    print("Turning back straight")

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

                    if len(distance_samples) >= 10:
                        left, right = calc_distances(distance_samples)
                        print(f"Left: {left}, Right: {right}")
                        if left is not None and right is not None:
                            if (float(left) - float(right)) < -0.15:
                                if mode == "front": print("Turning Right")
                                else: print("Turning Left")
                            elif (float(left) - float(right)) > 0.15:
                                if mode == "front": print("Turning Left")
                                else: print("Turning Right")
                            else:
                                if mode == "front": print("Going straight")
                                else: print("Going back")
                        else:
                            print("One or both distances are not visible.")
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