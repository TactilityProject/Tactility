#!/usr/bin/env python3
# License: Apache License v2
"""Runs the Tactility developer tools of the SDK version that an app targets.

"tt <command> [arguments]" runs <command>.py from the tools of that SDK version.
When TACTILITY_SDK_PATH is set, its SDK and tools are used instead of downloading them.
Both ~/.tactility and TACTILITY_SDK_PATH hold sdk/<version>/tools/ and sdk/<version>/platform-<platform>/TactilitySDK/.
The tt (Linux, macOS) and tt.ps1 (Windows PowerShell) wrappers next to this script run it.
The SDK version is read from ./manifest.properties, or else from the first Apps/*/manifest.properties or apps/*/manifest.properties.
Without a manifest, the SDK version that was chosen before is used (.tactility/sdk_version in the current folder),
else the version of the device that "tt host" stored, else it asks for one.
"tt host" and "tt sdk" are built in, because they need no SDK: they show or store the device host (.tactility/device_host) and the SDK version (.tactility/sdk_version).
"""

import hashlib
import json
import os
import re
import shutil
import socket
import ssl
import subprocess
import sys
import time
import urllib.request
import zipfile

TT_VERSION = "1.4.0"
CDN_URL = os.environ.get("TT_CDN", "https://cdn.tactilityproject.org")
HOME_DIRECTORY = os.path.join(os.path.expanduser("~"), ".tactility")
INDEX_VALIDITY_SECONDS = 3600
DEVICE_PORT = 6667
DEVICE_HOST_PATH = os.path.join(".tactility", "device_host")
SDK_VERSION_PATH = os.path.join(".tactility", "sdk_version")
DEVICE_TIMEOUT_SECONDS = 5
DEVICE_REPLY_MAX_BYTES = 1024
COMMAND_PATTERN = re.compile(r"^[a-z0-9-]+$")
VERSION_PATTERN = re.compile(r"^[0-9A-Za-z.+-]+$")


def fail(message):
    print(f"tt: {message}", file=sys.stderr)
    sys.exit(1)


def warn(message):
    print(f"tt: {message}", file=sys.stderr)


def download(url, path):
    """Downloads to a temporary file next to path, so a failed download keeps the existing file"""
    request = urllib.request.Request(url, headers={"User-Agent": f"tt.py {TT_VERSION}"})
    partial_path = f"{path}.part"
    try:
        with urllib.request.urlopen(request, timeout=30) as response, open(partial_path, "wb") as file:
            shutil.copyfileobj(response, file)
    except OSError as error:
        if os.path.exists(partial_path):
            os.remove(partial_path)
        warn(f"failed to download {url}: {error}")
        return False
    os.replace(partial_path, path)
    return True


def read_properties(path):
    properties = {}
    with open(path) as file:
        for line in file:
            stripped = line.strip()
            if not stripped or stripped.startswith("#"):
                continue
            key, separator, value = line.partition("=")
            if separator:
                properties[key.strip()] = value.strip()
    return properties


def find_manifest():
    if os.path.isfile("manifest.properties"):
        return "manifest.properties"
    for apps_directory in ("Apps", "apps"):
        if not os.path.isdir(apps_directory):
            continue
        for name in sorted(os.listdir(apps_directory)):
            path = os.path.join(apps_directory, name, "manifest.properties")
            if os.path.isfile(path):
                return path
    return None


def is_valid_version(version):
    return VERSION_PATTERN.match(version) is not None and ".." not in version


def read_line():
    """Reads from the input one byte at a time, so the command that tt starts still gets the rest of a piped input"""
    line = bytearray()
    while True:
        byte = os.read(0, 1)
        if not byte or byte == b"\n":
            return line.decode("utf-8", errors="replace").strip()
        line += byte


def ask(question, default=None, is_valid=None, description="invalid value"):
    """Asks until the answer is valid. An empty answer or the end of the input gives the default. Returns None when there is none."""
    suffix = f" [{default}]" if default is not None else ""
    while True:
        # On stderr, so the prompts don't end up in the output of the command
        print(f"{question}{suffix}: ", end="", file=sys.stderr, flush=True)
        answer = read_line()
        if not answer:
            return default
        if is_valid is None or is_valid(answer):
            return answer
        print(f"tt: {description}", file=sys.stderr)


def read_stored(path):
    try:
        with open(path) as file:
            return file.read().strip() or None
    except OSError:
        return None


def write_stored(path, value):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w") as file:
        file.write(f"{value}\n")


def read_device_host():
    return read_stored(DEVICE_HOST_PATH)


def read_bounded_line(connection, deadline):
    """Returns the first line of the connection, or None when it has no newline within the size limit or before the deadline"""
    data = bytearray()
    while b"\n" not in data:
        remaining = deadline - time.monotonic()
        if remaining <= 0 or len(data) >= DEVICE_REPLY_MAX_BYTES:
            return None
        connection.settimeout(remaining)
        chunk = connection.recv(DEVICE_REPLY_MAX_BYTES - len(data))
        if not chunk:
            return None
        data += chunk
    return bytes(data.split(b"\n", 1)[0])


def query_device_info(host):
    """Returns the info of the device (vendor, name, id and version), or None when it can't be read"""
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
    # The info is public and the device has a self-signed certificate
    context.check_hostname = False
    context.verify_mode = ssl.CERT_NONE
    hello = {"protocol": 1, "mode": "info", "client": "tt"}
    deadline = time.monotonic() + DEVICE_TIMEOUT_SECONDS
    try:
        with socket.create_connection((host, DEVICE_PORT), DEVICE_TIMEOUT_SECONDS) as connection:
            with context.wrap_socket(connection, server_hostname=host) as secure:
                secure.sendall((json.dumps(hello, separators=(",", ":")) + "\n").encode("utf-8"))
                line = read_bounded_line(secure, deadline)
        if line is None:
            print(f"tt: could not read the device info of {host}: no complete reply in time", file=sys.stderr)
            return None
        info = json.loads(line)["info"]
        if not isinstance(info, dict) or not all(isinstance(info.get(key), str) for key in ("vendor", "name", "id", "version")) or not is_valid_version(info["version"]):
            print(f"tt: could not read the device info of {host}: incomplete reply", file=sys.stderr)
            return None
        return info
    except (OSError, ValueError, KeyError, TypeError) as error:
        print(f"tt: could not read the device info of {host}: {error}", file=sys.stderr)
        return None


def print_device_info(info):
    name = f"{info['vendor']} {info['name']}" if info["vendor"] else info["name"]
    print(f"tt: using device {name} ({info['id']}) with Tactility {info['version']}", file=sys.stderr)


def resolve_device_target():
    """Returns the SDK version of the stored device, or None"""
    host = read_device_host()
    if host is None:
        return None
    info = query_device_info(host)
    if info is None:
        return None
    print_device_info(info)
    return info["version"]


def host_command(arguments):
    """Shows the stored device host, or stores one and checks that the device answers"""
    if not arguments:
        print(read_device_host() or "none")
    elif len(arguments) == 1 and arguments[0].strip():
        host = arguments[0].strip()
        write_stored(DEVICE_HOST_PATH, host)
        print(f"Device host: {host}")
        info = query_device_info(host)
        if info is not None:
            print_device_info(info)
    else:
        fail("usage: tt host [hostname]")


def read_project_properties(manifest_path):
    """Returns the properties of the manifest and its SDK version. Fails when the version is missing or invalid."""
    manifest = read_properties(manifest_path)
    version = manifest.get("target.sdk")
    if not version:
        fail(f"target.sdk not found in {manifest_path}")
    if not is_valid_version(version):
        fail(f"invalid target.sdk in {manifest_path}: {version}")
    return manifest, version


def read_stored_sdk_version():
    """Returns the stored SDK version, or None when it is missing or invalid"""
    version = read_stored(SDK_VERSION_PATH)
    return version if version is not None and is_valid_version(version) else None


def sdk_command(arguments):
    """Shows the SDK version with its source, or stores one. A project's SDK version still takes precedence."""
    if not arguments:
        stored_version = read_stored_sdk_version()
        manifest_path = find_manifest()
        if manifest_path:
            project_version = read_project_properties(manifest_path)[1]
            print(f"{project_version} (from project {manifest_path})")
            if stored_version:
                print(f"{stored_version} in {SDK_VERSION_PATH} is not used while a project is found")
        elif stored_version:
            print(f"{stored_version} (from local setting {SDK_VERSION_PATH})")
        else:
            host = read_device_host()
            info = query_device_info(host) if host else None
            print(f"{info['version']} (from device {host})" if info else "none")
    elif len(arguments) == 1 and is_valid_version(arguments[0]):
        write_stored(SDK_VERSION_PATH, arguments[0])
        print(f"SDK version: {arguments[0]}")
    else:
        fail("usage: tt sdk [version]")


def resolve_target():
    """Returns the SDK version and platforms of the app manifest"""
    manifest_path = find_manifest()
    if manifest_path is None:
        # A project always decides the SDK. Without one, a chosen version beats the device.
        stored_version = read_stored_sdk_version()
        if stored_version is not None:
            print(f"tt: using SDK {stored_version} from {SDK_VERSION_PATH}", file=sys.stderr)
            return stored_version, []
        device_version = resolve_device_target()
        if device_version is not None:
            return device_version, []
        print("tt: no manifest.properties found in . or Apps/*/ or apps/*/", file=sys.stderr)
        # Commands like "tt shell" and "tt app create" don't need an app, only the tools of an SDK version
        version = ask("SDK version to use without a project (leave empty to abort)", None, is_valid_version, "invalid SDK version")
        if version is None:
            fail("no manifest.properties found in . or Apps/*/ or apps/*/")
        write_stored(SDK_VERSION_PATH, version)
        return version, []
    manifest, version = read_project_properties(manifest_path)
    platforms = [platform.strip() for platform in manifest.get("target.platforms", "").split(",") if platform.strip()]
    return version, platforms


def sdk_url(version, file_name):
    return f"{CDN_URL}/sdk/{version}/{file_name}"


def extract_zip(zip_path, target_directory):
    """Extracts into a new directory and then replaces target_directory, refusing entries outside of it"""
    staging_directory = f"{target_directory}.new"
    shutil.rmtree(staging_directory, ignore_errors=True)
    os.makedirs(staging_directory)
    root = os.path.realpath(staging_directory)
    with zipfile.ZipFile(zip_path) as archive:
        for member in archive.infolist():
            destination = os.path.realpath(os.path.join(root, member.filename))
            if not destination.startswith(root + os.sep):
                shutil.rmtree(staging_directory, ignore_errors=True)
                fail(f"invalid entry in {zip_path}: {member.filename}")
        archive.extractall(root)
    shutil.rmtree(target_directory, ignore_errors=True)
    os.replace(staging_directory, target_directory)


def file_sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as file:
        for block in iter(lambda: file.read(65536), b""):
            digest.update(block)
    return digest.hexdigest()


def load_index(version, version_directory):
    index_path = os.path.join(version_directory, "index.json")
    is_fresh = os.path.isfile(index_path) and time.time() - os.path.getmtime(index_path) < INDEX_VALIDITY_SECONDS
    if not is_fresh and not download(sdk_url(version, "index.json"), index_path):
        if not os.path.isfile(index_path):
            fail(f"SDK {version} is not available: check your internet connection and that this SDK version exists")
        warn(f"using the cached information of SDK {version}")
    with open(index_path) as file:
        return json.load(file)


def version_path(sdk_path, version):
    return os.path.join(sdk_path, "sdk", version)


def install_tools(version, version_directory, index):
    tools = index.get("tools")
    if tools is None:
        fail(f"SDK {version} has no tools")
    tools_directory = os.path.join(version_directory, "tools")
    hash_path = os.path.join(version_directory, "tools.sha256")
    print(f"Downloading the tools of SDK {version}", file=sys.stderr)
    zip_path = os.path.join(version_directory, "tools.zip")
    if not download(sdk_url(version, tools["file"]), zip_path):
        fail(f"failed to download the tools of SDK {version}")
    if file_sha256(zip_path) != tools["sha256"]:
        os.remove(zip_path)
        fail(f"the tools of SDK {version} don't match their checksum")
    extract_zip(zip_path, tools_directory)
    os.remove(zip_path)
    with open(hash_path, "w") as file:
        file.write(tools["sha256"])


def install_platforms(version, version_directory, index, platforms):
    available = index.get("platforms", {})
    for platform in platforms:
        platform_directory = os.path.join(version_directory, f"platform-{platform}")
        if platform not in available:
            fail(f"platform {platform} is not available in SDK {version}")
        print(f"Downloading SDK {version} for {platform}", file=sys.stderr)
        zip_path = os.path.join(version_directory, f"platform-{platform}.zip")
        if not download(sdk_url(version, available[platform]), zip_path):
            fail(f"failed to download SDK {version} for {platform}")
        extract_zip(zip_path, os.path.join(platform_directory, "TactilitySDK"))
        os.remove(zip_path)
        # The commit of the SDK build, to find out when a -dev version was published again
        with open(os.path.join(platform_directory, "commit"), "w") as file:
            file.write(index.get("gitCommit", ""))


def read_text(path):
    try:
        with open(path) as file:
            return file.read().strip()
    except OSError:
        return None


def is_installed_build(version_directory, index):
    """True when the installed tools and platforms are of the build that the index describes.
    Installed platforms that the index no longer has (e.g. after a rename) don't count: they can't be
    updated, and "tt update-sdk" removes them."""
    installed_tools_hash = read_text(os.path.join(version_directory, "tools.sha256"))
    if installed_tools_hash is not None and installed_tools_hash != index.get("tools", {}).get("sha256"):
        return False
    available = index.get("platforms", {})
    prefix = "platform-"
    for name in os.listdir(version_directory):
        if not name.startswith(prefix) or name[len(prefix):] not in available:
            continue
        commit = read_text(os.path.join(version_directory, name, "commit"))
        if commit is not None and commit != index.get("gitCommit", ""):
            return False
    return True


def prepare_sdk(version, platforms):
    """Installs what's missing of the SDK version. Installed tools and platforms are only replaced by "tt update-sdk", so they stay from the same build."""
    version_directory = version_path(HOME_DIRECTORY, version)
    index = None
    # "target.platforms=all" means every platform of the SDK
    if platforms == ["all"]:
        os.makedirs(version_directory, exist_ok=True)
        index = load_index(version, version_directory)
        platforms = sorted(index.get("platforms", {}))
    has_tools = os.path.isdir(os.path.join(version_directory, "tools"))
    missing_platforms = [platform for platform in platforms if not os.path.isdir(os.path.join(version_directory, f"platform-{platform}", "TactilitySDK"))]
    if has_tools and not missing_platforms:
        return version_directory
    os.makedirs(version_directory, exist_ok=True)
    if index is None:
        index = load_index(version, version_directory)
    for platform in platforms:
        if platform not in index.get("platforms", {}):
            fail(f"platform {platform} is not available in SDK {version}")
    # The CDN only has the latest build of a version, so a newer build can't be mixed with the installed one
    if not is_installed_build(version_directory, index):
        if has_tools:
            fail(f"SDK {version} was published again since it was installed: run 'tt update-sdk' first")
        fail(f"SDK {version} was published again since it was installed: remove {version_directory} and try again")
    if not has_tools:
        install_tools(version, version_directory, index)
    install_platforms(version, version_directory, index, missing_platforms)
    return version_directory


def is_command(name):
    """Tools named _*.py are internal to the tools and are never commands"""
    return COMMAND_PATTERN.match(name) is not None and not name.startswith("_")


def list_commands(tools_directory):
    return sorted(name[:-3] for name in os.listdir(tools_directory) if name.endswith(".py") and is_command(name[:-3]))


def print_help(version, tools_directory):
    """Without an app manifest, the version and its commands are unknown"""
    print("Usage: tt <command> [arguments]")
    print("")
    print("Built-in commands:")
    print("  host")
    print("  sdk")
    print("")
    if version is None:
        print("Run this in an app folder, or in a folder with Apps/*/ or apps/*/, to list the commands of its SDK")
    else:
        print(f"SDK {version}, commands:")
        for command in list_commands(tools_directory):
            print(f"  {command}")
    print("")
    print("The SDK and tools of TACTILITY_SDK_PATH are used when it is set")


def tool_environment(version, sdk_path, is_local):
    environment = dict(os.environ)
    environment["TACTILITY_SDK_VERSION"] = version
    environment["TACTILITY_SDK_PATH"] = sdk_path
    environment["TT_DIRECTORY"] = os.path.dirname(os.path.abspath(__file__))
    environment["TT_VERSION"] = TT_VERSION
    # The SDK in TACTILITY_SDK_PATH is managed by tt unless it is local
    if is_local:
        environment["TT_LOCAL"] = "1"
    else:
        environment.pop("TT_LOCAL", None)
    return environment


def run_tool(tool_path, arguments, environment):
    command = [sys.executable, tool_path, *arguments]
    if os.name == "posix":
        sys.stdout.flush()
        sys.stderr.flush()
        os.execve(sys.executable, command, environment)
    sys.exit(subprocess.run(command, env=environment).returncode)


def main(arguments):
    command = arguments[0] if arguments else None
    if command == "host":
        host_command(arguments[1:])
        return
    if command == "sdk":
        sdk_command(arguments[1:])
        return
    is_help = command is None or command in ("help", "--help")
    if not is_help and not is_command(command):
        fail(f"command not found: {command}")
    if is_help and find_manifest() is None:
        print_help(None, None)
        return

    version, platforms = resolve_target()
    # Empty counts as unset, like after "export TACTILITY_SDK_PATH="
    local_path = os.environ.get("TACTILITY_SDK_PATH") or None
    is_local = local_path is not None
    if is_local:
        sdk_path = os.path.abspath(local_path)
        print(f"tt: using the local SDK at {sdk_path}", file=sys.stderr)
        tools_directory = os.path.join(version_path(sdk_path, version), "tools")
        if not os.path.isdir(tools_directory):
            fail(f"tools not found: {tools_directory}")
    else:
        sdk_path = HOME_DIRECTORY
        # update-sdk replaces the installed build, so it only needs the tools
        required_platforms = [] if command == "update-sdk" else platforms
        tools_directory = os.path.join(prepare_sdk(version, required_platforms), "tools")
    environment = tool_environment(version, sdk_path, is_local)
    if is_help:
        print_help(version, tools_directory)
        return
    tool_path = os.path.join(tools_directory, f"{command}.py")
    if not os.path.isfile(tool_path):
        fail(f"command not found: {command}")
    run_tool(tool_path, arguments[1:], environment)


if __name__ == "__main__":
    main(sys.argv[1:])
