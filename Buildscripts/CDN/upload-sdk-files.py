import io
import json
import os
import re
import sys
import boto3

SHELL_COLOR_RED = "\033[91m"
SHELL_COLOR_ORANGE = "\033[93m"
SHELL_COLOR_RESET = "\033[m"

def print_warning(message):
    print(f"{SHELL_COLOR_ORANGE}WARNING: {message}{SHELL_COLOR_RESET}")

def print_error(message):
    print(f"{SHELL_COLOR_RED}ERROR: {message}{SHELL_COLOR_RESET}")

def print_help():
    print("Usage: python upload-sdk-files.py [path] [version] [cloudflareAccountId] [cloudflareTokenName] [cloudflareTokenValue]")
    print("")
    print("Options:")
    print("  --index-only                   Upload only index.json")

def exit_with_error(message):
    print_error(message)
    sys.exit(1)

TOOL_PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tactility.py")

def create_tool_json():
    with open(TOOL_PATH, "r") as file:
        match = re.search(r'^ttbuild_version = "([^"]+)"', file.read(), re.MULTILINE)
    if match is None:
        exit_with_error(f"ttbuild_version not found in {TOOL_PATH}")
    return json.dumps({"version": match.group(1)}, indent=2).encode("utf-8")

def main(path: str, version: str, cloudflare_account_id, cloudflare_token_name: str, cloudflare_token_value: str, index_only: bool):
    if not os.path.exists(path):
        exit_with_error(f"Path not found: {path}")
    s3 = boto3.client(
        service_name="s3",
        endpoint_url=f"https://{cloudflare_account_id}.r2.cloudflarestorage.com",
        aws_access_key_id=cloudflare_token_name,
        aws_secret_access_key=cloudflare_token_value,
        region_name="auto"
    )
    files_to_upload = os.listdir(path)
    counter = 1
    total = len(files_to_upload)
    for file_name in files_to_upload:
        if not index_only or file_name == 'index.json':
            object_path = f"sdk/{version}/{file_name}"
            print(f"[{counter}/{total}] Uploading {file_name} to {object_path}")
            file_path = os.path.join(path, file_name)
            try:
                s3.upload_file(file_path, "tactility", object_path)
            except Exception as e:
                exit_with_error(f"Failed to upload {file_name}: {str(e)}")
            counter += 1
    if not index_only:
        tool_object_path = f"sdk/{version}/tactility.py"
        print(f"Uploading tactility.py to {tool_object_path}")
        try:
            s3.upload_file(TOOL_PATH, "tactility", tool_object_path)
        except Exception as e:
            exit_with_error(f"Failed to upload tactility.py: {str(e)}")
        tool_json_object_path = f"sdk/{version}/tactility.py.json"
        print(f"Uploading tactility.py.json to {tool_json_object_path}")
        try:
            s3.upload_fileobj(io.BytesIO(create_tool_json()), "tactility", tool_json_object_path)
        except Exception as e:
            exit_with_error(f"Failed to upload tactility.py.json: {str(e)}")

if __name__ == "__main__":
    print("Tactility CDN SDK Uploader")
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
        index_only="--index-only" in sys.argv
    )
