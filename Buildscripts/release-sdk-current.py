#!/usr/bin/env python3

import os
import platform
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

def get_version():
    try:
        with open("version.txt", "r") as f:
            return f.read().strip()
    except FileNotFoundError:
        print("version.txt not found")
        sys.exit(1)

def run_release_script(script_name, sdk_path):
    # Cleanup sdk_path
    if os.path.exists(sdk_path):
        print(f"Cleaning up {sdk_path}")
        shutil.rmtree(sdk_path)

    os.makedirs(sdk_path, exist_ok=True)

    # Note: Using sys.executable to ensure we use the same python interpreter
    script_path = os.path.join("Buildscripts", script_name)
    print(f"Running {script_path} {sdk_path}")

    result = subprocess.run([sys.executable, script_path, sdk_path])

    if result.returncode != 0:
        print(f"Error: {script_path} failed with return code {result.returncode}")
        sys.exit(result.returncode)

def main():
    version = get_version()

    # ESP_IDF_VERSION is only set once an ESP-IDF environment has been activated (export.sh /
    # the Windows PowerShell profile - see building.md); same check release-sdk-esp32.py and
    # release-sdk-posix.py themselves use to tell the two builds apart.
    esp_idf_version = os.environ.get("ESP_IDF_VERSION", "")

    if esp_idf_version:
        idf_target = get_idf_target()
        if not idf_target:
            print("Could not determine IDF target from sdkconfig")
            sys.exit(1)
        sdk_path = os.path.join("release", "sdk", version, f"platform-{idf_target}", "TactilitySDK")
        run_release_script("release-sdk-esp32.py", sdk_path)
    else:
        arch = platform.machine()
        host_os = "macos" if platform.system() == "Darwin" else "linux"
        sdk_path = os.path.join("release", "sdk", version, f"platform-{host_os}-{arch}", "TactilitySDK")
        run_release_script("release-sdk-posix.py", sdk_path)

if __name__ == "__main__":
    main()
