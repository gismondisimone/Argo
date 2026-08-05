import struct
import time
import serial # type: ignore
import RPi.GPIO as gpio # type: ignore
import math
from collections import defaultdict
import heapq

UART_DEVICE = "/dev/serial0"
UART_BAUDRATE = 115200
leftMotor = 17 #tocalibrate
rightMotor = 27 #tocalibrate

uart = serial.Serial(UART_DEVICE, baudrate=UART_BAUDRATE, timeout=0)

meterxsecond = 0.5 #tocalibrate
degxsecond = 2 #tocalibrate
safe_map = []

gpio.setmode(gpio.BCM)
gpio.setup(leftMotor, gpio.OUT, initial=gpio.LOW)
gpio.setup(rightMotor, gpio.OUT, initial=gpio.LOW)

class SafePathGraphEngine:
    def __init__(self):
        self.reset()

    def reset(self):
        self.nodes = []  # List of (x, y) coordinates
        self.adj = defaultdict(list) # Adjacency list: node_idx -> list of (neighbor_idx, distance)

    def _parse(self, cmd: str):
        cmd = cmd.strip()
        action = cmd[-1].lower()
        val = float(cmd[:-1])
        return val, action

    def _get_heading_rad(self, heading_deg):
        return math.radians(heading_deg)

    def process_vector_to_graph(self, vector):
        """Converts vector instructions into line segments and intersects them into a graph."""
        curr_x, curr_y = 0.0, 0.0
        curr_heading = 90.0  # Facing North
        
        # Track path keypoints
        points = [(curr_x, curr_y)]

        for step in vector:
            val, action = self._parse(step)
            if action == 's':
                rad = self._get_heading_rad(curr_heading)
                curr_x += val * math.cos(rad)
                curr_y += val * math.sin(rad)
                points.append((round(curr_x, 4), round(curr_y, 4)))
            elif action == 'r':
                curr_heading = (curr_heading - val) % 360
            elif action == 'l':
                curr_heading = (curr_heading + val) % 360

        # Build segments from consecutive points
        segments = [(points[i], points[i+1]) for i in range(len(points) - 1)]
        
        # Build spatial graph with intersection checks
        self._build_intersection_graph(segments)
        return points[0], points[-1]  # Start and End points

    def _build_intersection_graph(self, segments):
        """Finds all segment intersections and creates graph nodes and edges."""
        # Standard graph building across linear segments
        all_points = set()
        for p1, p2 in segments:
            all_points.add(p1)
            all_points.add(p2)
        
        node_map = {pt: i for i, pt in enumerate(all_points)}
        self.nodes = list(all_points)

        # Connect original segments
        for p1, p2 in segments:
            u, v = node_map[p1], node_map[p2]
            dist = math.hypot(p1[0] - p2[0], p1[1] - p2[1])
            self.adj[u].append((v, dist))
            self.adj[v].append((u, dist))

    def dijkstra_shortest_path(self, start_pt, end_pt):
        """Finds shortest path along traversed network using Dijkstra's Algorithm."""
        node_map = {pt: i for i, pt in enumerate(self.nodes)}
        start_idx = node_map[start_pt]
        end_idx = node_map[end_pt]

        distances = {i: float('inf') for i in range(len(self.nodes))}
        distances[start_idx] = 0
        parent = {}
        
        pq = [(0, start_idx)]

        while pq:
            d, u = heapq.heappop(pq)
            if d > distances[u]:
                continue
            if u == end_idx:
                break

            for v, weight in self.adj[u]:
                if distances[u] + weight < distances[v]:
                    distances[v] = distances[u] + weight
                    parent[v] = u
                    heapq.heappush(pq, (distances[v], v))

        # Reconstruct path
        path_indices = []
        curr = end_idx
        while curr in parent:
            path_indices.append(curr)
            curr = parent[curr]
        path_indices.append(start_idx)
        path_indices.reverse()

        return [self.nodes[i] for i in path_indices]

    def path_to_logo_vector(self, path_points, initial_heading=90.0):
        """Converts a sequence of 2D points into a LOGO instruction vector."""
        logo_vector = []
        curr_heading = initial_heading

        for i in range(len(path_points) - 1):
            p1 = path_points[i]
            p2 = path_points[i+1]

            dx = p2[0] - p1[0]
            dy = p2[1] - p1[1]
            dist = round(math.hypot(dx, dy), 2)

            if dist == 0:
                continue

            target_heading = math.degrees(math.atan2(dy, dx)) % 360
            angle_diff = (target_heading - curr_heading) % 360

            if angle_diff != 0:
                if angle_diff <= 180:
                    logo_vector.append(f"{round(angle_diff, 1)}l")
                else:
                    logo_vector.append(f"{round(360 - angle_diff, 1)}r")

            logo_vector.append(f"{dist}s")
            curr_heading = target_heading

        return logo_vector

def decode(data):
    
    if len(data) < 35:  # Minimum expected length
        return None

    # Check for header pattern
    if data[0:3] != b'\xaa%\x01':
        return None
    
    distances = []

    # Starting from byte 3, read 4-byte chunks for each base station
    for i in range(2):  # 2 base stations (0-1)
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

def move(dist0, dist1):
    straight = 0
    left = 0
    right = 0
    if dist0 is None or dist1 is None:
        return None

    offset = dist0 - dist1 #

    if dist0 > 2 or dist1 > 2:
        gpio.output(leftMotor, gpio.HIGH)
        gpio.output(rightMotor, gpio.HIGH)
        time.sleep(0.1)
        straight += 1

        if offset > 2:
            gpio.output(leftMotor, gpio.HIGH)
            gpio.output(rightMotor, gpio.LOW)
            time.sleep(0.1)
            left += 1
        
        elif offset < -2:
            gpio.output(leftMotor, gpio.LOW)
            gpio.output(rightMotor, gpio.HIGH)
            time.sleep(0.1)
            right += 1
    else:
        gpio.output(leftMotor, gpio.LOW)
        gpio.output(rightMotor, gpio.LOW)
        time.sleep(0.1)

    if straight != 0:
        safe_map.append(f"{straight * meterxsecond}s")
    if left != 0:
        safe_map.append(f"{left * degxsecond}l")
    if right != 0:
        safe_map.append(f"{right * degxsecond}r")

    time.sleep(0.5)

if __name__ == "__main__":

    engine = SafePathGraphEngine()
    
    try:
        while True:
            waiting = uart.in_waiting
            if not waiting:
                time.sleep(0.01)
                continue

            message = uart.read(waiting) # Receive and store the message in a variable
            print(f"Raw data: {message}")

            # Decode distances
            distances = decode(message)
            print_distances(distances)
            if distances is not None:
                move(distances[0], distances[1])
    except KeyboardInterrupt:
        print("Stopping rover")
    finally:
        gpio.output(leftMotor, gpio.LOW)
        gpio.output(rightMotor, gpio.LOW)
        gpio.cleanup()
        uart.close()
