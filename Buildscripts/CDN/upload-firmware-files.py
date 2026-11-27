import json
import os
import re
import sys
import boto3

BUCKET = "tactility"
DEFAULT_KEEP_BUILDS = 2

SHELL_COLOR_RED = "\033[91m"
SHELL_COLOR_ORANGE = "\033[93m"
SHELL_COLOR_RESET = "\033[m"

def print_warning(message):
    print(f"{SHELL_COLOR_ORANGE}WARNING: {message}{SHELL_COLOR_RESET}")

def print_error(message):
    print(f"{SHELL_COLOR_RED}ERROR: {message}{SHELL_COLOR_RESET}")

def print_help():
    print("Usage: python upload-firmware-files.py [path] [version] [cloudflareAccountId] [cloudflareTokenName] [cloudflareTokenValue]")
    print("")
    print("Options:")
    print("  --index-only                   Upload only index.json")
    print(f"  --keep-builds N                Keep the app images of the N newest builds per device (default {DEFAULT_KEEP_BUILDS})")
    print("  --no-cleanup                   Don't delete app images of older builds")

def exit_with_error(message):
    print_error(message)
    sys.exit(1)

def get_upload_args(file_name: str):
    """The manifests change with every build and are small, so the CDN only keeps them for a minute"""
    if file_name.endswith(".json"):
        return {"CacheControl": "public, max-age=60"}
    return {}

def select_obsolete(objects: list, device_id: str, current_file: str, keep: int) -> list:
    """
    The app images of a device that can be deleted. Every build has an image named <device>-<build id>-<name>.
    @param objects (key, last modified) of the files in the channel's folder, keys without the folder
    @param current_file the image that the new manifest refers to, which is always kept
    @return the keys to delete: images beyond the newest `keep` builds. The image named <device>-<name>, without a build id, counts as a build too.
    """
    prefix = f"{device_id}-"
    if not current_file.startswith(prefix):
        return []
    parts = current_file[len(prefix):].split("-", 1)
    if len(parts) != 2:
        return []
    image_name = parts[1]
    # A build id is a commit hash or a timestamp, so the images of a device whose id starts with this device's id don't match
    pattern = re.compile(f"^{re.escape(prefix)}([0-9a-f]{{12}}|[0-9]+)-{re.escape(image_name)}$")
    # A manifest that a device read before this publish can still refer to it, so it is kept like any other previous build
    legacy = f"{prefix}{image_name}"
    images = sorted(
        (item for item in objects if (pattern.match(item[0]) or item[0] == legacy) and item[0] != current_file),
        key=lambda item: item[1],
        reverse=True
    )
    return [key for key, _ in images[max(keep - 1, 0):]]

def clean_up(s3, path: str, version: str, keep: int):
    """Deletes the app images of older builds. A failure is only reported, as the upload succeeded already."""
    folder = f"firmware/{version}/"
    try:
        objects = []
        for page in s3.get_paginator("list_objects_v2").paginate(Bucket=BUCKET, Prefix=folder):
            objects.extend((item["Key"][len(folder):], item["LastModified"]) for item in page.get("Contents", []))
        obsolete = []
        for file_name in sorted(os.listdir(path)):
            if not file_name.endswith("-ota.json"):
                continue
            with open(os.path.join(path, file_name)) as manifest_file:
                current_file = json.load(manifest_file)["file"]
            obsolete.extend(select_obsolete(objects, file_name.removesuffix("-ota.json"), current_file, keep))
        deleted = 0
        for start in range(0, len(obsolete), 1000):
            batch = obsolete[start:start + 1000]
            for key in batch:
                print(f"Deleting {folder}{key}")
            response = s3.delete_objects(Bucket=BUCKET, Delete={"Objects": [{"Key": f"{folder}{key}"} for key in batch], "Quiet": True})
            # A failed object is reported in the response, not raised
            errors = response.get("Errors", [])
            for error in errors:
                print_warning(f"Failed to delete {error.get('Key')}: {error.get('Code')} {error.get('Message')}")
            deleted += len(batch) - len(errors)
        print(f"Deleted {deleted}/{len(obsolete)} app images of older builds")
    except Exception as e:
        print_warning(f"Failed to delete the app images of older builds: {str(e)}")

def main(path: str, version: str, cloudflare_account_id, cloudflare_token_name: str, cloudflare_token_value: str, index_only: bool, keep_builds: int, cleanup: bool):
    if not os.path.exists(path):
        exit_with_error(f"Path not found: {path}")
    s3 = boto3.client(
        service_name="s3",
        endpoint_url=f"https://{cloudflare_account_id}.r2.cloudflarestorage.com",
        aws_access_key_id=cloudflare_token_name,
        aws_secret_access_key=cloudflare_token_value,
        region_name="auto"
    )
    # The manifests go last, so a device never finds a manifest that refers to a binary that isn't uploaded yet
    files_to_upload = sorted(os.listdir(path), key=lambda name: name.endswith(".json"))
    counter = 1
    total = len(files_to_upload)
    for file_name in files_to_upload:
        if not index_only or file_name == 'index.json':
            object_path = f"firmware/{version}/{file_name}"
            print(f"[{counter}/{total}] Uploading {file_name} to {object_path}")
            file_path = os.path.join(path, file_name)
            try:
                s3.upload_file(file_path, BUCKET, object_path, ExtraArgs=get_upload_args(file_name))
            except Exception as e:
                exit_with_error(f"Failed to upload {file_name}: {str(e)}")
            counter += 1
    if cleanup and not index_only:
        clean_up(s3, path, version, keep_builds)

def get_option_value(name: str, default: str) -> str:
    if name in sys.argv:
        index = sys.argv.index(name)
        if index + 1 < len(sys.argv):
            return sys.argv[index + 1]
        exit_with_error(f"{name} needs a value")
    return default

if __name__ == "__main__":
    print("Tactility CDN Firmware Uploader")
    if "--help" in sys.argv:
        print_help()
        sys.exit()
    # Argument validation
    if len(sys.argv) < 6:
        print_help()
        sys.exit(1)
    main(
        path=sys.argv[1],
        version=sys.argv[2],
        cloudflare_account_id=sys.argv[3],
        cloudflare_token_name=sys.argv[4],
        cloudflare_token_value=sys.argv[5],
        index_only="--index-only" in sys.argv,
        keep_builds=max(int(get_option_value("--keep-builds", str(DEFAULT_KEEP_BUILDS))), 1),
        cleanup="--no-cleanup" not in sys.argv
    )
