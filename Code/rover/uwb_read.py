import struct
import time
import serial

uart = serial.Serial("/dev/serial0", baudrate=115200, timeout=0)

def decode(data):
    """
    Decode UWB distance data from binary message
    Returns list of distances in meters for each base station
    """
    if len(data) < 35:  # Minimum expected length
        return None

    # Check for header pattern
    if data[0:3] != b'\xaa%\x01':
        return None

    # Extract distance data (skip header, process 4-byte chunks)
    distances = []

    # Starting from byte 3, read 4-byte chunks for each base station
    for i in range(8):  # 8 base stations (0-7)
        byte_offset = 3 + (i * 4)  # Each distance is 4 bytes
        if byte_offset + 3 < len(data):
            # Read as little-endian 32-bit integer
            distance_raw = struct.unpack('<I', data[byte_offset:byte_offset+4])[0]
            # Convert to meters
            if distance_raw > 0:
                distance_meters = distance_raw / 1000.0
                distances.append(distance_meters)
            else:
                distances.append(None)  # No signal/not visible
        else:
            distances.append(None)  # Base station not in data

    return distances

def print_distances(distances):
    """Print distances in a readable format"""
    if distances is None:
        print("Invalid data received")
        return

    print("Base Station Distances:")
    for i, distance in enumerate(distances):
        if distance is not None and distance > 0:
            print(f"  BS{i}: {distance:.3f}m")
        else:
            print(f"  BS{i}: Not visible")
    print("-" * 30)

while True:
    # Check if anything is available in buffer
    if uart.in_waiting:
        # Receive and store the message in a variable
        message = uart.read(uart.in_waiting)
        print(f"Raw data: {message}")

        # Decode distances
        distances = decode(message)
        print_distances(distances)
    else:
        time.sleep(0.01)
