import heapq
import math
import struct
import time
from collections import defaultdict

import serial  # type: ignore
from gpiozero import DigitalInputDevice, DigitalOutputDevice, PWMOutputDevice  # type: ignore

#serial
SERIAL_DEVICE = "/dev/serial0"
SERIAL_BAUDRATE = 115200
HEADER = b"\xaa%\x01"
FRAME_LENGTH = 35

#infrared
Lir = 7 #dacambiare
Rir = 11 #dacambiare

# ZS-Z11H driver pins (BCM numbers). Fill these in after wiring the drivers.
# Each driver needs one PWM pin and one DIR pin. Leave as None until then.
LmotPWM = None #dacambiare left driver's P input
LmotDIR = None #dacambiare left driver's DIR input
RmotPWM = None #dacambiare right driver's P input
RmotDIR = None #dacambiare right driver's DIR input

#movement, calibrate these values for the rover
meterxsecond = 0.5
degxsecond = 2
MOVE_SPEED = 0.18
TURN_SPEED = 0.10
MINIMUM_USER_DISTANCE = 0.15
DISTANCE_DIFFERENCE_THRESHOLD = 0.15
DISTANCE_OFFSET = 0.20 #distance between uwb
safe_map = []

HEADER_LENGTH = len(HEADER)


class SafePathEngine:
    def __init__(self):
        self.reset()

    def reset(self):
        self.nodes = [] # List of (x, y) coordinates
        self.adj = defaultdict(list) # node index -> (neighbor index, distance)

    def _parse(self, cmd: str):
        cmd = cmd.strip()
        action = cmd[-1].lower()
        val = float(cmd[:-1])
        return val, action

    def process_vector_to_graph(self, vector):
        """Convert movement instructions into a graph of traveled segments."""
        curr_x, curr_y = 0.0, 0.0
        curr_heading = 90.0 # Facing North
        points = [(curr_x, curr_y)]

        for step in vector:
            val, action = self._parse(step)
            if action == "s":
                rad = math.radians(curr_heading)
                curr_x += val * math.cos(rad)
                curr_y += val * math.sin(rad)
                points.append((round(curr_x, 4), round(curr_y, 4)))
            elif action == "r":
                curr_heading = (curr_heading - val) % 360
            elif action == "l":
                curr_heading = (curr_heading + val) % 360

        all_points = set(points)
        self.nodes = list(all_points)
        node_map = {point: i for i, point in enumerate(self.nodes)}
        self.adj = defaultdict(list)

        for i in range(len(points) - 1):
            p1, p2 = points[i], points[i + 1]
            u, v = node_map[p1], node_map[p2]
            distance = math.hypot(p1[0] - p2[0], p1[1] - p2[1])
            self.adj[u].append((v, distance))
            self.adj[v].append((u, distance))

        return points[0], points[-1]

    def dijkstra_shortest_path(self, start_pt, end_pt):
        """Find the shortest route between points in the traveled graph."""
        if start_pt not in self.nodes or end_pt not in self.nodes:
            return []

        node_map = {point: i for i, point in enumerate(self.nodes)}
        start_idx, end_idx = node_map[start_pt], node_map[end_pt]
        distances = [float("inf")] * len(self.nodes)
        distances[start_idx] = 0
        parent = {}
        queue = [(0, start_idx)]

        while queue:
            distance, u = heapq.heappop(queue)
            if distance > distances[u]:
                continue
            if u == end_idx:
                break
            for v, weight in self.adj[u]:
                new_distance = distance + weight
                if new_distance < distances[v]:
                    distances[v] = new_distance
                    parent[v] = u
                    heapq.heappush(queue, (new_distance, v))

        if distances[end_idx] == float("inf"):
            return []

        path = [end_idx]
        while path[-1] != start_idx:
            path.append(parent[path[-1]])
        path.reverse()
        return [self.nodes[i] for i in path]

    def path_to_logo_vector(self, path_points, initial_heading=90.0):
        """Convert graph points into straight/left/right movement commands."""
        logo_vector = []
        curr_heading = initial_heading

        for i in range(len(path_points) - 1):
            p1, p2 = path_points[i], path_points[i + 1]
            dx, dy = p2[0] - p1[0], p2[1] - p1[1]
            distance = round(math.hypot(dx, dy), 2)
            if distance == 0:
                continue

            target_heading = math.degrees(math.atan2(dy, dx)) % 360
            angle_diff = (target_heading - curr_heading) % 360
            if angle_diff != 0:
                if angle_diff <= 180:
                    logo_vector.append(f"{round(angle_diff, 1)}l")
                else:
                    logo_vector.append(f"{round(360 - angle_diff, 1)}r")
            logo_vector.append(f"{distance}s")
            curr_heading = target_heading

        return logo_vector


def get_serial_port():
    while True:
        try:
            ser = serial.Serial(SERIAL_DEVICE, baudrate=SERIAL_BAUDRATE, timeout=1)
            print("Connected to serial port.")
            return ser
        except (OSError, serial.SerialException) as e:
            print(f"Waiting for serial port... ({e})")
            time.sleep(2)


def calc_distances(samples):
    if not samples:
        return None, None

    left = [sample[0] for sample in samples if sample[0] is not None]
    right = [sample[1] for sample in samples if sample[1] is not None]
    left = sum(left) / len(left) if left else None
    right = sum(right) / len(right) if right else None
    return left, right


def decode(frame):
    if len(frame) != FRAME_LENGTH or frame[:HEADER_LENGTH] != HEADER:
        return None

    distances = []
    for i in range(2):
        offset = HEADER_LENGTH + (i * 4)
        raw = struct.unpack_from("<I", frame, offset)[0]
        if raw > 0:
            distances.append((raw / 1000.0) - DISTANCE_OFFSET)
        else:
            distances.append(None)
    return distances


def get_frames(rx_buffer):
    while True:
        header_index = rx_buffer.find(HEADER)
        if header_index == -1:
            del rx_buffer[:-HEADER_LENGTH + 1]
            return
        if header_index > 0:
            del rx_buffer[:header_index]
        if len(rx_buffer) < FRAME_LENGTH:
            return

        frame = bytes(rx_buffer[:FRAME_LENGTH])
        del rx_buffer[:FRAME_LENGTH]
        yield frame


def move(left_speed, right_speed, leftMotor, rightMotor):
    """Set signed wheel speeds; negative speed selects reverse on the driver."""
    if leftMotor is None or rightMotor is None:
        print("Motor pins are placeholders. Set LmotPWM/LmotDIR/RmotPWM/RmotDIR.")
        return

    leftMotor[0].value = abs(left_speed)
    rightMotor[0].value = abs(right_speed)
    # ZS-Z11H DIR is active low; zero duty coasts the wheel.
    leftMotor[1].value = 0 if left_speed < 0 else 1
    rightMotor[1].value = 0 if right_speed < 0 else 1


def stop(leftMotor, rightMotor):
    if leftMotor is not None:
        leftMotor[0].off()
    if rightMotor is not None:
        rightMotor[0].off()


def move_following(left, right, mode, ifr1, ifr2, leftMotor, rightMotor):
    if left is None or right is None:
        print("One or both distances are not visible. Stopping.")
        stop(leftMotor, rightMotor)
        return

    if not ifr1.value or not ifr2.value:
        print("Obstacle detected. Stopping.")
        stop(leftMotor, rightMotor)
        return

    if left <= MINIMUM_USER_DISTANCE or right <= MINIMUM_USER_DISTANCE:
        print("Too close to user. Stopping.")
        stop(leftMotor, rightMotor)
        return

    difference = left - right
    direction = 1 if mode == "front" else -1
    if difference < -DISTANCE_DIFFERENCE_THRESHOLD:
        print("Turning Left")
        # Reverse the wheel-speed bias when driving backwards.
        left_speed = direction * (TURN_SPEED if direction > 0 else MOVE_SPEED)
        right_speed = direction * (MOVE_SPEED if direction > 0 else TURN_SPEED)
    elif difference > DISTANCE_DIFFERENCE_THRESHOLD:
        print("Turning Right")
        left_speed = direction * (MOVE_SPEED if direction > 0 else TURN_SPEED)
        right_speed = direction * (TURN_SPEED if direction > 0 else MOVE_SPEED)
    else:
        print("Going straight" if mode == "front" else "Going back")
        left_speed = direction * MOVE_SPEED
        right_speed = direction * MOVE_SPEED

    move(left_speed, right_speed, leftMotor, rightMotor)


def main():
    ifr1 = DigitalInputDevice(Lir, pull_up=True)
    ifr2 = DigitalInputDevice(Rir, pull_up=True)

    motors_configured = all(pin is not None for pin in
                            (LmotPWM, LmotDIR, RmotPWM, RmotDIR))
    if motors_configured:
        leftMotor = (PWMOutputDevice(LmotPWM, frequency=1000, initial_value=0),
                     DigitalOutputDevice(LmotDIR, initial_value=True))
        rightMotor = (PWMOutputDevice(RmotPWM, frequency=1000, initial_value=0),
                      DigitalOutputDevice(RmotDIR, initial_value=True))
    else:
        leftMotor = rightMotor = None
        print("Motor GPIO pins are placeholders; fill them in before motor control.")

    ser = None
    rx_buffer = bytearray()
    distance_samples = []
    last_sample_time = time.monotonic()
    last_frame_time = time.monotonic()
    ogleft = None
    mode = None
    calibrating = False

    try:
        ser = get_serial_port()
        while True:
            if ser.in_waiting:
                rx_buffer.extend(ser.read(ser.in_waiting))

            for frame in get_frames(rx_buffer):
                distances = decode(frame)
                if distances is None:
                    continue
                last_frame_time = time.monotonic()

                now = time.monotonic()
                if now - last_sample_time < 0.1:
                    continue
                last_sample_time = now
                distance_samples.append(distances)

                # Configure whether the remote is in front of or behind the rover.
                if mode is None and not calibrating and len(distance_samples) >= 5:
                    ogleft, ogright = calc_distances(distance_samples)
                    print(f"Left: {ogleft}, Right: {ogright}")
                    print("Turn the remote about 45 degrees left now.")
                    distance_samples = []
                    calibrating = True
                    time.sleep(2)
                    ser.reset_input_buffer()
                    rx_buffer.clear()
                    continue

                if mode is None and calibrating and len(distance_samples) >= 5:
                    left, right = calc_distances(distance_samples)
                    if left is not None and ogleft is not None:
                        if left < ogleft:
                            print("Remote in front")
                            mode = "front"
                        elif left > ogleft:
                            print("Remote in back")
                            mode = "back"
                        else:
                            print("Could not determine direction; try configuration again.")
                    distance_samples = []
                    continue

                if mode is not None and len(distance_samples) >= 10:
                    left, right = calc_distances(distance_samples)
                    print(f"Left: {left}, Right: {right}")
                    move_following(left, right, mode, ifr1, ifr2, leftMotor, rightMotor)
                    distance_samples = []

            if mode is not None and time.monotonic() - last_frame_time > 0.5:
                stop(leftMotor, rightMotor)
            time.sleep(0.001)

    except KeyboardInterrupt:
        print("Stopping rover")
    except (OSError, serial.SerialException) as e:
        print(f"[Hardware Reset Detected] {e}")
    finally:
        stop(leftMotor, rightMotor)
        ifr1.close()
        ifr2.close()
        if leftMotor is not None:
            leftMotor[0].close()
            leftMotor[1].close()
            rightMotor[0].close()
            rightMotor[1].close()
        if ser is not None and ser.is_open:
            ser.close()


if __name__ == "__main__":
    main()
