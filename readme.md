# EFR32xG22_OEPL

## Preface
This software is a side project, driven by spare time and depending on motivation. As such, it comes with no guarantee about future support whatsoever, but any contributions made to it will be highly welcome.

## License
[CC BY-NC-SA 4.0](https://creativecommons.org/licenses/by-nc-sa/4.0/) (same license as parent project) for files not taken/modified from the Silicon Labs SDK. Those files have a separate license header inside the file (mostly Zlib).

## Introduction

This project aims to provide an implementation of [OEPL](https://openepaperlink.de/)
as replacement firmware for ESL tags built around the Silicon Labs EFR32xG22 (MG22/FG22/BG22) SoC.

## Development

### Prerequisites

For building directly from your own environment:
- A local install of [Simplicity Studio](https://www.silabs.com/developers/simplicity-studio)
  with tools and SDK installed for the 32-bit product family. This project currently
  builds with GSDK version 4.4.1.
- A local install of Silicon Labs [SLC-CLI](https://docs.silabs.com/simplicity-studio-5-users-guide/latest/ss-5-users-guide-tools-slc-cli/02-installation)
  and its dependencies (Java runtime).
- make or cmake
- ARM GCC for embedded > v12 installed on your system

For building in Docker:
- A working Docker install

For flashing firmware:
- [Simplicity Commander](https://www.silabs.com/developers/simplicity-studio/simplicity-commander) or a debug probe of your choice with support for EFR32BG22
- The correct wiring between your debug probe and your device's debug port.

### Building firmware

#### Bootloader
The firmware relies on a bootloader which is compiled separately in order to support OTA capabilities. See the [bootloader's own readme](./bootloader/readme.md).

#### Main firmware
See [own readme](./firmware/readme.md).

#### All-in-one Docker build

- Build the buildsystem container with the Silabs build support tools
  - In this folder, run `docker build . -t silabs-builder -f Dockerfile` to build the container and call it `silabs-builder`
- Launch an interactive shell within the build container
  - In this folder, run `docker run --rm -it --user builder -v $(pwd):/build -v ~/.gitconfig:/home/builder/.gitconfig silabs-builder` to launch a build shell
- Run the build script (`build_all.sh`) within the container

## Flashing

The firmware on device consists of a bootloader and a main firmware. Both need to be flashed
to ensure the device runs properly, but the bootloader only needs to be flashed on the initial flash.
Subsequent updates don't need to overwrite the bootloader.

Use the image from the `full_binaries` folder for your initial flash (it's the bootloader + application firmware in one image).
Afterwards, you can use the OEPL OTA process.

| Hardware | Bootloader | Main FW | Bootloader + Main |
| -------- | ----------------------- | -------- | -------- |
| WSTK + BRD4402B | BRD4402B_WSTK.s37 | firmware.s37 | BRD4402B_WSTK_FULL.s37 |
| WSTK + BRD4402B + Seeed 104030067 | BRD4402B_WSTK_EPD.s37 | firmware.s37 | BRD4402B_WSTK_EPD_FULL.s37 |
| Solum M3, EFR32BG22-based (autodetect screen based on userdata content) | SOLUM_AUTODETECT.s37 | firmware.s37 | SOLUM_AUTODETECT_FULL.s37 |
| Pricer HD150 using [modchip](https://github.com/OpenEPaperLink/Hardware/pull/1) | MODCHIP_HD150.s37 | firmware.s37 | MODCHIP_HD150_FULL.s37 |

## Notes

### Debug-locked devices

Some EFR32xG22 devices are debug-locked by their manufacturer to prevent unauthorised
reading of their (trade secret) firmware. As long as 'unauthenticated debug unlock' is
not disabled on the chip, it is possible to unlock the chip for reflashing. The side-
effect of this is that the original firmware is erased before gaining access to the
chip (as this function intended).

Unlocking is best done using Simplicity Commander (works with any J-Link based probe).
Use 'unlock debug access' in the 'flash' tab in the GUI, or issue the command
`commander device unlock`.
