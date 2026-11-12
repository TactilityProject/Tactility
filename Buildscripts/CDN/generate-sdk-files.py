import hashlib
import subprocess
from datetime import datetime, UTC
import os
import sys
from dataclasses import dataclass, asdict
import json
import shutil
import zipfile

VERBOSE = False
TOOLS_PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "TactilitySDK", "Tools")
NEW_PROJECT_PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "TactilitySDK", "NewProject")

@dataclass
class SdkIndex:
    version: str
    created: str
    gitCommit: str
    platforms: dict
    tools: dict
    simulators: dict

shell_color_red = "\033[91m"
shell_color_orange = "\033[93m"
shell_color_reset = "\033[m"

def print_warning(message):
    print(f"{shell_color_orange}WARNING: {message}{shell_color_reset}")

def print_error(message):
    print(f"{shell_color_red}ERROR: {message}{shell_color_reset}")

def exit_with_error(message):
    print_error(message)
    sys.exit(1)

def print_help():
    print("Usage: python generate-sdk-files.py [inPath] [outPath] [version]")
    print("     inPath   path with the extracted release files")
    print("     outPath  path where the CDN files will become available")
    print("     version  technical version name (e.g. 1.2.0)")
    print("Options:")
    print("  --verbose                      Show extra console output")

def get_git_commit_hash():
    return subprocess.check_output(['git', 'rev-parse', 'HEAD']).decode('ascii').strip()

def create_tools_bundle(out_path: str):
    """Bundles the tools that tt.py runs for this SDK version, with the project templates of "tt app create" in NewProject/"""
    if VERBOSE:
        print(f"Archiving {TOOLS_PATH} to {out_path}/tools.zip")
    archive_path = os.path.join(out_path, "tools.zip")
    with zipfile.ZipFile(archive_path, "w", zipfile.ZIP_DEFLATED) as archive:
        for file_name in sorted(os.listdir(TOOLS_PATH)):
            if file_name.endswith(".py"):
                archive.write(os.path.join(TOOLS_PATH, file_name), file_name)
        for directory, directory_names, file_names in os.walk(NEW_PROJECT_PATH):
            directory_names.sort()
            for file_name in sorted(file_names):
                file_path = os.path.join(directory, file_name)
                archive.write(file_path, os.path.join("NewProject", os.path.relpath(file_path, NEW_PROJECT_PATH)))
    with open(archive_path, "rb") as archive:
        sha256 = hashlib.sha256(archive.read()).hexdigest()
    return {"file": "tools.zip", "sha256": sha256}

def add_simulator(in_path: str, out_path: str, artifact_directory: str, sdk_index: SdkIndex):
    """Archives a simulator build, named <os>-<architecture> like the SDK platforms, e.g. Simulator-linux-x86_64"""
    simulator_platform = artifact_directory.removeprefix("Simulator-")
    if VERBOSE:
        print(f"Archiving {in_path}/{artifact_directory} to {out_path}/{artifact_directory}.zip")
    archive_path = shutil.make_archive(os.path.join(out_path, artifact_directory), 'zip', os.path.join(in_path, artifact_directory))
    with open(archive_path, "rb") as archive:
        sha256 = hashlib.sha256(archive.read()).hexdigest()
    sdk_index.simulators[simulator_platform] = {"file": f"{artifact_directory}.zip", "sha256": sha256}

def main(in_path: str, out_path: str, version: str):
    if not os.path.exists(in_path):
        exit_with_error(f"Input path not found: {in_path}")
    if os.path.exists(out_path):
        shutil.rmtree(out_path)
    os.mkdir(out_path)
    artifact_directories = os.listdir(in_path)
    sdk_index = SdkIndex(
        version=version,
        created=datetime.now(UTC).strftime('%Y-%m-%dT%H:%M:%S'),
        gitCommit=get_git_commit_hash(),
        platforms={},
        tools=create_tools_bundle(out_path),
        simulators={}
    )
    for artifact_directory in artifact_directories:
        if VERBOSE:
            print(f"Processing {in_path}/{artifact_directory}")
        if artifact_directory.startswith("Simulator-"):
            add_simulator(in_path, out_path, artifact_directory, sdk_index)
            continue
        if not artifact_directory.startswith("TactilitySDK-"):
            continue
        sdk_platform = artifact_directory.removeprefix("TactilitySDK-")
        if not sdk_platform:
            exit_with_error(f"Cannot derive platform from directory name: {artifact_directory}")
        sdk_index.platforms[sdk_platform] = f"{artifact_directory}.zip"
        if VERBOSE:
            print(f"Archiving {in_path}/{artifact_directory} to {out_path}/{artifact_directory}.zip")
        shutil.make_archive(os.path.join(out_path, artifact_directory), 'zip', os.path.join(in_path, artifact_directory))
    index_file_path = os.path.join(out_path, "index.json")
    if VERBOSE:
        print(f"Generating {index_file_path}")
    with open(index_file_path, "w") as index_file:
        json.dump(asdict(sdk_index), index_file, indent=2)

if __name__ == "__main__":
    print("Tactility CDN SDK File Generator")
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
