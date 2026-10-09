#!/usr/bin/env bash

# Usage:
#   merge.sh [chip]
#
# Arguments:
#   chip - optional ESP32 SOC variant (e.g. esp32, esp32s3, esp32c6)
#
# Requirements:
#   jq - run 'pip install jq'
#   esptool.py - run 'pip install esptool'
#
# Documentation:
#   https://docs.espressif.com/projects/esptool/en/latest/esp32/
#

# Source: https://stackoverflow.com/a/53798785
function is_bin_in_path {
  builtin type -P "$1" &> /dev/null
}

function require_bin {
  program=$1
  if ! is_bin_in_path $program; then
      exit 1
  else
      exit 0
  fi
}

# Find either esptool (installed via system package manager) or esptool.py (installed via pip)
if ! is_bin_in_path esptool; then
  if ! is_bin_in_path esptool.py; then
    echo "\e[31m⚠️  esptool not found! Install it from your package manager or install python and run 'pip install esptool'\e[0m"
    exit 1
  else 
    esptoolPath=esptool.py
  fi
else
  esptoolPath=esptool
fi

chip=${1:-esp32s3}

# Take the flash_arg file contents and join each line in the file into a single line
flash_args="$(tr '\n' ' ' < Binaries/flash_args)"
read -r -a flash_args_array <<< "$flash_args"
(
  cd Binaries || exit 1
  "$esptoolPath" --chip "$chip" merge-bin --output merged_binary.bin "${flash_args_array[@]}"
) || exit 1

