import subprocess as sp
import time
import datetime
import os
import requests #type:ignore
from PIL import Image #type:ignore
import numpy as np #type:ignore
from hailo_platform import VDevice, FormatType #type:ignore
from flask import Flask, request #type:ignore

app = Flask(__name__)

# Configurazioni
CLASSES = ["Candy", "Ceramic", "Other"]
HEF_PATH = "/home/argo/ArcheoModel_Argo.hef" #da cambiare
ESP_IP = "10.118.94.72"  # IP statico assegnato all'ESP32 #da cambiare

# Variabili globali per riutilizzare l'istanza Hailo
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
    """Invia una richiesta HTTP all'ESP32 per sbloccare il nastro (equivalente a inviare 'oc' o risposta ok)"""
    try:
        url = f"http://{ESP_IP}/"
        response = requests.get(url, timeout=5)
        print(f"Notifica inviata all'ESP32. Status code: {response.status_code}")
    except Exception as e:
        print(f"Errore durante l'invio della notifica all'ESP32: {e}")

@app.route('/mento', methods=['GET'])
def handle_scan_request():
    data = request.args.get('data')
    print(f"Richiesta ricevuta dall'ESP. Data: {data}")

    # Il "1" corrisponde al segnale di invio pezzo sul piatto
    if data == "1":
        print("Segnale '1' ricevuto. Avvio procedura di scansione...")
        
        dir_path = f"/home/argo/Desktop/out/scan_{datetime.datetime.now().strftime('%Y_%m_%d__%H_%M')}/up/pos_00_side.jpg"

        # Esecuzione dello script di scansione
        sp.run(["python3", "Desktop/scan.py"])

        if os.path.exists(dir_path):
            try:
                clas = ArcheoClassify(dir_path)
                print(f"Risultato classificazione: {clas}")

                label = CLASSES[clas[0]]
                print(f"Classe identificata: {label}")

            except Exception as e:
                print(f"Errore durante la classificazione AI: {e}")
        else:
            print(f"File immagine non trovato in: {dir_path}")

        time.sleep(2)
        
        # Invia il via libera all'ESP32 tramite HTTP
        notify_esp()

        return "OK", 200

    return "Ignorato", 200

if __name__ == '__main__':
    # Inizializza il modello AI una sola volta all'avvio
    init_hailo()
    
    # Avvia il server Flask in ascolto su tutte le interfacce (porta 80)
    print("Server in ascolto sulla porta 80...")
    app.run(host='0.0.0.0', port=80)