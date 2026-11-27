import os
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
    print("Usage: python upload-firmware-files.py [path] [version] [cloudflareAccountId] [cloudflareTokenName] [cloudflareTokenValue]")
    print("")
    print("Options:")
    print("  --index-only                   Upload only index.json")

def exit_with_error(message):
    print_error(message)
    sys.exit(1)

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
                s3.upload_file(file_path, "tactility", object_path)
            except Exception as e:
                exit_with_error(f"Failed to upload {file_name}: {str(e)}")
            counter += 1

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
        index_only="--index-only" in sys.argv
    )
