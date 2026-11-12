#!/usr/bin/env python3
"""Exports the tools that tt runs, with the new project templates in NewProject/, to release/sdk/<version>/tools/, for tt with TACTILITY_SDK_PATH=release."""

import os
import shutil
import sys

ROOT_PATH = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
TOOLS_PATH = os.path.join(ROOT_PATH, "Buildscripts", "TactilitySDK", "Tools")
NEW_PROJECT_PATH = os.path.join(ROOT_PATH, "Buildscripts", "TactilitySDK", "NewProject")


if __name__ == "__main__":
    with open(os.path.join(ROOT_PATH, "version.txt")) as file:
        version = file.read().strip()
    output_path = os.path.join(ROOT_PATH, "release", "sdk", version, "tools")
    shutil.rmtree(output_path, ignore_errors=True)
    os.makedirs(output_path)
    for file_name in sorted(os.listdir(TOOLS_PATH)):
        if file_name.endswith(".py"):
            shutil.copy2(os.path.join(TOOLS_PATH, file_name), os.path.join(output_path, file_name))
    shutil.copytree(NEW_PROJECT_PATH, os.path.join(output_path, "NewProject"))
    print(f"Exported the tools to {output_path}")
