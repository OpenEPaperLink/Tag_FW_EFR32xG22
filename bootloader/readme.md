# Bootloader for EFR32-based OEPL tag firmware

## Description
This folder contains the files required to recreate a bootloader for EFR32xG22-based OEPL tags, primarily for use with the tag firmware included in this same repo.

Currently defined build targets:
| Hardware | Associated build flag |
| -------- | ----------------------- |
| WSTK + BRD4402B | BTL_TYPE_BRD4402B |
| WSTK + BRD4402B + Seeed 104030067 | BTL_TYPE_BRD4402B |
| Solum M3, EFR32BG22-based (autodetect screen based on userdata content) | BTL_TYPE_SOLUM |
| DisplayData / Digi Singapore, EFR32FG22-based | BTL_TYPE_DISPLAYDATA |
| Pricer HD150 using [modchip](https://github.com/OpenEPaperLink/Hardware/pull/1) | BTL_TYPE_MODCHIP |

## Configuration
This is a bootloader configured to store the upgrade file in external flash. It defines the firmware upgrade file size as maximum 236kB. This is done to maximise the amount of storage space available to store images on the same external flash.

Other config options to note:
- No support for encrypted/signed images. This bootloader has no security at all.
- Support for LZMA compression of the upgrade file. The smallest (flash-wise) device we currently support is the 352kB part on the Solum M3 tags. When subtracting 24kB (3 flash pages) for the bootloader and 24kB (3 flash pages) for the non-volatile storage mechanism on internal flash, that leaves the maximum size of the application image at 304kB. So if the LZMA compression is able to hit a 0.77 compression ratio (which it empirically does), we can do a full OTA in the 236kB allocated to OTA.
- No communication at boot, i.e. no way to update the device other than OTA or through connecting an SWD debugger (JLink, DAPLink, ...).
- Supports all flashes compatible with SFDP

The pinout for each supported build target is defined in [`bootloader_pinout.h`](./bootloader_pinout.h). Note that you need to set the correct build flag for the bootloader to compile for your hardware.

## Building
Prebuilt binaries are available in the [`variant_binaries`](./variant_binaries/) folder. If you made changes to the bootloader configuration through modifying the project or header file(s), you will need to rebuild them. See [`build_bootloaders.sh`](./build_bootloaders.sh) for the correct way to rebuild the bootloaders using Silicon Labs' [SLC-CLI](https://docs.silabs.com/simplicity-studio-5-users-guide/latest/ss-5-users-guide-tools-slc-cli/) and the ARM-GCC toolchain. For an easy way to recreate this build environment, see this repo's [Dockerfile](../Dockerfile). Launch the container in interactive mode (see hint below), and running the `build_bootloaders.sh` script should 'just work'.

Hints on using Docker:
- Build the container with the Silabs build support tools: in the folder with the Dockerfile, run `docker build . -t silabs-builder -f Dockerfile` to build the container and call it `silabs-builder`
- Launch an interactive shell within the build container: in the folder with the .slcp file(s), run `docker run --rm -it --user builder -v $(pwd):/build -v ~/.gitconfig:/home/builder/.gitconfig silabs-builder` to launch a build shell
- Run any build script within the container
