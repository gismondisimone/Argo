import time
import threading
import requests #type:ignore
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
ESP_HOST = "mento.local"   # ESP del buffer (nome mDNS, niente IP statico)
PORT = 5000
maxx = 31  # same as maxx in the buffer ESP sketch

def set_motors(left_speed, right_speed, left_motor, right_motor):
    left_motor[0].value = abs(left_speed)
    right_motor[0].value = abs(right_speed)

    # The ZS-Z11H DIR input is active low for reverse.
    left_motor[1].value = left_speed >= 0
    right_motor[1].value = right_speed >= 0

def stop_motors(left_motor, right_motor):
    left_motor[0].off()
    right_motor[0].off()

def go(dir):
    match dir:
        case "on": set_motors(speed, -speed, left_motor, right_motor)
        case "back": set_motors(-speed, speed, left_motor, right_motor)
        case "right": set_motors(speed+0.15, speed-0.15, left_motor, right_motor)
        case "left": set_motors(-speed+0.15, -speed-0.15, left_motor, right_motor)

# remote button name -> go() direction
GO = {"left": "left", "ahead": "on", "back": "back", "right": "right"}
NAMES = {"left": "LEFT", "ahead": "AHEAD", "back": "BACK", "right": "RIGHT"}

percent = 0
conveyor_full = False
drive_dir = None
last_cmd = 0.0
blocked_until = 0.0
lock = threading.Lock()

def notify_esp():
    try:
        url = f"http://{ESP_HOST}/"
        response = requests.get(url, timeout=5)
        print(f"Notifica inviata all'ESP32. Status code: {response.status_code}")
    except Exception as e:
        print(f"Errore durante l'invio della notifica all'ESP32: {e}")

def display_line():
    # "status,percent,text" -> status 0 idle, 1 moving, 2 alert
    if conveyor_full:
        s, t = 2, "CONVEYOR FULL!"
    elif time.time() < blocked_until:
        s, t = 2, "OBSTACLE"
    elif drive_dir:
        s, t = 1, "MOVING " + NAMES[drive_dir]
    else:
        s, t = 0, "IDLE"
    return f"{s},{percent},{t}"

@app.route('/display', methods=['GET'])
def display():
    return display_line(), 200

@app.route('/drive', methods=['GET'])
def drive():
    global drive_dir, last_cmd, blocked_until
    d = request.args.get('dir', 'stop')
    with lock:
        if d in GO:
            if d == "ahead" and (ifr1.is_active or ifr2.is_active):
                stop_motors(left_motor, right_motor)
                drive_dir = None
                blocked_until = time.time() + 2
                print("----obstacle detected----")
            else:
                go(GO[d])
                drive_dir = d
                last_cmd = time.time()
                print(f"Moving {d}")
        else:
            stop_motors(left_motor, right_motor)
            drive_dir = None
            print("Stopping.")
        return display_line(), 200

@app.route('/mento', methods=['GET'])
def buffer_status():
    global percent, conveyor_full
    data = request.args.get('data')
    print(f"Richiesta ricevuta dall'ESP. Data: {data}")

    if data == "2000":
        print("Segnale '2000' ricevuto.")
        percent = 100
        if not conveyor_full:
            conveyor_full = True
            threading.Thread(target=notify_esp, daemon=True).start()
        return "OK", 200

    try:
        percent = min(100, int(data) * 100 // maxx)
    except (TypeError, ValueError):
        return "Bad data", 400
    conveyor_full = False
    return "OK", 200

def watchdog():
    # safety: if the remote goes silent (WiFi lost) the rover stops; also stops when an obstacle appears while going ahead
    global drive_dir, blocked_until
    while True:
        with lock:
            if drive_dir:
                if time.time() - last_cmd > 0.6:
                    stop_motors(left_motor, right_motor)
                    drive_dir = None
                    print("No command from the remote. Stopping.")
                elif drive_dir == "ahead" and (ifr1.is_active or ifr2.is_active):
                    stop_motors(left_motor, right_motor)
                    drive_dir = None
                    blocked_until = time.time() + 2
                    print("----obstacle detected----")
        time.sleep(0.05)

if __name__ == '__main__':
    threading.Thread(target=watchdog, daemon=True).start()
    print(f"Server in ascolto sulla porta {PORT}...")
    try:
        app.run(host='0.0.0.0', port=PORT)
    finally:
        print("Interrupted, cleaning up...")
        stop_motors(left_motor, right_motor)
        for device in (*left_motor, *right_motor, ifr1, ifr2):
            device.close()