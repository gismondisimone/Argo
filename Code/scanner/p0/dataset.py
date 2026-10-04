import time
import json
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from gpiozero import OutputDevice, Button #type:ignore

#setup stepper
pin_piatto = [
    OutputDevice(17),
    OutputDevice(27),
    OutputDevice(22),
    OutputDevice(23),
]
pin_cam = [
    OutputDevice(24),
    OutputDevice(25),
    OutputDevice(5),
    OutputDevice(6),
]
# da cambiare
pin_bin = [
    OutputDevice(12),
    OutputDevice(13),
    OutputDevice(26),
    OutputDevice(16),
]

#main setup
server_ip = "0.0.0.0"
server_port = 8765

plate_sequence = [3, 1, 2, 0]
cam_sequence = [0, 2, 1, 3]
cam_reset_sequence = cam_sequence[::-1] #reverse #[3, 1, 2, 0]

plate_step_index = 0
cam_step_index = 0

plate_error = 0
cam_error = 0

step_angle = 1.8
plate_gear = 4
cam_gear = 9

def cleanup():
    for pin in pin_piatto:
        pin.off()

    for pin in pin_cam:
        pin.off()

def cleanup_plate():
    for pin in pin_piatto:
        pin.off()

def cleanup_cam():
    for pin in pin_cam:
        pin.off()

def rotate_motor(pins, sequence, step_index, degrees, gear_ratio, error):
    exact_steps = (degrees * gear_ratio / step_angle) + error
    steps = int(exact_steps + 1e-9)
    error = exact_steps - steps

    for _ in range(steps):
        active_pin = sequence[step_index % len(sequence)]

        for n, pin in enumerate(pins):
            if n == active_pin:
                pin.on()
            else:
                pin.off()

        step_index += 1
        time.sleep(0.01)

    return step_index, error, steps

def rotate_plate(degrees=10):
    global plate_step_index, plate_error

    plate_step_index, plate_error, steps = rotate_motor(
        pin_piatto,
        plate_sequence,
        plate_step_index,
        degrees,
        plate_gear,
        plate_error
    )

    cleanup_plate()
    return steps


def rotate_cam(degrees=1):
    global cam_step_index, cam_error

    cam_step_index, cam_error, steps = rotate_motor(
        pin_cam,
        cam_sequence,
        cam_step_index,
        degrees,
        cam_gear,
        cam_error
    )

    cleanup_cam()
    return steps


def reset_cam(degrees=45):
    global cam_step_index, cam_error

    cam_step_index, cam_error, steps = rotate_motor(
        pin_cam,
        cam_reset_sequence,
        cam_step_index,
        degrees,
        cam_gear,
        cam_error
    )

    cleanup_cam()
    return steps


def test_cam(steps=200):
    global cam_step_index

    for _ in range(steps):
        active_pin = cam_sequence[cam_step_index % len(cam_sequence)]

        for n, pin in enumerate(pin_cam):
            if n == active_pin:
                pin.on()
            else:
                pin.off()

        cam_step_index += 1
        time.sleep(0.02)

    cleanup_cam()

class Server(BaseHTTPRequestHandler):
    def log_message(self, format, *args):
        return

    def send_data(self, code, data):
        data = json.dumps(data).encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def read_data(self):
        size = int(self.headers.get("Content-Length", "0"))
        if size == 0:
            return {}

        return json.loads(self.rfile.read(size).decode("utf-8"))

    def do_POST(self):
        try:
            data = self.read_data()

            if self.path == "/rotate_plate":
                steps = rotate_plate(float(data.get("degrees", 10)))
                self.send_data(200, {"ok": True, "steps": steps})

            elif self.path == "/rotate_cam":
                steps = rotate_cam(float(data.get("degrees", 1)))
                self.send_data(200, {"ok": True, "steps": steps})

            elif self.path == "/reset_cam":
                steps = reset_cam(float(data.get("degrees", 45)))
                self.send_data(200, {"ok": True, "steps": steps})

            elif self.path == "/test_cam":
                steps = int(data.get("steps", 200))
                test_cam(steps)
                self.send_data(200, {"ok": True, "steps": steps})

            elif self.path == "/cleanup":
                cleanup()
                self.send_data(200, {"ok": True})

            else:
                self.send_data(404, {"error": "not found"})

        except Exception as error:
            cleanup()
            self.send_data(500, {"error": str(error)})


cleanup()
server = ThreadingHTTPServer((server_ip, server_port), Server)
print(f"P0 motor server listening on port {server_port}")

try:
    server.serve_forever()
except KeyboardInterrupt:
    print("\nStopping motor server")
finally:
    cleanup()
    server.server_close()