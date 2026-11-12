#!/usr/bin/env python3
# License: Apache License v2
"""Downloads the latest build of the simulator for this computer from the CDN and runs it. Run it as "tt simulator"."""

import argparse
import hashlib
import io
import json
import os
import platform
import shutil
import sys
import urllib.request
import zipfile

CDN_URL = os.environ.get("TT_CDN", "https://cdn.tactilityproject.org")
EXECUTABLE_NAME = "Tactility"


def fail(message):
    print(f"tt simulator: {message}", file=sys.stderr)
    sys.exit(1)


def fetch(url):
    """Returns the content of the url, or raises OSError"""
    request = urllib.request.Request(url, headers={"User-Agent": "tt simulator"})
    with urllib.request.urlopen(request, timeout=30) as response:
        return response.read()


def host_platform():
    """The name of the simulator build for this computer, e.g. "linux-x86_64" or "macos-arm64" """
    if sys.platform == "darwin":
        host_os = "macos"
    elif sys.platform.startswith("linux"):
        host_os = "linux"
    else:
        fail(f"there is no simulator for {sys.platform}")
    return f"{host_os}-{platform.machine()}"


def read_commit(directory):
    try:
        with open(os.path.join(directory, "commit")) as file:
            return file.read().strip()
    except OSError:
        return None


def install(version, simulator_platform, directory):
    """Downloads the simulator when the CDN has a newer build than the installed one"""
    executable_path = os.path.join(directory, EXECUTABLE_NAME)
    try:
        index = json.loads(fetch(f"{CDN_URL}/sdk/{version}/index.json"))
    except OSError as error:
        if os.path.isfile(executable_path):
            print(f"tt simulator: CDN unavailable, running the installed simulator ({error})", file=sys.stderr)
            return
        fail(f"failed to download the index of SDK {version}: {error}")
    entry = index.get("simulators", {}).get(simulator_platform)
    if entry is None:
        available = ", ".join(sorted(index.get("simulators", {}))) or "none"
        fail(f"SDK {version} has no simulator for {simulator_platform} (available: {available})")
    commit = index.get("gitCommit", "")
    if os.path.isfile(executable_path) and read_commit(directory) == commit:
        return
    print(f"Downloading the simulator of SDK {version} for {simulator_platform}", file=sys.stderr)
    archive_url = f"{CDN_URL}/sdk/{version}/{entry['file']}"
    try:
        content = fetch(archive_url)
    except OSError as error:
        if os.path.isfile(executable_path):
            print(f"tt simulator: download failed, running the installed simulator ({error})", file=sys.stderr)
            return
        fail(f"failed to download {archive_url}: {error}")
    if hashlib.sha256(content).hexdigest() != entry["sha256"]:
        fail(f"the simulator of SDK {version} doesn't match its checksum")
    # The user data of the simulator is kept: it lives in the folder that the simulator runs in
    staging_directory = f"{directory}.new"
    shutil.rmtree(staging_directory, ignore_errors=True)
    with zipfile.ZipFile(io.BytesIO(content)) as archive:
        archive.extractall(staging_directory)
    staged_executable = os.path.join(staging_directory, EXECUTABLE_NAME)
    if not os.path.isfile(staged_executable):
        shutil.rmtree(staging_directory)
        fail(f"the simulator of SDK {version} has no {EXECUTABLE_NAME}")
    os.chmod(staged_executable, 0o755)
    with open(os.path.join(staging_directory, "commit"), "w") as file:
        file.write(commit)
    if os.path.isdir(directory):
        data_directory = os.path.join(directory, "data")
        if os.path.isdir(data_directory):
            shutil.rmtree(os.path.join(staging_directory, "data"), ignore_errors=True)
            shutil.move(data_directory, os.path.join(staging_directory, "data"))
        shutil.rmtree(directory)
    os.replace(staging_directory, directory)


def main():
    parser = argparse.ArgumentParser(prog="tt simulator", description="download the latest simulator for this computer and run it")
    parser.add_argument("arguments", nargs=argparse.REMAINDER, help="arguments for the simulator")
    args = parser.parse_args()

    version = os.environ.get("TACTILITY_SDK_VERSION")
    sdk_path = os.environ.get("TACTILITY_SDK_PATH")
    if version is None or sdk_path is None:
        fail("run this as 'tt simulator'")
    simulator_platform = host_platform()
    directory = os.path.join(sdk_path, "sdk", version, f"simulator-{simulator_platform}")
    if os.environ.get("TT_LOCAL") == "1":
        # A local SDK isn't managed by tt, so it has to contain the simulator
        if not os.path.isfile(os.path.join(directory, EXECUTABLE_NAME)):
            fail(f"simulator not found: {directory}")
    else:
        install(version, simulator_platform, directory)

    # The simulator uses its working directory for the data of the device
    os.chdir(directory)
    command = [os.path.join(directory, EXECUTABLE_NAME), *args.arguments]
    sys.stdout.flush()
    sys.stderr.flush()
    os.execv(command[0], command)


if __name__ == "__main__":
    import _bootstrap
    _bootstrap.run()
    main()
