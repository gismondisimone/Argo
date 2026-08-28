from machine import UART, Pin # type: ignore
import struct
import time

uart = UART(0, baudrate=115200, tx=Pin(0), rx=Pin(1))

def decode_uwb_distances(data):

    if len(data) < 35:  # Minimum expected length
        return None

    # Check for header pattern
    if data[0:3] != b'\xaa%\x01':
        return None

    # Extract distance data (skip header, process 4-byte chunks)
    distances = []

    # Starting from byte 3, read 4-byte chunks for each base station
    for i in range(2):  # 2 base stations (0-1)
        byte_offset = 3 + (i * 4)  # Each distance is 4 bytes
        if byte_offset + 3 < len(data):
            # Read as little-endian 32-bit integer
            distance_raw = struct.unpack('<I', data[byte_offset:byte_offset+4])[0]
            # Convert to meters
            if distance_raw > 0:
                distance_meters = (distance_raw / 1000.0) - 0.20    
                distances.append(distance_meters)
            else:
                distances.append(None)  # No signal/not visible
        else:
            distances.append(None)  # Base station not in data

    return distances

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
last_sample_time = time.ticks_ms()

while True:
    # Check if anything is available in buffer
    if uart.any():
        # Receive and store the message in a variable
        message = uart.read()
        # print(f"Raw data: {message}")

        # Decode distances and take one sample approximately every 0.1 seconds
        distances = decode_uwb_distances(message)
        now = time.ticks_ms()
        if distances is not None and time.ticks_diff(now, last_sample_time) >= 100:
            distance_samples.append(distances)
            last_sample_time = now

            if len(distance_samples) >= 5:
                print_distances(distance_samples)
                distance_samples = []