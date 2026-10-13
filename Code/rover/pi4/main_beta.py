import serial # type: ignore
import struct
import time
import threading
import requests #type:ignore
from urllib import request
from gpiozero import DigitalInputDevice, DigitalOutputDevice, PWMOutputDevice #type:ignore
from flask import Flask, request #type:ignore

app = Flask(__name__)

#infrared
ifr1 = DigitalInputDevice(23, pull_up=True) #dacambiare
ifr2 = DigitalInputDevice(25, pull_up=True) #dacambiare
#belts
Lpwm = 4
Ldir = 17
Rpwm = 27
Rdir = 22
speed = 0.3
left_motor = (
    PWMOutputDevice(Lpwm, frequency=1000, initial_value=0),
    DigitalOutputDevice(Ldir, initial_value=True),
)
right_motor = (
    PWMOutputDevice(Rpwm, frequency=1000, initial_value=0),
    DigitalOutputDevice(Rdir, initial_value=True),
)
#ESP
ESP_IP = "10.118.94.72"

error = 0.15

def set_motors(left_speed, right_speed, left_motor, right_motor):
    left_motor[0].value = abs(left_speed)
    right_motor[0].value = abs(right_speed)

    # The ZS-Z11H DIR input is active low for reverse.
    left_motor[1].value = left_speed >= 0
    right_motor[1].value = right_speed >= 0

def stop_motors(left_motor, right_motor):
    left_motor[0].off()
    right_motor[0].off()

def run_step(label, left_speed, right_speed, duration, left_motor, right_motor):
    print(label)
    set_motors(left_speed, right_speed, left_motor, right_motor)
    time.sleep(duration)
    stop_motors(left_motor, right_motor)
    time.sleep(0.5)

def go(dir):
    match dir:
        case "on": set_motors(speed, -speed, left_motor, right_motor)
        case "back": set_motors(-speed, speed, left_motor, right_motor)
        case "right": set_motors(speed+0.15, speed-0.15, left_motor, right_motor)
        case "left": set_motors(-speed+0.15, -speed-0.15, left_motor, right_motor)

def get_serial_port():
    while True:
        try:
            s = serial.Serial('/dev/ttyACM0', baudrate=115200, timeout=1)
            print("Connected to serial port.")
            return s
        except Exception as e:
            print(f"Waiting for serial port... ({e})")
            time.sleep(2)

ser = get_serial_port()
ser_tx = serial.Serial('/dev/serial0', baudrate=115200)  # Pi GPIO14 (TXD) -> base RX1 (PA10)
rx_buffer = bytearray()
HEADER = b'\xaa%\x01'
FRAME_LENGTH = 35

percent = 0
maxx = 31  # same as maxx in the ESP sketch
tx_lock = threading.Lock()
last_key_id = None
move_status = "move"

def notify_esp():
    try:
        url = f"http://{ESP_IP}/"
        response = requests.get(url, timeout=5)
        print(f"Notifica inviata all'ESP32. Status code: {response.status_code}")
    except Exception as e:
        print(f"Errore durante l'invio della notifica all'ESP32: {e}")

@app.route('/mento', methods=['GET'])
def buffer_status():
    global percent
    data = request.args.get('data')
    print(f"Richiesta ricevuta dall'ESP. Data: {data}")

    if data == "2000":
        print("Segnale '2000' ricevuto.")

        send_display(2, 100)        
        notify_esp()

        return "OK", 200
    else:
        try:
            percent = min(100, int(data) * 100 // maxx)
        except (TypeError, ValueError):
            return "Bad data", 400
        send_display(0, percent)

    return "Ignored", 200

def send_display(status, percent):
    with tx_lock:
        ser_tx.write(bytes([0xAA, 0x25, 0x02, status, int(percent)]))

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

# the /mento route only answers if the web server is actually running
threading.Thread(target=lambda: app.run(host='0.0.0.0', port=5000), daemon=True).start()

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
                    run_step("Turning 45 deg left",-speed,-speed-0.2,0.75,left_motor,right_motor)
                    run_step("Going back",speed,speed+0.2,0.75,left_motor,right_motor)
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
                    print(f"new Left: {left}, new Right: {right}")
                    if left is not None and ogleft is not None:
                        if (float(left) < float(ogleft)):
                            print("----Remote in front----")
                            mode = "front"
                            break
                        elif (float(left) > float(ogleft)):
                            print("----Remote in back----")
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
    except (KeyboardInterrupt):
        print("Interrupted, cleaning up...")
        stop_motors(left_motor, right_motor)
        for device in (*left_motor, *right_motor):
            device.close()
        raise

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

                if last_key_id is None:
                    last_key_id = frame[12]
                elif frame[12] != last_key_id:
                    last_key_id = frame[12]
                    print(f"Button {frame[11]} pressed")
                    match frame[11]:
                        case 1: move_status = "move"
                        case 2: move_status = "stop"
                        case 3: move_status = "home"
                        case 4: move_status ="freaky"
                
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
                        print(f"Infrared 1: {ifr1.is_active}, Infrared 2: {ifr2.is_active}")
                        if move_status == "move":
                            if left is not None and right is not None:
                                if not ifr1.is_active and not ifr2.is_active:
                                    if float(left) > error and float(right) > error:
                                        send_display(1, percent)
                                        if (float(left) - float(right)) < -error:
                                            if mode == "front": print("Turning Left"); go("left")
                                            else: print("Turning Right"); go("right")
                                        elif (float(left) - float(right)) > error:
                                            if mode == "front": print("Turning Right"); go("right")
                                            else: print("Turning Left"); go("left")
                                        else:
                                            if mode == "front": print("Going straight"); go("on")
                                            else: print("Going back"); go("back")
                                    else:
                                        send_display(1, percent)
                                        print("Too close to user. Stopping.")
                                else:
                                    send_display(1, percent)
                                    print("----obstacle detected----")
                                    stop_motors(left_motor, right_motor)
                            else:
                                send_display(0, percent)
                                print("One or both distances are not visible.")
                        elif move_status == "stop":
                            send_display(0, percent)
                            print("Stopping.")
                            stop_motors(left_motor, right_motor)
                        elif move_status == "home":
                            send_display(1, percent)
                            print("Going home.")
                            stop_motors(left_motor, right_motor)
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
    except (KeyboardInterrupt):
        print("Interrupted, cleaning up...")
        stop_motors(left_motor, right_motor)
        for device in (*left_motor, *right_motor):
            device.close()
        raise

    time.sleep(0.001)