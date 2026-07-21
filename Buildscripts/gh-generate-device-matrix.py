import fnmatch
import json
import os
import sys

DEVICES_DIRECTORY = os.path.join(os.path.dirname(__file__), "..", "Devices")

IGNORE_LIST = {
    "simulator",
    "generic-*",
}


def is_ignored(device_id):
    return any(fnmatch.fnmatch(device_id, pattern) for pattern in IGNORE_LIST)

def read_properties_file(path):
    properties = {}
    with open(path, "r") as file:
        for line in file:
            stripped = line.strip()
            if not stripped or stripped.startswith("#"):
                continue
            key, sep, value = line.partition("=")
            if not sep:
                continue
            properties[key.strip()] = value.strip()
    return properties

def get_board_entries():
    boards = []
    for device_id in sorted(os.listdir(DEVICES_DIRECTORY)):
        if is_ignored(device_id):
            continue
        device_dir = os.path.join(DEVICES_DIRECTORY, device_id)
        properties_path = os.path.join(device_dir, "device.properties")
        if not os.path.isdir(device_dir) or not os.path.isfile(properties_path):
            continue
        properties = read_properties_file(properties_path)
        target = properties.get("hardware.target")
        if not target:
            sys.exit(f"ERROR: {properties_path} has no hardware.target property")
        boards.append({"id": device_id, "arch": target.lower()})
    return boards

def main():
    matrix_json = json.dumps({"board": get_board_entries()})

    github_output = os.environ.get("GITHUB_OUTPUT")
    if github_output:
        with open(github_output, "a") as file:
            file.write(f"matrix={matrix_json}\n")
    else:
        print(matrix_json)

if __name__ == "__main__":
    main()
