#!/usr/bin/env python3
"""
Prints a short fingerprint of the compiler-relevant part of a device's sdkconfig, for the key of the CI compiler cache.

Usage: gh-sdkconfig-fingerprint.py <device-id>
       gh-sdkconfig-fingerprint.py --list

Devices with the same fingerprint share a compiler cache. The sdkconfig is generated like device.py does, in memory.
Keys that only name the device, set its storage layout or change how the UI is sized are left out: the compiler cache
compares what is actually compiled, so leaving a key out only puts more devices in one bucket and never makes a wrong hit.
--list shows the devices per fingerprint.
"""
import collections
import contextlib
import hashlib
import importlib.util
import io
import os
import sys

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
IGNORED_PREFIXES = (
    "CONFIG_TT_DEVICE", "CONFIG_TT_ROOT", "CONFIG_PARTITION", "CONFIG_ESPTOOLPY",
    "CONFIG_LV_", "CONFIG_TT_UI", "CONFIG_TT_FONT", "CONFIG_TT_SYSTEM_BARS",
)


def load_device_module():
    spec = importlib.util.spec_from_file_location("device", os.path.join(ROOT, "device.py"))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def get_config(device_module, device_id: str) -> dict:
    properties = device_module.read_device_properties(device_id)
    output = io.StringIO()
    # device.py prints progress, which would end up in the output of this script
    with contextlib.redirect_stdout(io.StringIO()):
        device_module.write_properties(output, properties, device_id, False)
    config = {"target": properties.get("hardware.target", "").lower()}
    for line in output.getvalue().splitlines():
        line = line.strip()
        if line and not line.startswith("#") and "=" in line:
            key, _, value = line.partition("=")
            config[key] = value
        elif line.startswith("# CONFIG_") and line.endswith("is not set"):
            config[line.split()[1]] = "n"
    return config


def get_fingerprint(config: dict) -> str:
    items = sorted((key, value) for key, value in config.items() if not key.startswith(IGNORED_PREFIXES))
    return hashlib.sha256(repr(items).encode()).hexdigest()[:12]


def main():
    if len(sys.argv) != 2:
        print(f"Usage: {sys.argv[0]} <device-id> | --list", file=sys.stderr)
        sys.exit(1)
    os.chdir(ROOT)
    device_module = load_device_module()
    if sys.argv[1] == "--list":
        groups = collections.defaultdict(list)
        for device_id in device_module.list_device_ids():
            try:
                config = get_config(device_module, device_id)
            except SystemExit:
                continue
            groups[(config["target"], get_fingerprint(config))].append(device_id)
        for (target, fingerprint), devices in sorted(groups.items(), key=lambda item: (item[0][0], -len(item[1]))):
            print(f"{target} {fingerprint} ({len(devices)}): {', '.join(devices)}")
        return
    device_id = device_module.resolve_device_id(sys.argv[1])
    print(get_fingerprint(get_config(device_module, device_id)))


if __name__ == "__main__":
    main()
