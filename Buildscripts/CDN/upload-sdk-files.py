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
    print("Usage: python upload-sdk-files.py [path] [version] [cloudflareAccountId] [cloudflareTokenName] [cloudflareTokenValue]")
    print("       python upload-sdk-files.py --tt [path] [cloudflareAccountId] [cloudflareTokenName] [cloudflareTokenValue]")
    print("")
    print("Options:")
    print("  --index-only                   Upload only index.json")
    print("  --tt                           Upload the tt files that Buildscripts/release-tt.py created in [path]")

def exit_with_error(message):
    print_error(message)
    sys.exit(1)

def create_s3_client(cloudflare_account_id, cloudflare_token_name: str, cloudflare_token_value: str):
    return boto3.client(
        service_name="s3",
        endpoint_url=f"https://{cloudflare_account_id}.r2.cloudflarestorage.com",
        aws_access_key_id=cloudflare_token_name,
        aws_secret_access_key=cloudflare_token_value,
        region_name="auto"
    )

def upload_launcher(path: str, cloudflare_account_id, cloudflare_token_name: str, cloudflare_token_value: str):
    """tt is independent of the SDK version, so it is stored next to the SDK version folders"""
    s3 = create_s3_client(cloudflare_account_id, cloudflare_token_name, cloudflare_token_value)
    # tt.json goes last, so it never announces a version that isn't uploaded yet
    for file_name in ("tt.zip", "tt.json"):
        file_path = os.path.join(path, file_name)
        if not os.path.isfile(file_path):
            exit_with_error(f"File not found: {file_path}")
        print(f"Uploading {file_name} to sdk/{file_name}")
        try:
            s3.upload_file(file_path, "tactility", f"sdk/{file_name}")
        except Exception as e:
            exit_with_error(f"Failed to upload {file_name}: {str(e)}")

def main(path: str, version: str, cloudflare_account_id, cloudflare_token_name: str, cloudflare_token_value: str, index_only: bool):
    if not os.path.exists(path):
        exit_with_error(f"Path not found: {path}")
    s3 = create_s3_client(cloudflare_account_id, cloudflare_token_name, cloudflare_token_value)
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

if __name__ == "__main__":
    print("Tactility CDN SDK Uploader")
    if "--help" in sys.argv:
        print_help()
        sys.exit()
    if "--tt" in sys.argv:
        arguments = [argument for argument in sys.argv[1:] if argument != "--tt"]
        if len(arguments) < 4:
            print_help()
            sys.exit(1)
        upload_launcher(arguments[0], arguments[1], arguments[2], arguments[3])
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
