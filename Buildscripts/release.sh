#!/bin/sh

#
# Usage: release.sh [boardname]
# Example: release.sh lilygo-tdeck
# Description: Releases the current build labeled as a release for the specified board name.
#

echoNewPhase() {
    echo -e "⏳ \e[36m${1}\e[0m"
}

fatalError() {
    echo -e "⚠️ \e[31m${1}\e[0m"
    exit 0
}

releaseSymbols() {
    target_path=$1
    echoNewPhase "Making symbols release at '$target_path'"
    mkdir $target_path
    cp build/*.elf $target_path/
}

release() {
    target_path=$1
    echoNewPhase "Making release at '$target_path'"

    bin_path=$target_path/Binaries
    mkdir -p $bin_path
    mkdir -p $bin_path/partition_table
    mkdir -p $bin_path/bootloader
    cp build/*.bin $bin_path/
    cp build/bootloader/*.bin $bin_path/bootloader/
    cp build/partition_table/*.bin $bin_path/partition_table/
    cp build/flash_args $bin_path/
    cp build/flasher_args.json $bin_path/
    cp build/build_info.json $bin_path/

    cp Buildscripts/Flashing/* $target_path
}

board=$1
release_path=release

if [ $# -lt 1 ]; then
    fatalError "Must pass board name as first argument. (e.g. lilygo_tdeck)"
fi

if [ ! -f $sdkconfig_file ]; then
    fatalError "Board not found: ${sdkconfig_file}"
fi

release "${release_path}/Tactility-${board}"
releaseSymbols "${release_path}/Tactility-${board}-symbols"
