#!/usr/bin/env python3

import os
import shutil
import subprocess
import sys

def get_idf_target():
    try:
        with open("sdkconfig", "r") as f:
            for line in f:
                if line.startswith("CONFIG_IDF_TARGET="):
                    # CONFIG_IDF_TARGET="esp32s3" -> esp32s3
                    return line.split('=')[1].strip().strip('"')
    except FileNotFoundError:
        print("sdkconfig not found")
        return None
    return None

def main():
    # 1. Get idf_target
    idf_target = get_idf_target()
    if not idf_target:
        print("Could not determine IDF target from sdkconfig")
        sys.exit(1)

    # 2. Get version
    try:
        with open("version.txt", "r") as f:
            version = f.read().strip()
    except FileNotFoundError:
        print("version.txt not found")
        sys.exit(1)

    # 3. Construct sdk_path
    # release/TactilitySDK/${version}-${idf_target}/TactilitySDK
    sdk_path = os.path.join("release", "TactilitySDK", f"{version}-{idf_target}", "TactilitySDK")
    
    # 4. Cleanup sdk_path
    if os.path.exists(sdk_path):
        print(f"Cleaning up {sdk_path}")
        shutil.rmtree(sdk_path)
    
    os.makedirs(sdk_path, exist_ok=True)

    # 5. Call release-sdk.py
    # Note: Using sys.executable to ensure we use the same python interpreter
    script_path = os.path.join("Buildscripts", "release-sdk.py")
    print(f"Running {script_path} {sdk_path}")
    
    result = subprocess.run([sys.executable, script_path, sdk_path])
    
    if result.returncode != 0:
        print(f"Error: {script_path} failed with return code {result.returncode}")
        sys.exit(result.returncode)

if __name__ == "__main__":
    main()
