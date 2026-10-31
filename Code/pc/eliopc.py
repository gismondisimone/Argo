import os
import time
import subprocess
import shutil

WATCH_DIR = "pi_receive"
OUTPUT_DIR = "3d"

def reconstruct(folder, startname):
    base_argo = "C:\\Users\\simon\\Desktop\\Argo"
    file = os.path.join(base_argo, WATCH_DIR, folder)
    os.mkdir(f"C:\\Users\\simon\\Desktop\\Argo\\3d\\{startname}_data\\3d")
    os.mkdir(f"C:\\Users\\simon\\Desktop\\Argo\\3d\\{startname}_data\\3d\\temp")
    treD_f = f"C:\\Users\\simon\\Desktop\\Argo\\3d\\{startname}_data\\3d\\temp"
    
    os.makedirs(treD_f, exist_ok=True)
    os.chdir(treD_f)
    
    # Init
    subprocess.run(["openMVG_main_SfMInit_ImageListing.exe", "-i", file, "-o", ".", "-c", "3", "-f", "2300"])
    time.sleep(1)
    subprocess.run(["openMVG_main_ComputeFeatures.exe", "-i", "sfm_data.json", "-o", ".", "-p", "HIGH"]) #cambio
    subprocess.run(["openMVG_main_ComputeMatches.exe", "-i", "sfm_data.json", "-o", "matches.putative.bin", "-f", "1"])
    subprocess.run(["openMVG_main_GeometricFilter.exe", "-i", "sfm_data.json", "-m", "matches.putative.bin", "-o", "matches.f.bin"])
    
    # SfM
    os.makedirs("reconstruction", exist_ok=True)
    subprocess.run(["openMVG_main_SfM.exe", "-i", "sfm_data.json", "-m", ".", "-o", "reconstruction"])
    
    # Export e OpenMVS
    sfm_bin = os.path.join("reconstruction", "sfm_data.bin")
    if os.path.exists(sfm_bin): #cambio
        subprocess.run(["openMVG_main_openMVG2openMVS.exe", "-i", sfm_bin, "-o", "scene.mvs", "-d", file])
        
        mvs_path = r"C:\Argo\openMVS\bu1ld\bin\vc17\x64\Release"
        subprocess.run([f"{mvs_path}\\DensifyPointCloud.exe", "scene.mvs"])
        subprocess.run([f"{mvs_path}\\ReconstructMesh.exe", "scene_dense.mvs"])
        subprocess.run([f"{mvs_path}\\TextureMesh.exe", "scene_dense_mesh.mvs"])
    
    txtr_ply = os.path.join("C:\\Users\\simon\\Desktop\\Argo\\3d", f"{startname}_data", "3d", "temp", "scene_dense.ply")
    cloud_ply = os.path.join("C:\\Users\\simon\\Desktop\\Argo\\3d", f"{startname}_data", "3d", "temp", "reconstruction", "cloud_and_poses.ply")
    mesh_ply = os.path.join("C:\\Users\\simon\\Desktop\\Argo\\3d", f"{startname}_data", "3d", "temp", "scene_dense_mesh.ply")
    if os.path.exists(txtr_ply):
        shutil.move(txtr_ply, os.path.join(base_argo, OUTPUT_DIR, f"{startname}_data", "3d"))
    if os.path.exists(cloud_ply):
        shutil.move(cloud_ply, os.path.join(base_argo, OUTPUT_DIR, f"{startname}_data", "3d"))
    if os.path.exists(mesh_ply):
        shutil.move(mesh_ply, os.path.join(base_argo, OUTPUT_DIR, f"{startname}_data", "3d"))

while True:
    os.chdir("C:\\Users\\simon\\Desktop\\Argo")
    print("watchin")
    if os.path.exists(os.path.join(WATCH_DIR, "done.txt")):
        folder_name = next((d for d in os.listdir(WATCH_DIR) if os.path.isdir(os.path.join(WATCH_DIR, d)) and not d.endswith("_nobg")), None)
        
        if folder_name:
            in_p = os.path.join(WATCH_DIR, folder_name)
            output_folder = f"{folder_name}_nobg"
            out_p = os.path.join(WATCH_DIR, output_folder)
            os.makedirs(out_p, exist_ok=True)
            
            print("Flag detected! Starting 3D reconstruction...")
            ti=time.time()
            os.remove(os.path.join(WATCH_DIR, "done.txt"))
            
            subprocess.run(["rembg", "p", "-m", "birefnet-general-lite", in_p, out_p])
            reconstruct(output_folder, folder_name)
            
            
            os.chdir("C:\\Users\\simon\\Desktop\\Argo")
            target_dir = os.path.join("C:\\Users\\simon\\Desktop\\Argo\\3d", f"{folder_name}_data")
            os.makedirs(target_dir, exist_ok=True)
            shutil.move(os.path.join(WATCH_DIR, folder_name), target_dir)
            shutil.move(os.path.join(WATCH_DIR, folder_name + "_nobg"), target_dir)
            
            print("Model finished! Ready for next scan.")
            tf = time.time() - ti
            print(f"Total time taken: {tf:.2f} seconds")
    
    time.sleep(5)