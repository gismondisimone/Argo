import json
import subprocess as sp
import time
import datetime
import os
import threading
import urllib.request as urlrequest
import requests #type:ignore
from PIL import Image #type:ignore
import numpy as np #type:ignore
from hailo_platform import VDevice, FormatType #type:ignore
from flask import Flask, request #type:ignore

app = Flask(__name__)

# Configurazioni
CLASSES = ["Candy", "Ceramic", "Bolt", "Other"]
HEF_PATH = "ArcheoModel_Argo.hef" #da cambiare
ESP_IP = "10.118.94.72"  # IP statico assegnato all'ESP32 #da cambiare
ESP_IP = "mento.local"
PORT = 5000
#setup P0
p0_ip = "argoslave.local"
p0_port = 8765
p0_url = f"http://{p0_ip}:{p0_port}"
current_bin = 0
state = "idle"
scan_lock = threading.Lock()

def p0_get(path):
    with urlrequest.urlopen(p0_url + path, timeout=5) as response:
        return json.loads(response.read().decode("utf-8"))


def p0_post(path, data=None, timeout=30):
    if data is None:
        data = {}

    data = json.dumps(data).encode("utf-8")
    req = urlrequest.Request(
        p0_url + path,
        data=data,
        headers={"Content-Type": "application/json"},
        method="POST"
    )

    with urlrequest.urlopen(req, timeout=timeout) as response:
        return json.loads(response.read().decode("utf-8"))

def rotate_bins(diff):
    p0_post("/rotate_bins", {"degrees": diff})

def paddle():
    p0_post("/paddle")

# Variabili globali per riutilizzare Hailo
vdevice = None
infer_model = None

def init_hailo():
    global vdevice, infer_model
    vdevice = VDevice()
    infer_model = vdevice.create_infer_model(HEF_PATH)
    infer_model.output().set_format_type(FormatType.FLOAT32)

def ArcheoClassify(image_path):
    img = Image.open(image_path).convert("RGB").resize((224, 224))
    in_data = np.array(img).astype(np.uint8)
    in_data = np.expand_dims(in_data, axis=0)
    in_data = np.ascontiguousarray(in_data)

    with infer_model.configure() as model:
        bindings = model.create_bindings()

        input_name = infer_model.input_names[0]
        output_name = infer_model.output_names[0]

        output_buffer = np.empty(
            infer_model.output(output_name).shape,
            dtype=np.float32
        )

        bindings.input(input_name).set_buffer(in_data)
        bindings.output(output_name).set_buffer(output_buffer)

        model.activate()
        model.run([bindings], timeout=10000)
        model.deactivate()

        predictions = bindings.output(output_name).get_buffer().flatten()

        class_id = np.argmax(predictions)
        conf = predictions[class_id]
        return class_id, conf

def notify_esp():
    try:
        url = f"http://{ESP_IP}/"
        response = requests.get(url, timeout=5)
        print(f"ESP notified, status code: {response.status_code}")
    except Exception as e:
        print(f"Error notifying ESP: {e}")

@app.route('/status', methods=['GET'])
def status():
    return {"state": state}, 200

@app.route('/mento', methods=['GET'])
def handle_scan_request():
    global current_bin, state
    data = request.args.get('data')
    print(f"Request from ESP: {data}")

    if data == "1":
        if not scan_lock.acquire(blocking=False):
            print("Already scanning, ignoring request.")
            return "BUSY", 200
        try:
            print("Signal '1' received. Starting scanning procedure...")
            state = "scanning"

            dir_path = f"/home/argo/Desktop/out/scan_{datetime.datetime.now().strftime('%Y_%m_%d__%H_%M')}/up/pos_00_side.jpg"

            sp.run(["python3", "Desktop/scan.py"])

            if os.path.exists(dir_path):
                try:
                    state = "classifying"
                    clas = ArcheoClassify(dir_path)
                    print(f"Result of classification: {clas}")

                    label = CLASSES[clas[0]]
                    if label == "Candy":
                        target_bin = 0
                    elif label == "Ceramic":
                        target_bin = 1
                    else:
                        target_bin = 2
                    print(f"Class identified: {label}")
                    if target_bin != current_bin:
                        rotate_bins(target_bin - current_bin)

                    current_bin = target_bin
                    paddle()
                    print("Finish")

                except Exception as e:
                    print(f"Error during classification: {e}")
            else:
                print(f"File not found in: {dir_path}")

            time.sleep(2)

            notify_esp()
        finally:
            state = "idle"
            scan_lock.release()

        return "OK", 200

    return "Ignored", 200

if __name__ == '__main__':
    init_hailo()

    print(f"Server listening in port: {PORT}...")
    app.run(host='0.0.0.0', port=PORT)