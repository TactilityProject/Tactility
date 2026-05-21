import subprocess
from datetime import datetime, UTC
import os
import sys
import configparser
from dataclasses import dataclass, asdict
import json
import shutil
from configparser import RawConfigParser

VERBOSE = False
DEVICES_FOLDER = "Devices"

@dataclass
class IndexEntry:
    id: str
    name: str
    vendor: str
    incubating: bool
    warningMessage: str
    infoMessage: str

@dataclass
class Manifest:
    name: str
    version: str
    new_install_prompt_erase: str
    funding_url: str
    builds: list

@dataclass
class ManifestBuild:
    chipFamily: str
    parts: list

@dataclass
class ManifestBuildPart:
    path: str
    offset: int

@dataclass
class DeviceIndex:
    version: str
    created: str
    gitCommit: str
    devices: list

shell_color_red = "\033[91m"
shell_color_orange = "\033[93m"
shell_color_reset = "\033[m"

def print_warning(message):
    print(f"{shell_color_orange}WARNING: {message}{shell_color_reset}")

def print_error(message):
    print(f"{shell_color_red}ERROR: {message}{shell_color_reset}")

def print_help():
    print("Usage: python generate-firmware-files.py [inPath] [outPath] [version]")
    print("     inPath   path with the extracted release files")
    print("     outPath  path where the CDN files will become available")
    print("     version  technical version name (e.g. 1.2.0)")
    print("Options:")
    print("  --verbose                      Show extra console output")

def exit_with_error(message):
    print_error(message)
    sys.exit(1)

def read_properties_file(path):
    config = configparser.RawConfigParser()
    # Don't convert keys to lowercase
    config.optionxform = str
    config.read(path)
    return config

def get_property_or_none(properties: RawConfigParser, group: str, key: str):
    if group not in properties.sections():
        return None
    if key not in properties[group].keys():
        return None
    return properties[group][key]

def get_boolean_property_or_false(properties: RawConfigParser, group: str, key: str):
    if group not in properties.sections():
        return False
    if key not in properties[group].keys():
        return False
    return properties[group][key] == "true"

def get_property_or_exit(properties: RawConfigParser, group: str, key: str):
    if group not in properties.sections():
        exit_with_error(f"Device properties does not contain group: {group}")
    if key not in properties[group].keys():
        exit_with_error(f"Device properties does not contain key: {key}")
    return properties[group][key]

def read_device_properties(device_id):
    mapping_file_path = os.path.join(DEVICES_FOLDER, device_id, "device.properties")
    if not os.path.isfile(mapping_file_path):
        exit_with_error(f"Mapping file not found: {mapping_file_path}")
    return read_properties_file(mapping_file_path)

def to_manifest_chip_name(name):
    if name == "esp32":
        return "ESP32"
    elif name == "esp32s2":
        return "ESP32-S2"
    elif name == "esp32s3":
        return "ESP32-S3"
    elif name == "esp32c2":
        return "ESP32-C2"
    elif name == "esp32c3":
        return "ESP32-C3"
    elif name == "esp32c5":
        return "ESP32-C5"
    elif name == "esp32c6":
        return "ESP32-C6"
    elif name == "esp32c61":
        return "ESP32-C61"
    elif name == "esp32h2":
        return "ESP32-H2"
    elif name == "esp32h4":
        return "ESP32-H4"
    elif name == "esp32p4":
        return "ESP32-P4"
    else:
        exit_with_error(f"to_manifest_chip_name() doesn't support {name} yet")
        return ""


def process_device(in_path: str, out_path: str, device_directory: str, device_id: str, device_properties: RawConfigParser, version: str):
    in_device_path = os.path.join(in_path, device_directory)
    in_device_binaries_path = os.path.join(in_device_path, "Binaries")
    if not os.path.isdir(in_device_binaries_path):
        exit_with_error(f"Could not find directory {in_device_binaries_path}")
    flasher_args_path = os.path.join(in_device_binaries_path, "flasher_args.json")
    if not os.path.isfile(flasher_args_path):
        exit_with_error(f"Could not find flasher arguments path {flasher_args_path}")
    with open(flasher_args_path) as json_data:
        flasher_args = json.load(json_data)
        flash_files = flasher_args["flash_files"]
        device_vendor = get_property_or_exit(device_properties, "general", "vendor")
        device_name = get_property_or_exit(device_properties, "general", "name")
        manifest = Manifest(
            name=f"Tactility for {device_vendor} {device_name}",
            version=version,
            new_install_prompt_erase="true",
            funding_url="https://github.com/sponsors/ByteWelder",
            builds=[
                ManifestBuild(
                    chipFamily=to_manifest_chip_name(flasher_args["extra_esptool_args"]["chip"]),
                    parts=[]
                )
            ]
        )
        for offset in flash_files:
            flash_file_entry = flash_files[offset]
            flash_file_entry_name = os.path.basename(flash_file_entry)
            in_flash_file_path = os.path.join(in_device_binaries_path, flash_file_entry)
            out_flash_file_name = f"{device_id}-{flash_file_entry_name}"
            out_flash_file_path = os.path.join(out_path, out_flash_file_name)
            if VERBOSE:
                print(f"Copying {in_flash_file_path} -> {out_flash_file_path}")
            shutil.copy(in_flash_file_path, out_flash_file_path)
            manifest.builds[0].parts.append(
                ManifestBuildPart(
                    path=out_flash_file_name,
                    offset=int(offset, 16)
                )
            )
        json_manifest_path = os.path.join(out_path, f"{device_id}.json")
        with open(json_manifest_path, 'w') as json_manifest_file:
            json.dump(asdict(manifest), json_manifest_file, indent=2)


def get_git_commit_hash():
    return subprocess.check_output(['git', 'rev-parse', 'HEAD']).decode('ascii').strip()

def main(in_path: str, out_path: str, version: str):
    if not os.path.exists(in_path):
        exit_with_error(f"Input path not found: {in_path}")
    if os.path.exists(out_path):
        shutil.rmtree(out_path)
    os.mkdir(out_path)
    artifact_directories = os.listdir(in_path)
    device_index = DeviceIndex(
        version=version,
        created=datetime.now(UTC).strftime('%Y-%m-%dT%H:%M:%S'),
        gitCommit=get_git_commit_hash(),
        devices=[]
    )
    for artifact_directory in artifact_directories:
        if artifact_directory.endswith("-symbols") or artifact_directory.endswith(".bin") or artifact_directory.startswith("TactilitySDK-"):
            continue
        device_id = artifact_directory.removeprefix("Tactility-")
        if not device_id:
            exit_with_error(f"Cannot derive device id from directory: {artifact_directory}")
        device_properties = read_device_properties(device_id)
        process_device(in_path, out_path, artifact_directory, device_id, device_properties, version)
        warning_message = get_property_or_none(device_properties, "cdn", "warningMessage")
        info_message = get_property_or_none(device_properties, "cdn", "infoMessage")
        incubating = get_boolean_property_or_false(device_properties, "general", "incubating")
        device_names = get_property_or_exit(device_properties, "general", "name").split(',')
        for device_name in device_names:
            device_index.devices.append(asdict(IndexEntry(
                id=device_id,
                name=device_name.strip(),
                vendor=get_property_or_exit(device_properties, "general", "vendor"),
                incubating=incubating,
                warningMessage=warning_message,
                infoMessage=info_message
            )))
    index_file_path = os.path.join(out_path, "index.json")
    with open(index_file_path, "w") as index_file:
        json.dump(asdict(device_index), index_file, indent=2)

if __name__ == "__main__":
    print("Tactility CDN File Generator")
    if "--help" in sys.argv:
        print_help()
        sys.exit()
    # Argument validation
    if len(sys.argv) < 4:
        print_help()
        sys.exit(1)
    if "--verbose" in sys.argv:
        VERBOSE = True
        sys.argv.remove("--verbose")
    main(in_path=sys.argv[1], out_path=sys.argv[2], version=sys.argv[3])
