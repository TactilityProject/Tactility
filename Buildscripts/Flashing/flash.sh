#!/bin/sh

# Usage:
#   flash.sh [port]
#
# Arguments:
#   port - the port of the device (e.g. /dev/ttyUSB0, ...)
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

# Ensure the port was specified
if [ -z "$1" ]; then
    echo -e "\e[31m⚠️ Must Specify port as argument. For example:\n\tflash.sh /dev/ttyACM0\n\tflash.sh /dev/ttyUSB0\e[0m"
    exit -1
fi

# Take the flash_arg file contents and join each line in the file into a single line
flash_args=`grep \n Binaries/flash_args | awk '{print}' ORS=' '`
cd Binaries
$esptoolPath --port $1 erase-flash
$esptoolPath --port $1 write-flash $flash_args
cd -

