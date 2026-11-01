#!/usr/bin/env python3
# License: Apache License v2

import os
import re
import shutil
import sys
import subprocess
import tarfile

import _device_host

ID_PATTERN = re.compile(r"^[0-9A-Za-z.]{5,32}$")
NAME_PATTERN = re.compile(r"^[0-9A-Za-z -]{2,32}$")
PROJECT_VARIANTS = ("gui", "terminal", "module")
ttbuild_version = "8.0.0"
verbose = False
# Compile the ESP-IDF and SDK components through ccache (see TactilitySDK.esp32.cmake)
use_ccache = True
# The development server client (shell.py) is next to this script
tool_directory = os.path.dirname(os.path.abspath(__file__))
# The SDKs are in TACTILITY_SDK_PATH/sdk/<version>, which tt sets along with the version it selected.
# Read once, because builds point TACTILITY_SDK_PATH at the platform SDK for CMake.
tt_sdk_version = os.environ.get("TACTILITY_SDK_VERSION")
sdk_root_path = os.environ.get("TACTILITY_SDK_PATH") or None

shell_color_red = "\033[91m"
shell_color_orange = "\033[93m"
shell_color_green = "\033[32m"
shell_color_purple = "\033[35m"
shell_color_cyan = "\033[36m"
shell_color_reset = "\033[m"

def print_help():
    command = "tt app"
    print(f"Usage: {command} [action] [options]")
    print("")
    print("Actions:")
    print("")
    print("  create                         Create a new app or module project in the current directory.")
    print("    Asks for the project type, package id, app id and app name.")
    print("")
    print("  build                          Build the app. Optionally specify a platform.")
    print("    Supported platforms are lower case. Example: esp32s3")
    print("    Supported platforms are read from manifest.properties (target.platforms=all for every installed SDK platform)")
    print("    Parameters:")
    print("      (optional) --architecture [input], -a [input]")
    print("")
    print("  clean                          Clean the build folders")
    print("")
    print("  run                            Run the application")
    print("    Parameters:")
    print("      (optional) --host, -h [input]      Defaults to the host of 'tt host'")
    print("")
    print("  install [app file]             Install the application, or the given .app file")
    print("    Parameters:")
    print("      (optional) --host, -h [input]      Defaults to the host of 'tt host'")
    print("")
    print("  uninstall                      Uninstall the application")
    print("    Parameters:")
    print("      (optional) --host, -h [input]      Defaults to the host of 'tt host'")
    print("")
    print("  bir                            Build, install then run.")
    print("    Parameters:")
    print("      (optional) --host, -h [input]      Defaults to the host of 'tt host'")
    print("      (optional) --architecture, -a [input]")
    print("")
    print("  brrr                           Functionally the same as \"bir\", but \"app goes brrr\" meme variant.")
    print("    Parameters:")
    print("      (optional) --host, -h [input]      Defaults to the host of 'tt host'")
    print("      (optional) --architecture, -a [input]")
    print("")
    print("Options:")
    print("  -p, --path [input]             Path to the app directory (defaults to current directory)")
    print("  --help                         Show this commandline info")
    print("  --no-ccache                    Don't reuse ESP-IDF/SDK objects from other app builds via ccache")
    print("  --skip-build                   Run everything except the idf.py/CMake commands")
    print("  --verbose                      Show extra console output")
    print("")
    print("Examples:")
    print(f"  {command} create")
    print(f"  {command} build")
    print(f"  {command} build --path Apps/Snake --architecture esp32s3 --verbose")
    print(f"  {command} bir --path Apps/Snake --host 192.168.1.50 --architecture esp32s3")

# region Core

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

#endregion Core

#region Versioning

class SemanticVersion:
    def __init__(self, major, minor, patch, tag=None):
        self.major = major
        self.minor = minor
        self.patch = patch
        self.tag = tag

    @staticmethod
    def parse(version_string):
        match = re.match(r"^(\d+)\.(\d+)\.(\d+)(?:-(.+))?$", version_string)
        if match is None:
            exit_with_error(f"Invalid version format: {version_string}")
        major, minor, patch, tag = match.groups()
        return SemanticVersion(int(major), int(minor), int(patch), tag)

    def _numeric(self):
        return (self.major, self.minor, self.patch)

    def __eq__(self, other):
        return self._numeric() == other._numeric() and self.tag == other.tag

    def __lt__(self, other):
        if self._numeric() != other._numeric():
            return self._numeric() < other._numeric()
        # Same major.minor.patch: an untagged version outranks any tagged (pre-release) one.
        if self.tag == other.tag:
            return False
        if self.tag is None:
            return False
        if other.tag is None:
            return True
        return self.tag < other.tag

    def __str__(self):
        return f"{self.major}.{self.minor}.{self.patch}" + (f"-{self.tag}" if self.tag else "")

    # Ignores tags: a "-dev"/"-rc1"-tagged pre-release build of a version is still that version,
    # not an older one, unlike __lt__'s full ordering (which ranks pre-releases below releases).
    def is_older_release_of(self, other):
        return self._numeric() < other._numeric()

# Append a new entry whenever the tool drops support for older SDKs.
# The last entry is the currently effective minimum.
SDK_COMPATIBILITY_LEDGER = [
    SemanticVersion.parse("0.8.0"),
]

def minimum_supported_sdk_version():
    return SDK_COMPATIBILITY_LEDGER[-1]

def validate_sdk_compatibility(target_sdk_version):
    minimum = minimum_supported_sdk_version()
    if SemanticVersion.parse(target_sdk_version).is_older_release_of(minimum):
        exit_with_error(
            f"This tool requires SDK version {minimum} or newer "
            f"(manifest.properties requests {target_sdk_version})"
        )

#endregion Versioning

#region Argument parsing

# canonical name -> (short flag or None, long flag, takes_value)
ARG_SPECS = {
    "path": ("-p", "--path", True),
    "host": ("-h", "--host", True),
    "platform": ("-a", "--architecture", True),
    "verbose": (None, "--verbose", False),
    "skip-build": (None, "--skip-build", False),
    "no-ccache": (None, "--no-ccache", False),
    "help": (None, "--help", False),
}

class Arguments:
    def __init__(self, values):
        self.values = values

    def get(self, key, default=None):
        return self.values.get(key, default)

    def has(self, key):
        return key in self.values

class ParsedCommand:
    def __init__(self, action, args):
        self.action = action
        self.arguments = args

def parse_command_line(argv):
    token_to_spec = {}
    for canonical, (short, long, takes_value) in ARG_SPECS.items():
        if short is not None:
            token_to_spec[short] = (canonical, takes_value)
        token_to_spec[long] = (canonical, takes_value)

    action = None
    args = {}
    index = 0
    while index < len(argv):
        token = argv[index]
        if token in token_to_spec:
            canonical, takes_value = token_to_spec[token]
            if takes_value:
                index += 1
                if index >= len(argv):
                    exit_with_error(f"Missing value for {token}")
                args[canonical] = argv[index]
            else:
                args[canonical] = "true"
        elif action is None:
            action = token
        elif action == "install" and "app-file" not in args:
            args["app-file"] = token
        else:
            exit_with_error(f"Unexpected argument: {token}")
        index += 1
    return ParsedCommand(action, Arguments(args))

#endregion Argument parsing

#region SDK helpers

def get_sdk_dir(version, platform):
    """Absolute, because builds export it as TACTILITY_SDK_PATH for CMake, which also runs in subprocesses with another working directory"""
    return os.path.abspath(os.path.join(sdk_root_path, "sdk", version, f"platform-{platform}", "TactilitySDK"))

def validate_sdks(platforms, version):
    for platform in platforms:
        sdk_dir = get_sdk_dir(version, platform)
        if not os.path.isdir(sdk_dir):
            exit_with_error(f"SDK {version} for {platform} not found: {sdk_dir}")
        if not sdk_has_sdkconfig(version, platform):
            exit_with_error(f"SDK {version} for {platform} does not contain sdkconfig.app.{platform}, which this tool requires. Use a newer SDK version.")

# Platforms that run on this computer (the simulator), named <os>-<architecture>, e.g. "linux-x86_64" or "macos-arm64"
def is_host_platform(platform):
    return platform.startswith(("linux-", "macos-"))

def get_sdk_sdkconfig_path(version, platform):
    return os.path.join(get_sdk_dir(version, platform), f"sdkconfig.app.{platform}")

# ESP32 SDKs built before the sdkconfig was bundled lack it and can't build apps. POSIX SDKs never need one.
def sdk_has_sdkconfig(version, platform):
    return is_host_platform(platform) or os.path.isfile(get_sdk_sdkconfig_path(version, platform))

def copy_sdk_sdkconfig(version, platform):
    if is_host_platform(platform):
        return
    sdkconfig_path = get_sdk_sdkconfig_path(version, platform)
    if not os.path.isfile(sdkconfig_path):
        exit_with_error(f"SDK does not contain {sdkconfig_path}")
    shutil.copy(sdkconfig_path, "sdkconfig")

#endregion SDK helpers

#region Validation

def validate_environment(platforms):
    if any(not is_host_platform(platform) for platform in platforms) and os.environ.get("IDF_PATH") is None:
        if sys.platform == "win32":
            exit_with_error("Cannot find the Espressif IDF SDK. Ensure it is installed and that it is activated via %IDF_PATH%\\export.ps1")
        else:
            exit_with_error("Cannot find the Espressif IDF SDK. Ensure it is installed and that it is activated via $PATH_TO_IDF_SDK/export.sh")
    if not os.path.exists("manifest.properties"):
        exit_with_error("manifest.properties not found")

#endregion Validation

#region Manifest

def read_manifest():
    return read_properties_file("manifest.properties")

def is_v3_manifest(manifest):
    return manifest.get("manifest.version") == "0.3"

# The package identifier: a v2 manifest has no package/app distinction, so its single "app.id"
# doubles as both; a v3 manifest's package id is the bare "id" key, distinct from any of its
# (possibly several) "app.N.id" app ids.
def get_package_id(manifest):
    return manifest["id"] if is_v3_manifest(manifest) else manifest["app.id"]

# The id of the app "run"/"install" operate on. A v2 manifest has exactly one; a v3 manifest may
# declare several, so this picks the first ("app.0.id") as the one a single-app dev workflow means.
def get_primary_app_id(manifest):
    if is_v3_manifest(manifest):
        return manifest.get("app.0.id")
    return manifest["app.id"]

def validate_manifest(manifest):
    for key in ("manifest.version", "target.sdk", "target.platforms"):
        if key not in manifest:
            exit_with_error(f"Invalid manifest format: {key} not found")
    if manifest["manifest.version"] not in ("0.2", "0.3"):
        exit_with_error(f"Unsupported manifest.version: {manifest['manifest.version']}")
    if is_v3_manifest(manifest):
        for key in ("id", "version.name", "version.code"):
            if key not in manifest:
                exit_with_error(f"Invalid manifest format: {key} not found")
        if "app.0.id" not in manifest and "module.0.binary" not in manifest:
            exit_with_error("Invalid manifest format: app.0.id or module.0.binary not found")
        index = 0
        while f"app.{index}.id" in manifest:
            for suffix in ("name", "binary"):
                key = f"app.{index}.{suffix}"
                if key not in manifest:
                    exit_with_error(f"Invalid manifest format: {key} not found")
            index += 1
    else:
        for key in ("app.id", "app.version.name", "app.version.code", "app.name"):
            if key not in manifest:
                exit_with_error(f"Invalid manifest format: {key} not found")
    validate_sdk_compatibility(manifest["target.sdk"])

# Maps each binary this app builds to the directory its own CMake project lives in. A v2
# manifest always describes exactly one app, built at the app's own root (see
# package_intermediate_binaries() for its fixed "app.{elf,so}" package filename, matching
# package_manifest_parse_v2()'s single implicit binary). A v3 manifest declaring a single
# "app.0.*" block is the same case: built at the app's own root. Only when a v3 manifest
# declares more than one "app.N.*" block (0-indexed, contiguous - the first missing "app.N.id"
# ends the list, mirroring package_manifest_parse_v3()) does each get its own subdirectory,
# named after its own "app.N.binary" (the filename it installs as - see
# package_manifest_parse_v3()'s doc).
#
# The "module.N.binary" entries of a v3 manifest are binaries as well and follow the same rules.
def get_binary_dirs(manifest):
    if is_v3_manifest(manifest):
        binaries = []
        index = 0
        while f"app.{index}.id" in manifest:
            binary = manifest[f"app.{index}.binary"]
            binaries.append((binary, binary))
            index += 1
        index = 0
        while f"module.{index}.binary" in manifest:
            binary = manifest[f"module.{index}.binary"]
            binaries.append((binary, binary))
            index += 1
        if len(binaries) == 1:
            return [(binaries[0][0], ".")]
        for binary, directory in binaries:
            if not os.path.isdir(directory):
                exit_with_error(f"Binary directory not found for '{binary}': {directory}")
        return binaries
    else:
        return [(manifest["app.id"], ".")]

# "target.platforms=all" means every platform that an SDK is installed for
def get_manifest_platforms(manifest):
    platforms = [platform.strip() for platform in manifest["target.platforms"].split(",")]
    if platforms != ["all"]:
        return platforms
    version_directory = os.path.join(sdk_root_path, "sdk", manifest["target.sdk"])
    prefix = "platform-"
    if not os.path.isdir(version_directory):
        exit_with_error(f"No SDK {manifest['target.sdk']} installed, so 'all' platforms can't be determined")
    return sorted(name[len(prefix):] for name in os.listdir(version_directory) if name.startswith(prefix))

def is_valid_manifest_platform(manifest, platform):
    return platform in get_manifest_platforms(manifest)

def validate_manifest_platform(manifest, platform):
    if not is_valid_manifest_platform(manifest, platform):
        exit_with_error(f"Platform {platform} is not available in the manifest.")

def get_manifest_target_platforms(manifest, requested_platform):
    if requested_platform == "" or requested_platform is None:
        return get_manifest_platforms(manifest)
    else:
        validate_manifest_platform(manifest, requested_platform)
        return [requested_platform]

#endregion Manifest

#region CMakeLists scaffolding

# Bump whenever CMAKELISTS_TEMPLATE below changes, so existing apps' generated CMakeLists.txt get
# regenerated on their next build instead of silently going stale.
CMAKELISTS_VERSION = 1

CMAKELISTS_TEMPLATE = """# tactility-cmakelists-version: %(version)d
cmake_minimum_required(VERSION 3.20)

if (NOT DEFINED ENV{TACTILITY_SDK_PATH})
    message(FATAL_ERROR "TACTILITY_SDK_PATH environment variable is not set")
endif()

get_filename_component(TACTILITY_SDK_PATH "$ENV{TACTILITY_SDK_PATH}" ABSOLUTE)
include("${TACTILITY_SDK_PATH}/TactilitySDK.cmake")

tactility_project_pre(%(app_id)s)
project(%(app_id)s)
tactility_project_post(%(app_id)s)
"""

def cmakelists_version_marker():
    return f"# tactility-cmakelists-version: {CMAKELISTS_VERSION}"

def ensure_cmakelists_up_to_date(project_id):
    marker = cmakelists_version_marker()
    if os.path.exists("CMakeLists.txt"):
        with open("CMakeLists.txt", "r") as file:
            first_line = file.readline().rstrip("\n")
        if first_line == marker:
            return
    print(f"Updating CMakeLists.txt to {marker}")
    content = CMAKELISTS_TEMPLATE % {"version": CMAKELISTS_VERSION, "app_id": project_id}
    with open("CMakeLists.txt", "w") as file:
        file.write(content)

#endregion CMakeLists scaffolding

#region Building

def get_cmake_path(platform):
    return os.path.join("build", f"cmake-build-{platform}")

def find_elf_file(platform):
    cmake_dir = get_cmake_path(platform)
    if not os.path.exists(cmake_dir):
        return None
    # POSIX apps are dlopen()ed shared objects (app-posix-module), not idf.py/elf_loader
    # relocatable images, so they land as a plain ".so" instead of "*.app.elf".
    suffix = ".so" if is_host_platform(platform) else ".app.elf"
    for file in os.listdir(cmake_dir):
        if file.endswith(suffix):
            return os.path.join(cmake_dir, file)
    return None

def get_posix_build_env(sdk_dir):
    # A dev shell may already have ESP-IDF's export.sh sourced; strip it so the SDK's plain-CMake
    # top-level CMakeLists.txt takes the POSIX branch instead of the idf.py one.
    env = os.environ.copy()
    env.pop("ESP_IDF_VERSION", None)
    env.pop("IDF_PATH", None)
    env["TACTILITY_SDK_PATH"] = sdk_dir
    return env

def build_posix(version, platform, skip_build):
    sdk_dir = get_sdk_dir(version, platform)
    if verbose:
        print(f"Using SDK at {sdk_dir}")
    if skip_build:
        return True
    env = get_posix_build_env(sdk_dir)
    cmake_path = get_cmake_path(platform)
    print_status_busy(f"Building {platform}")
    configure_command = ["cmake", "-S", ".", "-B", cmake_path, "-G", "Ninja"]
    if verbose:
        print(f"Running command: {' '.join(configure_command)}")
    configure_result = subprocess.run(configure_command, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if configure_result.returncode != 0:
        print(configure_result.stdout.decode("UTF-8"), end="")
        print_status_error(f"Configuring {platform}")
        return False
    build_command = ["cmake", "--build", cmake_path]
    if verbose:
        print(f"Running command: {' '.join(build_command)}")
    with subprocess.Popen(build_command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, env=env) as process:
        build_output = wait_for_process(process)
        if process.returncode == 0:
            print_status_success(f"Building {platform}")
            return True
        else:
            for line in build_output:
                print(line, end="")
            print_status_error(f"Building {platform}")
            return False

def build_all(version, platforms, skip_build):
    for platform in platforms:
        if is_host_platform(platform):
            if not build_posix(version, platform, skip_build):
                return False
        # First build command must be "idf.py build", otherwise it fails to execute "idf.py elf"
        # We check if the ELF file exists and run the correct command
        # This can lead to code caching issues, so sometimes a clean build is required
        elif find_elf_file(platform) is None:
            if not build_first(version, platform, skip_build):
                return False
        else:
            if not build_consecutively(version, platform, skip_build):
                return False
    return True

# Always passed, because a -D value persists in CMakeCache.txt
def get_ccache_define():
    return f"TACTILITY_CCACHE={'ON' if use_ccache else 'OFF'}"

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
    copy_sdk_sdkconfig(version, platform)
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
    build_command = ["idf.py", "-B", cmake_path, "-D", get_ccache_define(), "build"]
    if verbose:
        print(f"Running command: {' '.join(build_command)}")
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
    copy_sdk_sdkconfig(version, platform)
    if skip_build:
        return True
    cmake_path = get_cmake_path(platform)
    print_status_busy(f"Building {platform} ELF")
    shell_needed = sys.platform == "win32"
    build_command = ["idf.py", "-B", cmake_path, "-D", get_ccache_define(), "elf"]
    if verbose:
        print(f"Running command: {' '.join(build_command)}")
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
        return False
    shutil.copy("manifest.properties", os.path.join(target_path, "manifest.properties"))
    return True

def get_artifact_extension(platform):
    # POSIX apps are dlopen()ed shared objects (app-posix-module), not idf.py/elf_loader
    # relocatable images, so they land as a plain ".so" instead of ".elf".
    return ".so" if is_host_platform(platform) else ".elf"

def package_intermediate_binaries(target_path, platforms, manifest):
    # Each binary is built in its own directory (get_binary_dirs()), with its own
    # build/cmake-build-{platform} tree; chdir into it so find_elf_file()/get_cmake_path()
    # (both CWD-relative) resolve against the right one, then restore CWD for the next binary.
    original_cwd = os.getcwd()
    for binary, directory in get_binary_dirs(manifest):
        os.chdir(directory)
        try:
            for platform in platforms:
                elf_path = find_elf_file(platform)
                if elf_path is None:
                    print_error(f"ELF file not found for '{binary}' on {platform}")
                    return False
                # v2's single app always installs at the fixed path bin/{platform}/app.{elf,so}
                # (package_manifest_parse_v2()); v3 apps install under their own declared
                # app.N.binary name (package_manifest_parse_v3()).
                artifact_name = binary if is_v3_manifest(manifest) else "app"
                platform_dir = os.path.join(target_path, "bin", platform)
                os.makedirs(platform_dir, exist_ok=True)
                shutil.copy(elf_path, os.path.join(platform_dir, f"{artifact_name}{get_artifact_extension(platform)}"))
        finally:
            os.chdir(original_cwd)
    return True

def package_intermediate_assets(target_path):
    if os.path.isdir("assets"):
        shutil.copytree("assets", os.path.join(target_path, "assets"), dirs_exist_ok=True)

def package_intermediate(platforms, manifest):
    target_path = os.path.abspath(os.path.join("build", "package-intermediate"))
    if os.path.isdir(target_path):
        shutil.rmtree(target_path)
    os.makedirs(target_path, exist_ok=True)
    if not package_intermediate_manifest(target_path):
        return False
    if not package_intermediate_binaries(target_path, platforms, manifest):
        return False
    package_intermediate_assets(target_path)
    return True

def package_name(manifest):
    return os.path.join("build", f"{get_package_id(manifest)}.app")

def package_all(manifest, platforms):
    status = f"Building package with {platforms}"
    print_status_busy(status)
    if not package_intermediate(platforms, manifest):
        print_status_error("Building package failed: missing inputs")
        return False
    # Create build/something.app
    try:
        tar_path = package_name(manifest)
        with tarfile.open(tar_path, mode="w", format=tarfile.USTAR_FORMAT) as tar:
            tar.add(os.path.join("build", "package-intermediate"), arcname="")
        print_status_success(status)
        return True
    except Exception as e:
        print_status_error(f"Building package failed: {e}")
        return False

#endregion Packaging

def build_action(manifest, arguments):
    platform_arg = arguments.get("platform")
    skip_build = arguments.has("skip-build")
    platforms_to_build = get_manifest_target_platforms(manifest, platform_arg)
    # Environment validation
    validate_environment(platforms_to_build)

    sdk_version = manifest["target.sdk"]
    validate_sdks(platforms_to_build, sdk_version)

    # A multi-binary app builds each of its binaries in its own subdirectory as an independent
    # CMake project; a single-binary app has exactly one binary, built at the app root
    # ("." from get_binary_dirs()), matching the tool's original single-binary behavior exactly.
    original_cwd = os.getcwd()
    for binary, directory in get_binary_dirs(manifest):
        os.chdir(directory)
        try:
            ensure_cmakelists_up_to_date(binary if directory == "." else f"{get_package_id(manifest)}.{binary}")
            if not build_all(sdk_version, platforms_to_build, skip_build):
                return False
        finally:
            os.chdir(original_cwd)

    if not skip_build:
        if not package_all(manifest, platforms_to_build):
            return False
    return True

def clean_action(manifest):
    cleaned_any = False
    # The app root always has its own build/ (package-intermediate + the final .app/.elf,
    # regardless of binary count). A multi-binary app additionally has one build/ per binary
    # subdirectory (get_binary_dirs()), which "." (the root, handled above) doesn't repeat.
    if os.path.exists("build"):
        print_status_busy("Removing build/")
        shutil.rmtree("build")
        print_status_success("Removed build/")
        cleaned_any = True
    original_cwd = os.getcwd()
    for _, directory in get_binary_dirs(manifest):
        if directory == ".":
            continue
        os.chdir(directory)
        try:
            if os.path.exists("build"):
                print_status_busy(f"Removing {directory}/build/")
                shutil.rmtree("build")
                print_status_success(f"Removed {directory}/build/")
                cleaned_any = True
        finally:
            os.chdir(original_cwd)
    if not cleaned_any:
        print("Nothing to clean")

def load_device_client():
    sys.path.insert(0, tool_directory)
    try:
        import shell
    except ImportError:
        exit_with_error(f"shell.py not found in {tool_directory}")
    shell.PROGRAM_NAME = "tt app"
    return shell

def get_host(arguments):
    host = arguments.get("host") or _device_host.read_device_host()
    if host is None:
        exit_with_error(_device_host.MISSING_HOST_MESSAGE)
    return host

def run_action(manifest, arguments):
    host = get_host(arguments)
    app_id = get_primary_app_id(manifest)
    if app_id is None:
        exit_with_error("This package has no app to run")
    print_status_busy("Running")
    device = load_device_client()
    device.run(host, device.DEFAULT_PORT, app_id)
    print_status_success("Running")

def install_package(host, package_path):
    print_status_busy("Installing")
    device = load_device_client()
    device.install(host, device.DEFAULT_PORT, package_path)
    print_status_success("Installing")

def install_with_manifest_action(manifest, arguments):
    host = get_host(arguments)
    package_path = package_name(manifest)
    if not os.path.isfile(package_path):
        print_status_error(f"Package not found: {package_path} (run 'build' first)")
        return False
    install_package(host, package_path)
    return True

def install_with_app_file_action(app_file, arguments):
    host = get_host(arguments)
    if not os.path.isfile(app_file):
        print_status_error(f"App file not found: {app_file}")
        return False
    install_package(host, app_file)
    return True

def is_app_file(path):
    return path.endswith(".app")

def install_action(manifest, arguments):
    app_file = arguments.get("app-file")
    if app_file is None:
        return install_with_manifest_action(manifest, arguments)
    if not is_app_file(app_file):
        print_status_error(f"Not an app file: {app_file}")
        return False
    return install_with_app_file_action(app_file, arguments)

def uninstall_action(manifest, arguments):
    host = get_host(arguments)
    app_id = get_package_id(manifest)
    print_status_busy("Uninstalling")
    device = load_device_client()
    device.uninstall(host, device.DEFAULT_PORT, app_id)
    print_status_success("Uninstalled")

#region Create

def ask(question, default, is_valid, description):
    """Asks until the answer is valid. An empty answer or the end of the input gives the default."""
    while True:
        try:
            answer = input(f"{question} [{default}]: ").strip()
        except EOFError:
            answer = ""
        if not answer:
            return default
        if is_valid(answer):
            return answer
        print_error(description)

def create_action():
    if tt_sdk_version is None:
        exit_with_error("TACTILITY_SDK_VERSION is not set: run this tool as 'tt app create'")
    if os.path.exists("manifest.properties") or os.path.exists("main"):
        exit_with_error("This folder already has a manifest.properties or main/")
    templates_path = os.path.join(tool_directory, "NewProject")
    if not os.path.isdir(templates_path):
        exit_with_error(f"SDK {tt_sdk_version} has no new project templates")
    variant = ask("Project type (gui, terminal or module)", "gui", lambda answer: answer in PROJECT_VARIANTS, "Use gui, terminal or module")
    package_id = ask("Package id", "example.helloworld", ID_PATTERN.match, "Use 5 to 32 letters, digits and dots")
    app_id = app_name = ""
    if variant != "module":
        app_id = ask("App id", "helloworld", ID_PATTERN.match, "Use 5 to 32 letters, digits and dots")
        app_name = ask("App name", "Hello World", NAME_PATTERN.match, "Use 2 to 32 letters, digits, spaces and dashes")
    placeholders = (("@SDK_VERSION@", tt_sdk_version), ("@PACKAGE_ID@", package_id), ("@APP_ID@", app_id), ("@APP_NAME@", app_name))
    variant_path = os.path.join(templates_path, variant)
    for directory, directory_names, file_names in os.walk(variant_path):
        directory_names.sort()
        for file_name in sorted(file_names):
            source = os.path.join(directory, file_name)
            relative_path = os.path.relpath(source, variant_path)
            with open(source, "rb") as file:
                content = file.read()
            if relative_path == "manifest.properties":
                text = content.decode("utf-8")
                for placeholder, value in placeholders:
                    text = text.replace(placeholder, value)
                content = text.encode("utf-8")
            os.makedirs(os.path.dirname(relative_path) or ".", exist_ok=True)
            with open(relative_path, "wb") as file:
                file.write(content)
            print(f"Created {relative_path}")
    if not os.path.isfile("manifest.properties"):
        exit_with_error(f"SDK {tt_sdk_version} has no {variant} project template")

#endregion Create

#region Main

if __name__ == "__main__":
    import _bootstrap
    _bootstrap.run()
    print(f"Tactility Build System v{ttbuild_version}")
    if sdk_root_path is None:
        exit_with_error("TACTILITY_SDK_PATH is not set: run this tool as 'tt app'")
    sdk_root_path = os.path.abspath(sdk_root_path)

    argv = sys.argv[1:]
    if len(argv) == 0:
        print_help()
        sys.exit(1)

    parsed_command = parse_command_line(argv)

    if parsed_command.arguments.has("help"):
        print_help()
        sys.exit()
    if parsed_command.action is None:
        print_help()
        sys.exit(1)

    verbose = parsed_command.arguments.has("verbose")
    use_ccache = not parsed_command.arguments.has("no-ccache")

    # Creating a project and installing an app file don't need an app project
    if parsed_command.action == "create":
        create_action()
        sys.exit()

    # Installing an app file doesn't need an app project
    if parsed_command.arguments.has("app-file"):
        if not install_action(None, parsed_command.arguments):
            sys.exit(1)
        sys.exit()

    app_path = parsed_command.arguments.get("path")
    if app_path is not None:
        if not os.path.isdir(app_path) or not os.path.isfile(os.path.join(app_path, "manifest.properties")):
            exit_with_error(f"App path not found or missing manifest.properties: {app_path}")
        if verbose:
            print_status_success(f"Switching to app directory: {app_path}")
        os.chdir(app_path)

    if not os.path.isfile("manifest.properties"):
        exit_with_error("manifest.properties not found")
    manifest = read_manifest()
    validate_manifest(manifest)
    if tt_sdk_version is not None and manifest["target.sdk"] != tt_sdk_version:
        exit_with_error(f"App targets SDK {manifest['target.sdk']}, but tt selected SDK {tt_sdk_version}")
    # Actions
    action_arg = parsed_command.action
    if action_arg == "build":
        if not build_action(manifest, parsed_command.arguments):
            sys.exit(1)
    elif action_arg == "clean":
        clean_action(manifest)
    elif action_arg == "run":
        run_action(manifest, parsed_command.arguments)
    elif action_arg == "install":
        install_action(manifest, parsed_command.arguments)
    elif action_arg == "uninstall":
        uninstall_action(manifest, parsed_command.arguments)
    elif action_arg == "bir" or action_arg == "brrr":
        if build_action(manifest, parsed_command.arguments):
            if install_action(manifest, parsed_command.arguments) and get_primary_app_id(manifest) is not None:
                run_action(manifest, parsed_command.arguments)
    else:
        print_help()
        exit_with_error("Unknown commandline parameter")

#endregion Main
