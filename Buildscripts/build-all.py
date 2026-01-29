import os
import subprocess
import shutil
import time

def build(device: str) -> bool:
    print(f"Building {device}...")
    shutil.rmtree(os.path.join('Firmware', 'Generated'), ignore_errors=True)
    result = subprocess.run(['python', 'device.py', device], capture_output=True, text=True)
    if result.returncode != 0:
        print(f"Failed to select device {device}")
        return False
    result = subprocess.run(['idf.py', f"-Bbuild-all-{device}", 'build'], capture_output=True, text=True)
    if result.returncode != 0:
        print(f"Build failed for {device}:")
        print(result.stdout)
        print(result.stderr)
    return result.returncode == 0

def main():
    start_time = time.time()
    buildscripts_dir = 'Devices'
    if not os.path.exists(buildscripts_dir):
        print(f"Directory '{buildscripts_dir}' not found.")
        return

    for item in os.listdir(buildscripts_dir):
        item_path = os.path.join(buildscripts_dir, item)
        if os.path.isdir(item_path) and item != 'simulator':
            if not build(item):
                return

    print("All builds succeeded.")
    end_time = time.time()
    print(f"Elapsed time: {end_time - start_time:.2f} seconds")

if __name__ == "__main__":
    main()
