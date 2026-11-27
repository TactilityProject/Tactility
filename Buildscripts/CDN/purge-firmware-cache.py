import json
import os
import sys
import urllib.error
import urllib.request

# Cloudflare purges at most this many URLs per request on the lower plans
URLS_PER_REQUEST = 30
CDN_BASE_URL = "https://cdn.tactilityproject.org"

SHELL_COLOR_ORANGE = "\033[93m"
SHELL_COLOR_RESET = "\033[m"

def print_warning(message):
    print(f"{SHELL_COLOR_ORANGE}WARNING: {message}{SHELL_COLOR_RESET}")

def print_help():
    print("Usage: python purge-firmware-cache.py [path] [version]")
    print("")
    print("Purges the CDN cache of the firmware files in [path] that were uploaded to firmware/[version]/.")
    print("Reads CLOUDFLARE_ZONE_ID and CLOUDFLARE_PURGE_CACHE_TOKEN from the environment.")
    print("Without them, or when the purge fails, it only prints a warning: the files then refresh when their cache expires.")

def purge(zone_id: str, token: str, urls: list) -> bool:
    request = urllib.request.Request(
        f"https://api.cloudflare.com/client/v4/zones/{zone_id}/purge_cache",
        data=json.dumps({"files": urls}).encode("utf-8"),
        headers={"Authorization": f"Bearer {token}", "Content-Type": "application/json"},
        method="POST"
    )
    try:
        with urllib.request.urlopen(request, timeout=30) as response:
            result = json.load(response)
    except urllib.error.HTTPError as e:
        print_warning(f"Cache purge failed with status {e.code}: {e.read().decode('utf-8', 'replace')}")
        return False
    except (urllib.error.URLError, TimeoutError, ValueError) as e:
        print_warning(f"Cache purge failed: {e}")
        return False
    if not result.get("success", False):
        print_warning(f"Cache purge failed: {result.get('errors')}")
        return False
    return True

def main(path: str, version: str):
    zone_id = os.environ.get("CLOUDFLARE_ZONE_ID", "")
    token = os.environ.get("CLOUDFLARE_PURGE_CACHE_TOKEN", "")
    if not zone_id or not token:
        print_warning("CLOUDFLARE_ZONE_ID or CLOUDFLARE_PURGE_CACHE_TOKEN is not set, so the CDN cache isn't purged")
        return
    if not os.path.isdir(path):
        print_warning(f"Path not found: {path}")
        return
    urls = [f"{CDN_BASE_URL}/firmware/{version}/{name}" for name in sorted(os.listdir(path))]
    purged = 0
    for start in range(0, len(urls), URLS_PER_REQUEST):
        batch = urls[start:start + URLS_PER_REQUEST]
        if purge(zone_id, token, batch):
            purged += len(batch)
    print(f"Purged the CDN cache of {purged}/{len(urls)} files")

if __name__ == "__main__":
    print("Tactility CDN Firmware Cache Purger")
    if "--help" in sys.argv:
        print_help()
        sys.exit()
    if len(sys.argv) < 3:
        print_help()
        sys.exit(1)
    main(path=sys.argv[1], version=sys.argv[2])
