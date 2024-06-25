# EFR32xG22_OEPL

## License
[CC BY-NC-SA 4.0](https://creativecommons.org/licenses/by-nc-sa/4.0/) (same license as parent project) for files not taken/modified from the Silicon Labs SDK. Those files have a separate license header inside the file (mostly Zlib).

## Introduction

See [top-level readme](../readme.md).

## Development

### Prerequisites

For building directly from your own environment:
- A local install of [Simplicity Studio](https://www.silabs.com/developers/simplicity-studio)
  with tools and SDK installed for the 32-bit product family. This project currently
  requires GSDK version 4.4.1.
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

#### Native build

Manual steps:
- Generate the project files for your local install using Silicon Labs' [SLC tool](https://docs.silabs.com/simplicity-studio-5-users-guide/latest/ss-5-users-guide-tools-slc-cli/)
  by running `slc generate -s <SDKPATH> -p ./EFR32xG22_OEPL.slcp -o makefile`, replacing `<SDKPATH>` with the actual path to your GSDK 4.4.1 (see Simplicity Studio for actual install location).
  Usually this path is something like `~/SimplicityStudio/SDKs/gecko_sdk`.
- Note: if SLC complains about having an untrusted SDK, use the provided command to 'trust' the GSDK install
- Run make (`make -f EFR32xG22_OEPL.Makefile`) to build the project. To clean the output files, build target `clean` using the same command.
- Flash the firmware using your favourite flasher (Simplicity Commander when using Silicon Labs' debug hardware).

Note: SLC-CLI also has a fairly recent option to generate a VS Code project if that is something you prefer.
To generate it, swap `makefile` for `vscode` in the SLC generate command.
See [here](https://docs.silabs.com/simplicity-studio-5-users-guide/latest/ss-5-users-guide-vscode-ide/) for more information.

Note: the SLC generate command needs to be run every time the project file is changed, e.g.
when adding or removing include paths or source files.

Alternatively, use the provided build script `build_firmware.sh`.

#### Docker build
- Build the container with the Silabs build support tools
  - In this folder, run `docker build . -t silabs-builder -f Dockerfile` to build the container and call it `silabs-builder`
- Launch an interactive shell within the build container
  - In this folder, run `docker run --rm -it --user builder -v $(pwd):/build -v ~/.gitconfig:/home/builder/.gitconfig silabs-builder` to launch a build shell
- Run the build script (`build_firmware.sh`) within the container

## Flashing

The firmware on device consists of a bootloader and a main firmware. Both need to be flashed
to ensure the device runs properly. Flash the correct bootloader for your target first,
and then the main firmware for your target.

## Notes

### Firmware operation

The firmware is designed as bare-metal, single thread. It has a main event loop which
is called in a loop for as long as the 'sleep manager' is told that there are actions
pending for the event loop to process. The same sleep manager also needs to be told which
power mode is the lowest mode it can go into, as e.g. receiving radio packets requires
the high frequency clock to be present, and as such cannot go lower than EM1 (while
receiving packets). The radio driver therefore communicates this to the sleep manager
when it is trying to send/receive.

When the radio is not sending or receiving, our application needs to figure out itself
when to add and remove sleep mode inhibitions. Otherwise, the sleep manager will put the
core to sleep at the end of the main loop iteration, and getting out of sleep will only
happen on the next IRQ (e.g. a timer expiring or a GPIO pin changing state if these are
setup properly).

For details on the radio protocol and tag functionality, see the parent project
[OpenEpaperLink](https://github.com/OpenEPaperLink/OpenEPaperLink/wiki) and its source code.
