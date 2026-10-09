import datetime
import time
import subprocess
import random
import os
import cv2 #type:ignore
import json
from urllib import request

#setup P0
p0_ip = "argoslave.local"
p0_port = 8765
p0_url = f"http://{p0_ip}:{p0_port}"

#main setup
dir = datetime.datetime.now().strftime('%Y-%m-%d_%H-%M')
tot_s = 10 # 10° per step
out_f = f"/home/argo/Desktop/out/scan_{dir}"
data_f = f"{out_f}_data"
pc_u = "Pollo"
pc_ip = "10.234.61.77"    
pc_fs = "C:/Users/Pollo/Desktop/Argo/pi_receive"
pc_fd = "C:/Users/Pollo/Desktop/Argo/3d"
psw = "volpedaseta10"
paused = False

def p0_get(path):
    with request.urlopen(p0_url + path, timeout=5) as response:
        return json.loads(response.read().decode("utf-8"))


def p0_post(path, data=None, timeout=30):
    if data is None:
        data = {}

    data = json.dumps(data).encode("utf-8")
    req = request.Request(
        p0_url + path,
        data=data,
        headers={"Content-Type": "application/json"},
        method="POST"
    )

    with request.urlopen(req, timeout=timeout) as response:
        return json.loads(response.read().decode("utf-8"))


def cleanup():
    p0_post("/cleanup")

def rotate_plate():
    p0_post("/rotate_plate", {"degrees": 10})

def rotate_cam():
    p0_post("/rotate_cam", {"degrees": 1})

def send(f_paths, f_pathd, pc_ip, pc_u, c_paths, c_pathd):
    global out_f
    try:
        scan_path = c_paths.replace("\\", "/")
        data_path = c_pathd.replace("\\", "/")
        
        mkdir_cmd = ["sshpass", "-p", psw, "ssh", "-o", "StrictHostKeyChecking=no", f"{pc_u}@{pc_ip}", f'cmd /c "if not exist \"{scan_path}\" mkdir \"{scan_path}\""']
        subprocess.run(mkdir_cmd, check=True, timeout=30)

        scancmd = [
             "sshpass", "-p", psw,
             "scp", "-o", "StrictHostKeyChecking=no", "-r", f_paths,
             f"{pc_u}@{pc_ip}:{scan_path}"
        ]

        datacmd = [
             "sshpass", "-p", psw,
             "scp", "-o", "StrictHostKeyChecking=no", "-r", f_pathd,
             f"{pc_u}@{pc_ip}:{data_path}"
        ]

        print(f"Attempting to send to {pc_u}@{pc_ip}...")
        
        subprocess.run(scancmd, check=True, timeout=300)
        subprocess.run(datacmd, check=True, timeout=300)
        print(f"Files sent successfully to {pc_ip}, creating flag...")
        flag_path = f"{scan_path}/done.txt".replace("/", "\\")
        subprocess.run(["sshpass", "-p", psw, "ssh", f"{pc_u}@{pc_ip}", f'type nul > "{flag_path}"'], timeout=30)
        subprocess.run(["rm", "-rf", f_paths], check=True)
        subprocess.run(["rm", "-rf", f_pathd], check=True)
        print("Deleted local directories")
        print(f"Files sent successfully to {pc_ip}")
    except subprocess.TimeoutExpired:
        print(f"Error: Connection to {pc_ip} timed out. Check if host is reachable and SSH is running.")
    except subprocess.CalledProcessError as e:
        print(f"Error sending files: {e}")
        print(f"Verify: 1) Host {pc_ip} is online")
        print(f"        2) SSH service is running on {pc_ip}")
        print(f"        3) Firewall allows port 22")
        print(f"        4) Path {c_paths} exists on remote machine")

os.makedirs(out_f, exist_ok=True)
os.makedirs(data_f, exist_ok=True)
with open(f"{data_f}/data.txt", "w") as file:
    file.write(f"""data example.
  humidity = {random.uniform(0, 100)}%
  porosity = {random.uniform(0.2, 1):.2f}

  position(x,y,z) = ({random.uniform(-10, 10):.3f}, {random.uniform(-10, 10):.3f}, {random.uniform(-10, 10):.3f})
    """)
print(f"made dir:{dir}")

s_time = time.time()

print("scannin")

for i in range(tot_s):

    print(f"step {i+1} of {tot_s}")
    cleanup()
    rotate_plate()
    rotate_cam()
    cleanup()
    print("rotated")
    time.sleep(1.0)

    if i <= 9:
        n = "0" + str(i)
    else:
        n = str(i)

    path = f"{out_f}/pos_{n}_side.jpg"
    subprocess.run([
        "rpicam-still", "-t", "500", "--camera", "0", "-o", path, "--brightness", "-0.2", "--ev", "0.1", "--saturation", "0.9", "--contrast", "1.4", "--awbgains", "1.4,2.0"
    ])
    img = cv2.imread(path)
    if img is not None:
        img = cv2.rotate(img, cv2.ROTATE_180)
        cv2.imwrite(path, img)

print("scan complete")
send(out_f, data_f, pc_ip, pc_u, pc_fs, pc_fd)
cleanup()

p0_post("/reset_cam", {"degrees": 10})
t_time = time.time() - s_time
print(f"took {round(t_time, 2)} seconds")
print("change piece to scan")