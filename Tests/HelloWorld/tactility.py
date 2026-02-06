import configparser
import json
import os
import re
import shutil
import sys
import subprocess
import time
import urllib.request
import zipfile
import requests
import tarfile

ttbuild_path = ".tactility"
ttbuild_version = "3.2.0"
ttbuild_cdn = "https://cdn.tactilityproject.org"
ttbuild_sdk_json_validity = 3600  # seconds
ttport = 6666
verbose = False
use_local_sdk = False
local_base_path = None

shell_color_red = "\033[91m"
shell_color_orange = "\033[93m"
shell_color_green = "\033[32m"
shell_color_purple = "\033[35m"
shell_color_cyan = "\033[36m"
shell_color_reset = "\033[m"

def print_help():
    print("Usage: python tactility.py [action] [options]")
    print("")
    print("Actions:")
    print("  build [esp32,esp32s3]          Build the app. Optionally specify a platform.")
    print("    esp32:         ESP32")
    print("    esp32s3:       ESP32 S3")
    print("  clean                          Clean the build folders")
    print("  clearcache                     Clear the SDK cache")
    print("  updateself                     Update this tool")
    print("  run [ip]                       Run the application")
    print("  install [ip]                   Install the application")
    print("  uninstall [ip]                 Uninstall the application")
    print("  bir [ip] [esp32,esp32s3]       Build, install then run. Optionally specify a platform.")
    print("  brrr [ip] [esp32,esp32s3]      Functionally the same as \"bir\", but \"app goes brrr\" meme variant.")
    print("")
    print("Options:")
    print("  --help                         Show this commandline info")
    print("  --local-sdk                    Use SDK specified by environment variable TACTILITY_SDK_PATH with platform subfolders matching target platforms.")
    print("  --skip-build                   Run everything except the idf.py/CMake commands")
    print("  --verbose                      Show extra console output")

# region Core

def download_file(url, filepath):
    global verbose
    if verbose:
        print(f"Downloading from {url} to {filepath}")
    request = urllib.request.Request(
        url,
        data=None,
        headers={
            "User-Agent": f"Tactility Build Tool {ttbuild_version}"
        }
    )
    try:
        response = urllib.request.urlopen(request)
        file = open(filepath, mode="wb")
        file.write(response.read())
        file.close()
        return True
    except OSError as error:
        if verbose:
            print_error(f"Failed to fetch URL {url}\n{error}")
        return False

def print_warning(message):
    print(f"{shell_color_orange}WARNING: {message}{shell_color_reset}")

def print_error(message):
    print(f"{shell_color_red}ERROR: {message}{shell_color_reset}")

def print_status_busy(status):
    sys.stdout.write(f"⌛ {status}\r")

def print_status_success(status):
    # Trailing spaces are to overwrite previously written characters by a potentially shorter print_status_busy() text
    print(f"✅ {shell_color_green}{status}{shell_color_reset}          ")

def print_status_error(status):
    # Trailing spaces are to overwrite previously written characters by a potentially shorter print_status_busy() text
    print(f"❌ {shell_color_red}{status}{shell_color_reset}          ")

def exit_with_error(message):
    print_error(message)
    sys.exit(1)

def get_url(ip, path):
    return f"http://{ip}:{ttport}{path}"

def read_properties_file(path):
    config = configparser.RawConfigParser()
    config.read(path)
    return config

#endregion Core

#region SDK helpers

def read_sdk_json():
    json_file_path = os.path.join(ttbuild_path, "tool.json")
    with open(json_file_path) as json_file:
        return json.load(json_file)

def get_sdk_dir(version, platform):
    global use_local_sdk, local_base_path
    if use_local_sdk:
        base_path = local_base_path
        if base_path is None:
            exit_with_error("TACTILITY_SDK_PATH environment variable is not set")
        sdk_parent_dir = os.path.join(base_path, f"{version}-{platform}")
        sdk_dir = os.path.join(sdk_parent_dir, "TactilitySDK")
        if not os.path.isdir(sdk_dir):
            exit_with_error(f"Local SDK folder not found for platform {platform}: {sdk_dir}")
        return sdk_dir
    else:
        return os.path.join(ttbuild_path, f"{version}-{platform}", "TactilitySDK")

def validate_local_sdks(platforms, version):
    if not use_local_sdk:
        return
    global local_base_path
    base_path = local_base_path
    for platform in platforms:
        sdk_parent_dir = os.path.join(base_path, f"{version}-{platform}")
        sdk_dir = os.path.join(sdk_parent_dir, "TactilitySDK")
        if not os.path.isdir(sdk_dir):
            exit_with_error(f"Local SDK folder missing for {platform}: {sdk_dir}")

def get_sdk_root_dir(version, platform):
    global ttbuild_cdn
    return os.path.join(ttbuild_path, f"{version}-{platform}")

def get_sdk_url(version, file):
    global ttbuild_cdn
    return f"{ttbuild_cdn}/sdk/{version}/{file}"

def sdk_exists(version, platform):
    sdk_dir = get_sdk_dir(version, platform)
    return os.path.isdir(sdk_dir)

def should_update_tool_json():
    global ttbuild_cdn
    json_filepath = os.path.join(ttbuild_path, "tool.json")
    if os.path.exists(json_filepath):
        json_modification_time = os.path.getmtime(json_filepath)
        now = time.time()
        global ttbuild_sdk_json_validity
        minimum_seconds_difference = ttbuild_sdk_json_validity
        return (now - json_modification_time) > minimum_seconds_difference
    else:
        return True

def update_tool_json():
    global ttbuild_cdn, ttbuild_path
    json_url = f"{ttbuild_cdn}/sdk/tool.json"
    json_filepath = os.path.join(ttbuild_path, "tool.json")
    return download_file(json_url, json_filepath)

def should_fetch_sdkconfig_files(platform_targets):
    for platform in platform_targets:
        sdkconfig_filename = f"sdkconfig.app.{platform}"
        if not os.path.exists(os.path.join(ttbuild_path, sdkconfig_filename)):
            return True
    return False

def fetch_sdkconfig_files(platform_targets):
    for platform in platform_targets:
        sdkconfig_filename = f"sdkconfig.app.{platform}"
        target_path = os.path.join(ttbuild_path, sdkconfig_filename)
        if not download_file(f"{ttbuild_cdn}/{sdkconfig_filename}", target_path):
            exit_with_error(f"Failed to download sdkconfig file for {platform}")

#endregion SDK helpers

#region Validation

def validate_environment():
    if os.environ.get("IDF_PATH") is None:
        if sys.platform == "win32":
            exit_with_error("Cannot find the Espressif IDF SDK. Ensure it is installed and that it is activated via %IDF_PATH%\\export.ps1")
        else:
            exit_with_error("Cannot find the Espressif IDF SDK. Ensure it is installed and that it is activated via $PATH_TO_IDF_SDK/export.sh")
    if not os.path.exists("manifest.properties"):
        exit_with_error("manifest.properties not found")
    if use_local_sdk == False and os.environ.get("TACTILITY_SDK_PATH") is not None:
        print_warning("TACTILITY_SDK_PATH is set, but will be ignored by this command.")
        print_warning("If you want to use it, use the '--local-sdk' parameter")
    elif use_local_sdk == True and os.environ.get("TACTILITY_SDK_PATH") is None:
        exit_with_error("local build was requested, but TACTILITY_SDK_PATH environment variable is not set.")

def validate_self(sdk_json):
    if not "toolVersion" in sdk_json:
        exit_with_error("Server returned invalid SDK data format (toolVersion not found)")
    if not "toolCompatibility" in sdk_json:
        exit_with_error("Server returned invalid SDK data format (toolCompatibility not found)")
    if not "toolDownloadUrl" in sdk_json:
        exit_with_error("Server returned invalid SDK data format (toolDownloadUrl not found)")
    tool_version = sdk_json["toolVersion"]
    tool_compatibility = sdk_json["toolCompatibility"]
    if tool_version != ttbuild_version:
        print_warning(f"New version available: {tool_version} (currently using {ttbuild_version})")
        print_warning(f"Run 'tactility.py updateself' to update.")
    if re.search(tool_compatibility, ttbuild_version) is None:
        print_error("The tool is not compatible anymore.")
        print_error("Run 'tactility.py updateself' to update.")
        sys.exit(1)

#endregion Validation

#region Manifest

def read_manifest():
    return read_properties_file("manifest.properties")

def validate_manifest(manifest):
    # [manifest]
    if not "manifest" in manifest:
        exit_with_error("Invalid manifest format: [manifest] not found")
    if not "version" in manifest["manifest"]:
        exit_with_error("Invalid manifest format: [manifest] version not found")
    # [target]
    if not "target" in manifest:
        exit_with_error("Invalid manifest format: [target] not found")
    if not "sdk" in manifest["target"]:
        exit_with_error("Invalid manifest format: [target] sdk not found")
    if not "platforms" in manifest["target"]:
        exit_with_error("Invalid manifest format: [target] platforms not found")
    # [app]
    if not "app" in manifest:
        exit_with_error("Invalid manifest format: [app] not found")
    if not "id" in manifest["app"]:
        exit_with_error("Invalid manifest format: [app] id not found")
    if not "versionName" in manifest["app"]:
        exit_with_error("Invalid manifest format: [app] versionName not found")
    if not "versionCode" in manifest["app"]:
        exit_with_error("Invalid manifest format: [app] versionCode not found")
    if not "name" in manifest["app"]:
        exit_with_error("Invalid manifest format: [app] name not found")

def is_valid_manifest_platform(manifest, platform):
    manifest_platforms = manifest["target"]["platforms"].split(",")
    return platform in manifest_platforms

def validate_manifest_platform(manifest, platform):
    if not is_valid_manifest_platform(manifest, platform):
        exit_with_error(f"Platform {platform} is not available in the manifest.")

def get_manifest_target_platforms(manifest, requested_platform):
    if requested_platform == "" or requested_platform is None:
        return manifest["target"]["platforms"].split(",")
    else:
        validate_manifest_platform(manifest, requested_platform)
        return [requested_platform]

#endregion Manifest

#region SDK download

def sdk_download(version, platform):
    sdk_root_dir = get_sdk_root_dir(version, platform)
    os.makedirs(sdk_root_dir, exist_ok=True)
    sdk_index_url = get_sdk_url(version, "index.json")
    print(f"Downloading SDK version {version} for {platform}")
    sdk_index_filepath = os.path.join(sdk_root_dir, "index.json")
    if verbose:
        print(f"Downloading {sdk_index_url} to {sdk_index_filepath}")
    if not download_file(sdk_index_url, sdk_index_filepath):
        # TODO: 404 check, print a more accurate error
        print_error(f"Failed to download SDK version {version}. Check your internet connection and make sure this release exists.")
        return False
    with open(sdk_index_filepath) as sdk_index_json_file:
        sdk_index_json = json.load(sdk_index_json_file)
    sdk_platforms = sdk_index_json["platforms"]
    if platform not in sdk_platforms:
        print_error(f"Platform {platform} not found in {sdk_platforms} for version {version}")
        return False
    sdk_platform_file = sdk_platforms[platform]
    sdk_zip_source_url = get_sdk_url(version, sdk_platform_file)
    sdk_zip_target_filepath = os.path.join(sdk_root_dir, f"{version}-{platform}.zip")
    if verbose:
        print(f"Downloading {sdk_zip_source_url} to {sdk_zip_target_filepath}")
    if not download_file(sdk_zip_source_url, sdk_zip_target_filepath):
        print_error(f"Failed to download {sdk_zip_source_url} to {sdk_zip_target_filepath}")
        return False
    with zipfile.ZipFile(sdk_zip_target_filepath, "r") as zip_ref:
        zip_ref.extractall(os.path.join(sdk_root_dir, "TactilitySDK"))
    return True

def sdk_download_all(version, platforms):
    for platform in platforms:
        if not sdk_exists(version, platform):
            if not sdk_download(version, platform):
                return False
        else:
            if verbose:
                print(f"Using cached download for SDK version {version} and platform {platform}")
    return True

#endregion SDK download

#region Building

def get_cmake_path(platform):
    return os.path.join("build", f"cmake-build-{platform}")

def find_elf_file(platform):
    cmake_dir = get_cmake_path(platform)
    if os.path.exists(cmake_dir):
        for file in os.listdir(cmake_dir):
            if file.endswith(".app.elf"):
                return os.path.join(cmake_dir, file)
    return None

def build_all(version, platforms, skip_build):
    for platform in platforms:
        # First build command must be "idf.py build", otherwise it fails to execute "idf.py elf"
        # We check if the ELF file exists and run the correct command
        # This can lead to code caching issues, so sometimes a clean build is required
        if find_elf_file(platform) is None:
            if not build_first(version, platform, skip_build):
                return False
        else:
            if not build_consecutively(version, platform, skip_build):
                return False
    return True

def wait_for_process(process):
    buffer = []
    if sys.platform != "win32":
        os.set_blocking(process.stdout.fileno(), False)
    while process.poll() is None:
        while True:
            line = process.stdout.readline()
            if line:
                decoded_line = line.decode("UTF-8")
                if decoded_line != "":
                    buffer.append(decoded_line)
                else:
                    break
            else:
                break
    # Read any remaining output
    for line in process.stdout:
        decoded_line = line.decode("UTF-8")
        if decoded_line:
            buffer.append(decoded_line)
    return buffer

# The first build must call "idf.py build" and consecutive builds must call "idf.py elf" as it finishes faster.
# The problem is that the "idf.py build" always results in an error, even though the elf file is created.
# The solution is to suppress the error if we find that the elf file was created.
def build_first(version, platform, skip_build):
    sdk_dir = get_sdk_dir(version, platform)
    if verbose:
        print(f"Using SDK at {sdk_dir}")
    os.environ["TACTILITY_SDK_PATH"] = sdk_dir
    sdkconfig_path = os.path.join(ttbuild_path, f"sdkconfig.app.{platform}")
    shutil.copy(sdkconfig_path, "sdkconfig")
    elf_path = find_elf_file(platform)
    # Remove previous elf file: re-creation of the file is used to measure if the build succeeded,
    # as the actual build job will always fail due to technical issues with the elf cmake script
    if elf_path is not None:
        os.remove(elf_path)
    if skip_build:
        return True
    print(f"Building first {platform} build")
    cmake_path = get_cmake_path(platform)
    print_status_busy(f"Building {platform} ELF")
    shell_needed = sys.platform == "win32"
    build_command = ["idf.py", "-B", cmake_path, "build"]
    if verbose:
        print(f"Running command: {" ".join(build_command)}")
    with subprocess.Popen(build_command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, shell=shell_needed) as process:
        build_output = wait_for_process(process)
        # The return code is never expected to be 0 due to a bug in the elf cmake script, but we keep it just in case
        if process.returncode == 0:
            print(f"{shell_color_green}Building for {platform} ✅{shell_color_reset}")
            return True
        else:
            if find_elf_file(platform) is None:
                for line in build_output:
                    print(line, end="")
                print_status_error(f"Building {platform} ELF")
                return False
            else:
                print_status_success(f"Building {platform} ELF")
                return True

def build_consecutively(version, platform, skip_build):
    sdk_dir = get_sdk_dir(version, platform)
    if verbose:
        print(f"Using SDK at {sdk_dir}")
    os.environ["TACTILITY_SDK_PATH"] = sdk_dir
    sdkconfig_path = os.path.join(ttbuild_path, f"sdkconfig.app.{platform}")
    shutil.copy(sdkconfig_path, "sdkconfig")
    if skip_build:
        return True
    cmake_path = get_cmake_path(platform)
    print_status_busy(f"Building {platform} ELF")
    shell_needed = sys.platform == "win32"
    build_command = ["idf.py", "-B", cmake_path, "elf"]
    if verbose:
        print(f"Running command: {" ".join(build_command)}")
    with subprocess.Popen(build_command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, shell=shell_needed) as process:
        build_output = wait_for_process(process)
        if process.returncode == 0:
            print_status_success(f"Building {platform} ELF")
            return True
        else:
            for line in build_output:
                print(line, end="")
            print_status_error(f"Building {platform} ELF")
            return False

#endregion Building

#region Packaging

def package_intermediate_manifest(target_path):
    if not os.path.isfile("manifest.properties"):
        print_error("manifest.properties not found")
        return
    shutil.copy("manifest.properties", os.path.join(target_path, "manifest.properties"))

def package_intermediate_binaries(target_path, platforms):
    elf_dir = os.path.join(target_path, "elf")
    os.makedirs(elf_dir, exist_ok=True)
    for platform in platforms:
        elf_path = find_elf_file(platform)
        if elf_path is None:
            print_error(f"ELF file not found at {elf_path}")
            return
        shutil.copy(elf_path, os.path.join(elf_dir, f"{platform}.elf"))

def package_intermediate_assets(target_path):
    if os.path.isdir("assets"):
        shutil.copytree("assets", os.path.join(target_path, "assets"), dirs_exist_ok=True)

def package_intermediate(platforms):
    target_path = os.path.join("build", "package-intermediate")
    if os.path.isdir(target_path):
        shutil.rmtree(target_path)
    os.makedirs(target_path, exist_ok=True)
    package_intermediate_manifest(target_path)
    package_intermediate_binaries(target_path, platforms)
    package_intermediate_assets(target_path)

def package_name(platforms):
    elf_path = find_elf_file(platforms[0])
    elf_base_name = os.path.basename(elf_path).removesuffix(".app.elf")
    return os.path.join("build", f"{elf_base_name}.app")


def package_all(platforms):
    status = f"Building package with {platforms}"
    print_status_busy(status)
    package_intermediate(platforms)
    # Create build/something.app
    try:
        tar_path = package_name(platforms)
        tar = tarfile.open(tar_path, mode="w", format=tarfile.USTAR_FORMAT)
        tar.add(os.path.join("build", "package-intermediate"), arcname="")
        tar.close()
        print_status_success(status)
        return True
    except Exception as e:
        print_status_error(f"Building package failed: {e}")
        return False

#endregion Packaging

def setup_environment():
    global ttbuild_path
    os.makedirs(ttbuild_path, exist_ok=True)

def build_action(manifest, platform_arg):
    # Environment validation
    validate_environment()
    platforms_to_build = get_manifest_target_platforms(manifest, platform_arg)
    
    if use_local_sdk:
        global local_base_path
        local_base_path = os.environ.get("TACTILITY_SDK_PATH")
        validate_local_sdks(platforms_to_build, manifest["target"]["sdk"])
    
    if should_fetch_sdkconfig_files(platforms_to_build):
        fetch_sdkconfig_files(platforms_to_build)
    
    if not use_local_sdk:
        sdk_json = read_sdk_json()
        validate_self(sdk_json)
    # Build
    sdk_version = manifest["target"]["sdk"]
    if not use_local_sdk:
        if not sdk_download_all(sdk_version, platforms_to_build):
            exit_with_error("Failed to download one or more SDKs")
    if not build_all(sdk_version, platforms_to_build, skip_build):  # Environment validation
        return False
    if not skip_build:
        package_all(platforms_to_build)
    return True

def clean_action():
    if os.path.exists("build"):
        print_status_busy("Removing build/")
        shutil.rmtree("build")
        print_status_success("Removed build/")
    else:
        print("Nothing to clean")

def clear_cache_action():
    if os.path.exists(ttbuild_path):
        print_status_busy(f"Removing {ttbuild_path}/")
        shutil.rmtree(ttbuild_path)
        print_status_success(f"Removed {ttbuild_path}/")
    else:
        print("Nothing to clear")

def update_self_action():
    sdk_json = read_sdk_json()
    tool_download_url = sdk_json["toolDownloadUrl"]
    if download_file(tool_download_url, "tactility.py"):
        print("Updated")
    else:
        exit_with_error("Update failed")

def get_device_info(ip):
    print_status_busy(f"Requesting device info")
    url = get_url(ip, "/info")
    try:
        response = requests.get(url)
        if response.status_code != 200:
            print_error("Run failed")
        else:
            print_status_success(f"Received device info:")
            print(response.json())
    except requests.RequestException as e:
        print_status_error(f"Device info request failed: {e}")

def run_action(manifest, ip):
    app_id = manifest["app"]["id"]
    print_status_busy("Running")
    url = get_url(ip, "/app/run")
    params = {'id': app_id}
    try:
        response = requests.post(url, params=params)
        if response.status_code != 200:
            print_error("Run failed")
        else:
            print_status_success("Running")
    except requests.RequestException as e:
        print_status_error(f"Running request failed: {e}")

def install_action(ip, platforms):
    print_status_busy("Installing")
    for platform in platforms:
        elf_path = find_elf_file(platform)
        if elf_path is None:
            print_status_error(f"ELF file not built for {platform}")
            return False
    package_path = package_name(platforms)
    # print(f"Installing {package_path} to {ip}")
    url = get_url(ip, "/app/install")
    try:
        # Prepare multipart form data
        with open(package_path, 'rb') as file:
            files = {
                'elf': file
            }
            response = requests.put(url, files=files)
            if response.status_code != 200:
                print_status_error("Install failed")
                return False
            else:
                print_status_success("Installing")
                return True
    except requests.RequestException as e:
        print_status_error(f"Install request failed: {e}")
        return False
    except IOError as e:
        print_status_error(f"Install file error: {e}")
        return False

def uninstall_action(manifest, ip):
    app_id = manifest["app"]["id"]
    print_status_busy("Uninstalling")
    url = get_url(ip, "/app/uninstall")
    params = {'id': app_id}
    try:
        response = requests.put(url, params=params)
        if response.status_code != 200:
            print_status_error("Server responded that uninstall failed")
        else:
            print_status_success("Uninstalled")
    except requests.RequestException as e:
        print_status_error(f"Uninstall request failed: {e}")

#region Main

if __name__ == "__main__":
    print(f"Tactility Build System v{ttbuild_version}")
    if "--help" in sys.argv:
        print_help()
        sys.exit()
    # Argument validation
    if len(sys.argv) == 1:
        print_help()
        sys.exit(1)
    if "--verbose" in sys.argv:
        verbose = True
        sys.argv.remove("--verbose")
    skip_build = False
    if "--skip-build" in sys.argv:
        skip_build = True
        sys.argv.remove("--skip-build")
    if "--local-sdk" in sys.argv:
        use_local_sdk = True
        sys.argv.remove("--local-sdk")
    action_arg = sys.argv[1]

    # Environment setup
    setup_environment()
    if not os.path.isfile("manifest.properties"):
        exit_with_error("manifest.properties not found")
    manifest = read_manifest()
    validate_manifest(manifest)
    all_platform_targets = manifest["target"]["platforms"].split(",")
    # Update SDK cache (tool.json)
    if not use_local_sdk and should_update_tool_json() and not update_tool_json():
        exit_with_error("Failed to retrieve SDK info")
    # Actions
    if action_arg == "build":
        if len(sys.argv) < 2:
            print_help()
            exit_with_error("Commandline parameter missing")
        platform = None
        if len(sys.argv) > 2:
            platform = sys.argv[2]
        if not build_action(manifest, platform):
            sys.exit(1)
    elif action_arg == "clean":
        clean_action()
    elif action_arg == "clearcache":
        clear_cache_action()
    elif action_arg == "updateself":
        update_self_action()
    elif action_arg == "run":
        if len(sys.argv) < 3:
            print_help()
            exit_with_error("Commandline parameter missing")
        run_action(manifest, sys.argv[2])
    elif action_arg == "install":
        if len(sys.argv) < 3:
            print_help()
            exit_with_error("Commandline parameter missing")
        platform = None
        platforms_to_install = all_platform_targets
        if len(sys.argv) >= 4:
            platform = sys.argv[3]
            platforms_to_install = [platform]
        install_action(sys.argv[2], platforms_to_install)
    elif action_arg == "uninstall":
        if len(sys.argv) < 3:
            print_help()
            exit_with_error("Commandline parameter missing")
        uninstall_action(manifest, sys.argv[2])
    elif action_arg == "bir" or action_arg == "brrr":
        if len(sys.argv) < 3:
            print_help()
            exit_with_error("Commandline parameter missing")
        platform = None
        platforms_to_install = all_platform_targets
        if len(sys.argv) >= 4:
            platform = sys.argv[3]
            platforms_to_install = [platform]
        if build_action(manifest, platform):
            if install_action(sys.argv[2], platforms_to_install):
                run_action(manifest, sys.argv[2])
    else:
        print_help()
        exit_with_error("Unknown commandline parameter")

#endregion Main
