#!/usr/bin/env python3
# License: Apache License v2
"""Downloads the tools and installed platforms of the selected SDK again when the CDN has a newer build of it,
and removes installed platforms that the SDK no longer has (e.g. after a rename). Run it as "tt update-sdk"."""

import hashlib
import io
import json
import os
import shutil
import sys
import urllib.request
import zipfile

CDN_URL = os.environ.get("TT_CDN", "https://cdn.tactilityproject.org")


def fail(message):
    print(f"tt update-sdk: {message}", file=sys.stderr)
    sys.exit(1)


def fetch(url):
    request = urllib.request.Request(url, headers={"User-Agent": "tt update-sdk"})
    try:
        with urllib.request.urlopen(request, timeout=30) as response:
            return response.read()
    except OSError as error:
        fail(f"failed to download {url}: {error}")


def read_commit(platform_directory):
    try:
        with open(os.path.join(platform_directory, "commit")) as file:
            return file.read().strip()
    except OSError:
        return None


def installed_platforms(version_directory):
    prefix = "platform-"
    return sorted(
        name[len(prefix):] for name in os.listdir(version_directory)
        if name.startswith(prefix) and os.path.isdir(os.path.join(version_directory, name, "TactilitySDK"))
    )


def read_installed_tools_hash(version_directory):
    try:
        with open(os.path.join(version_directory, "tools.sha256")) as file:
            return file.read().strip()
    except OSError:
        return None


class Update:
    """Stages every component next to its installed folder, so a failed download leaves the installation untouched"""

    def __init__(self):
        # (target directory, staging directory, metadata path, metadata)
        self.staged = []
        # Installed folders to delete once everything else is in place
        self.removed = []

    def stage(self, archive_data, target_directory, metadata_path, metadata):
        """Extracts into a staging folder, refusing entries outside of it"""
        staging_directory = f"{target_directory}.new"
        shutil.rmtree(staging_directory, ignore_errors=True)
        os.makedirs(staging_directory)
        self.staged.append((target_directory, staging_directory, metadata_path, metadata))
        root = os.path.realpath(staging_directory)
        try:
            with zipfile.ZipFile(io.BytesIO(archive_data)) as archive:
                for member in archive.infolist():
                    destination = os.path.realpath(os.path.join(root, member.filename))
                    if not destination.startswith(root + os.sep):
                        fail(f"invalid entry in the archive: {member.filename}")
                archive.extractall(root)
        except zipfile.BadZipFile:
            fail("the downloaded archive is invalid")

    def discard(self):
        for _, staging_directory, _, _ in self.staged:
            shutil.rmtree(staging_directory, ignore_errors=True)
        self.staged = []

    def commit(self):
        """Replaces the installed folders, and puts the previous ones back when that fails"""
        replaced = []
        try:
            for target_directory, staging_directory, _, _ in self.staged:
                backup_directory = f"{target_directory}.old"
                shutil.rmtree(backup_directory, ignore_errors=True)
                if os.path.isdir(target_directory):
                    os.replace(target_directory, backup_directory)
                replaced.append((target_directory, backup_directory))
                os.replace(staging_directory, target_directory)
        except OSError as error:
            for target_directory, backup_directory in reversed(replaced):
                shutil.rmtree(target_directory, ignore_errors=True)
                if os.path.isdir(backup_directory):
                    os.replace(backup_directory, target_directory)
            self.discard()
            fail(f"failed to install the update: {error}")
        for _, _, metadata_path, metadata in self.staged:
            with open(metadata_path, "w") as file:
                file.write(metadata)
        for _, backup_directory in replaced:
            shutil.rmtree(backup_directory, ignore_errors=True)
        for directory in self.removed:
            shutil.rmtree(directory, ignore_errors=True)
        self.staged = []
        self.removed = []


def main():
    version = os.environ.get("TACTILITY_SDK_VERSION")
    sdk_path = os.environ.get("TACTILITY_SDK_PATH")
    if version is None or sdk_path is None:
        fail("run this tool as 'tt update-sdk'")
    version_directory = os.path.join(sdk_path, "sdk", version)
    if os.environ.get("TT_LOCAL") == "1":
        fail("a local SDK can't be updated")
    index_data = fetch(f"{CDN_URL}/sdk/{version}/index.json")
    try:
        index = json.loads(index_data)
    except ValueError:
        fail("the downloaded index.json is invalid")
    commit = index.get("gitCommit", "")
    available = index.get("platforms", {})
    update = Update()
    try:
        tools = index.get("tools")
        if tools is not None and read_installed_tools_hash(version_directory) != tools.get("sha256"):
            print(f"Downloading the tools of SDK {version}", flush=True)
            archive_data = fetch(f"{CDN_URL}/sdk/{version}/{tools['file']}")
            if hashlib.sha256(archive_data).hexdigest() != tools["sha256"]:
                fail("the downloaded tools don't match their checksum")
            update.stage(archive_data, os.path.join(version_directory, "tools"), os.path.join(version_directory, "tools.sha256"), tools["sha256"])
        for platform in installed_platforms(version_directory):
            platform_directory = os.path.join(version_directory, f"platform-{platform}")
            if platform not in available:
                # Can't be updated, and "tt" refuses to mix it with a newer build of the other platforms
                print(f"Removing SDK {version} for {platform}: the SDK no longer has this platform", flush=True)
                update.removed.append(platform_directory)
                continue
            if read_commit(platform_directory) == commit:
                continue
            print(f"Downloading SDK {version} for {platform}", flush=True)
            archive_data = fetch(f"{CDN_URL}/sdk/{version}/{available[platform]}")
            update.stage(archive_data, os.path.join(platform_directory, "TactilitySDK"), os.path.join(platform_directory, "commit"), commit)
    except SystemExit:
        update.discard()
        print(f"SDK {version} was not changed", file=sys.stderr)
        raise
    updated = len(update.staged) > 0 or len(update.removed) > 0
    update.commit()
    with open(os.path.join(version_directory, "index.json"), "wb") as file:
        file.write(index_data)
    print(f"SDK {version} was updated" if updated else f"SDK {version} is up to date")


if __name__ == "__main__":
    main()
