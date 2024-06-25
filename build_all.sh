#!/bin/bash

# Build bootloaders
cd bootloader
source build_bootloaders.sh
cd ..

# Build firmware
cd firmware
source build_firmware.sh
cd ..

THIS_VER=$(cat firmware/EFR32xG22_OEPL.slcp | sed 's/\r$//' | awk '/SL_APPLICATION_VERSION/ {gsub(/'"'"'|"|\}|\s/, "", $5);print $5}')

# Assemble bootloader + firmware all-in-one images to flash to stock device
mkdir ./full_binaries
mkdir ./full_binaries/v$THIS_VER
for filename in bootloader/variant_binaries/*.s37; do
  commander convert -o ./full_binaries/v$THIS_VER/$(basename "$filename" .s37)_FULL_v$THIS_VER.s37 $filename firmware/firmware.s37
done

mkdir ./ota_file
THIS_EDITION=$(cat firmware/fwid.bin)
commander gbl create ./ota_files/upgrade_"$THIS_EDITION"_v"$THIS_VER".gbl.bin --app firmware/firmware.s37 --compress lzma --metadata firmware/fwid.bin
