#!/usr/bin/env bash
# Generates a module project with tt and builds it for one platform.
# Run it from the repository root, after the SDK was prepared in test_sdk/.
# Usage: test-new-project.sh [platform]
set -euo pipefail

platform="$1"
root="$(pwd)"
version="$(cat version.txt)"
project="$root/new-project-module"

rm -rf "$project"
mkdir -p "$project"
cd "$project"

# The answers are: SDK version, project type and package id. An empty line takes the default.
printf '%s\nmodule\n\n' "$version" | TACTILITY_SDK_PATH="$root/test_sdk" python "$root/tt.py" app create
TACTILITY_SDK_PATH="$root/test_sdk" python "$root/tt.py" app build -a "$platform"

# The entry point of a module is renamed by the ELF build. A binary without one is tiny.
test "$(stat -c %s "build/package-intermediate/bin/$platform/main.elf")" -gt 600
