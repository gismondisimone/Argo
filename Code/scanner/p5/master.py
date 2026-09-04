import serial #type:ignore
import subprocess as sp
import time
import datetime
import os
from PIL import Image #type:ignore
import numpy as np #type:ignore
from hailo_platform import VDevice, FormatType #type:ignore

def ArcheoClassify(image, hef="/home/argo/ArcheoModel_Argo.hef"):
    img = Image.open(image).convert("RGB").resize((224, 224))
    in_data = np.array(img).astype(np.uint8)
    in_data = np.expand_dims(in_data, axis=0)
    in_data = np.ascontiguousarray(in_data)

    with VDevice() as target:
        infer_model = target.create_infer_model(hef)

        infer_model.output().set_format_type(FormatType.FLOAT32)

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

if __name__ == '__main__':
    classes = ["Candy", "Ceramic", "Other"]
    ser = serial.Serial('/dev/ttyACM0', 115200, timeout=1)
    ser.reset_input_buffer()

    while True:
        if ser.in_waiting > 0:
            line = ser.readline().decode('utf-8').rstrip()
            print(line)

            if line == "!StartScan":
                dir = f"/home/argo/Desktop/out/scan_{datetime.datetime.now().strftime('%Y_%m_%d__%H_%M')}/up/pos_00_side.jpg"

                sp.run(["python", "Desktop/scan.py"])

                if os.path.exists(dir):
                    try:
                        clas = ArcheoClassify(dir)
                        print(clas)

                        label = classes[clas[0]]

                        if label == "Ceramic":
                            ser.write(b"1")
                        elif label == "Candy":
                            ser.write(b"2")
                        else:
                            ser.write(b"3")

                    except Exception as e:
                        print(f"Errore AI: {e}")

                time.sleep(5)
                ser.write(b"aight\n")
