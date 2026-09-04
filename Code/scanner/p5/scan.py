import datetime
import time
import subprocess
import os
import cv2 #type:ignore
import json
from urllib import request


#setup P0
p0_ip = "10.176.43.149"
p0_port = 8765
p0_url = f"http://{p0_ip}:{p0_port}"

#main setup
dir = datetime.datetime.now().strftime('%Y_%m_%d__%H_%M')
tot_s = 36 # 10° per step
out_f = f"/home/argo/Desktop/out/scan_{dir}"
data_f = f"{out_f}_data"
pc_u = "simon"
pc_ip = "10.176.43.214"
pc_fs = "C:/Users/simon/Desktop/Argo/pi_receive"
pc_fd = "C:/Users/simon/Desktop/Argo/3d"
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


def check_status():
    global paused

    status = p0_get("/status")

    # 1.check stop
    if status["stop"]:
        print("Stopped by user")
        cleanup()
        os._exit(0) #close

    # 2.check pause
    if status["pause"]:
        if not paused:
            print("Paused. Waiting for user...")
            paused = True

    while paused:
        status = p0_post("/wait_pause", timeout=None)

        #stop while paused
        if status["button"] == "stop":
            cleanup()
            os._exit(0)

        #check play
        if status["button"] == "play":
            print("Continuing")
            paused = False
            break


def rotate_plate():
    p0_post("/rotate_plate", {"degrees": 10})

def rotate_cam():
    p0_post("/rotate_cam", {"degrees": 1})

def rotate_bins():
    p0_post("/rotate_bins", {"degrees": 10})

def send(f_paths, f_pathd, pc_ip, pc_u, c_paths, c_pathd):
    global out_f
    try:
        scan_path = c_paths.replace("\\", "/")
        scan_dir = os.path.basename(f_paths)
        scan_pathup = os.path.join(scan_path, scan_dir, "up")
        scan_pathdown = os.path.join(scan_path, scan_dir, "down")
        data_path = c_pathd.replace("\\", "/")

        psw = "albalilli60"
        scanupcmd = [
             "sshpass", "-p", psw,
             "scp", "-o", "StrictHostKeyChecking=no", "-r", f_paths + "/up",
             f"{pc_u}@{pc_ip}:{scan_pathup}"
        ]

        scandowncmd = [
             "sshpass", "-p", psw,
             "scp", "-o", "StrictHostKeyChecking=no", "-r", f_paths + "/down",
             f"{pc_u}@{pc_ip}:{scan_pathdown}"
        ]

        datacmd = [
             "sshpass", "-p", psw,
             "scp", "-o", "StrictHostKeyChecking=no", "-r", f_pathd,
             f"{pc_u}@{pc_ip}:{data_path}"
        ]

        print(f"Attempting to send to {pc_u}@{pc_ip}...")
        
        subprocess.run(scanupcmd, check=True, timeout=30)
        subprocess.run(datacmd, check=True, timeout=30)
        print(f"Files sent successfully to {pc_ip}, creating flag...")
        with open("done.txt", "w") as f:
             pass
        flag_path = os.path.join(scan_path, "done.txt").replace("/", "\\")
        subprocess.run(["sshpass", "-p", psw, "ssh", f"{pc_u}@{pc_ip}", f'type nul > "{flag_path}"'], timeout=30)
        subprocess.run(scandowncmd, check=True, timeout=30)
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
os.makedirs(out_f + "/up", exist_ok=True)
os.makedirs(out_f + "/down", exist_ok=True)
os.makedirs(data_f, exist_ok=True)
with open(f"{data_f}/data.txt", "w") as file:
    file.write("""data example.
  humidity = 45%
  porosity = 0.87

  position(x,y,z) = (3.254, 34.650, -12.004)
    """)
print(f"made dir:{dir}")

s_time = time.time()

print("scannin")
p0_get("/health")

for i in range(tot_s):
    check_status()

    print(f"step {i+1} of {tot_s}")
    cleanup()
    rotate_plate()
    rotate_cam()
    cleanup()
    print("rotated")
    check_status()
    time.sleep(1.0)

    if i <= 9:
        n = "0" + str(i)
    else:
        n = str(i)

    # Postprocess camera 0: salvataggio, rotazione 180° via cv2 e riscrittura
    path_up = f"{out_f}/up/pos_{n}_side.jpg"
    subprocess.run([
        "rpicam-still", "-t", "500", "--camera", "0", "-o", path_up, "> /dev/null"
    ])
    img_up = cv2.imread(path_up)
    if img_up is not None:
        img_up = cv2.rotate(img_up, cv2.ROTATE_180)
        cv2.imwrite(path_up, img_up)

    subprocess.run([
        "rpicam-still", "-t", "500", "--camera", "1", "-o", f"{out_f}/down/pos_{n}_down.jpg", "> /dev/null"
    ])

print("scan complete")
send(out_f, data_f, pc_ip, pc_u, pc_fs, pc_fd)
cleanup()

p0_post("/reset_cam", {"degrees": 45})
t_time = time.time() - s_time
print(f"took {round(t_time, 2)} seconds")
