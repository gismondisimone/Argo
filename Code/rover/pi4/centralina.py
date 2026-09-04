import serial  # type: ignore
import struct
import time
import threading

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

# Global buffers and synchronization locks
rx_buffer = bytearray()
buffer_lock = threading.Lock()
running = True

HEADER = b'\xaa%\x01'
FRAME_LENGTH = 35

def serial_reader_thread():
    """
    Background thread to continually read data from the serial port into rx_buffer.
    Processes string-based button signals ('BTN_') immediately on arrival.
    """
    global rx_buffer, ser, running
    
    while running:
        try:
            if ser.in_waiting > 0:
                data = ser.read(ser.in_waiting)
                
                # Direct check for ASCII text button press notifications
                try:
                    text_data = data.decode('utf-8', errors='ignore')
                    if "BTN_" in text_data:
                        for line in text_data.splitlines():
                            if "BTN_" in line:
                                print(f"\n[UWB Button Event Detected]: {line.strip()}")
                                # Process actions without affecting frame processing
                                if "BTN_HOME" in line:
                                    print("--> Action: home")
                                elif "BTN_WALK" in line:
                                    print("--> Action: walk")
                                elif "BTN_STOP" in line:
                                    print("--> Action: stop")
                                elif "BTN_COORD" in line:
                                    print("--> Action: coord")
                except Exception:
                    pass  # Ignore decode errors when parsing raw binary UWB frames

                # Append raw byte payload safely to the shared binary buffer
                with buffer_lock:
                    rx_buffer.extend(data)

        except (OSError, serial.SerialException) as e:
            print(f"\n[Hardware Reset Detected in Reader] {e}")
            try:
                ser.close()
            except Exception:
                pass
            time.sleep(1)
            ser = get_serial_port()
            with buffer_lock:
                rx_buffer.clear()

        time.sleep(0.005)

# Start background serial consumer
reader = threading.Thread(target=serial_reader_thread, daemon=True)
reader.start()

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

# Configuration
while mode is None:
    with buffer_lock:
        buffer_len = len(rx_buffer)

    if buffer_len >= FRAME_LENGTH:
        with buffer_lock:
            header_index = rx_buffer.find(HEADER)
            
            if header_index == -1:
                rx_buffer = rx_buffer[-2:]
                continue
            
            if header_index > 0:
                rx_buffer = rx_buffer[header_index:]
            
            if len(rx_buffer) < FRAME_LENGTH:
                continue
                
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
            
            # Additional sample extraction
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

    time.sleep(0.005)

# Main
while True:
    with buffer_lock:
        buffer_len = len(rx_buffer)

    if buffer_len >= FRAME_LENGTH:
        with buffer_lock:
            header_index = rx_buffer.find(HEADER)
            
            if header_index == -1:
                rx_buffer = rx_buffer[-2:]
                continue
            
            if header_index > 0:
                rx_buffer = rx_buffer[header_index:]
            
            if len(rx_buffer) < FRAME_LENGTH:
                continue
                
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

    time.sleep(0.005)