import serial # type: ignore
import struct
import time

ser = serial.Serial('/dev/serial0', baudrate=115200, timeout=1)

# Buffer to store incoming stream data
rx_buffer = bytearray()
HEADER = b'\xaa%\x01'
FRAME_LENGTH = 35  # Adjust if your specific UWB firmware uses a different frame length

def print_distances(samples):
    """Print the average of the collected distance measurements."""
    if not samples:
        print("Invalid data received")
        return

    print("Average Base Station Distances:")
    for i in range(len(samples[0])):
        valid_distances = [sample[i] for sample in samples
                           if sample[i] is not None and sample[i] > 0]
        if valid_distances:
            average = sum(valid_distances) / len(valid_distances)
            print(f"  BS{i}: {average:.3f}m")
        else:
            print(f"  BS{i}: Not visible")
    print("-" * 30)

distance_samples = []
last_sample_time = time.time()

while True:
    try:
        # Read ALL available bytes in one pass
        waiting_bytes = ser.in_waiting
        if waiting_bytes > 0:
            rx_buffer.extend(ser.read(waiting_bytes))
            
            # Process frames while buffer contains enough data
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
                
                # Parse packet
                distances = []
                for i in range(2):
                    byte_offset = 3 + (i * 4)
                    if byte_offset + 4 <= len(frame):
                        distance_raw = struct.unpack('<I', frame[byte_offset:byte_offset+4])[0]
                        if distance_raw > 0:
                            distances.append((distance_raw / 1000.0) - 0.20)
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
        print(f"\nSerial error: {e}")
        ser.close()
        time.sleep(1)
        ser = open_serial()
        rx_buffer.clear()

    # Smaller sleep time prevents serial buffer congestion
    time.sleep(0.001)