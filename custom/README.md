## Custom builds

Each subdirectory under this directory contains a custom EFR32 build.

## Build requirements

- A bash-like shell
- A working [Docker install](https://github.com/OpenEPaperLink/Tag_FW_EFR32xG22?tab=readme-ov-file#all-in-one-docker-build)
- GNU Make

## Building Custom images

The subdirectory to build can be specified in the environment variable BUILD or
on the command line while running make.

For example to build the example:

```
$ make BUILD=example
Building bootloader...

[lots of output deleted]

Bootloader built successfully
Building firmware...

[lots of output deleted]

Firmware built successfully

[lots of output deleted]

Writing to full_binaries/v42/example_FULL_v42.s37...

[lots of output deleted]

Writing GBL file ota_files/upgrade_example_v42.ota.bin...
DONE
```

## Flashing Custom images

If your debug probe is J-Link compatible you can flash your custom
image by "make flash".

## Other Useful Make Targets

Run "make help" for a list of other targets

```
$make help
Usage:
  make            - build full and OTA update custom images
  make clean      - clean app build, leaving bootloader
  make veryclean  - clean bootloader and app builds
  make flash      - flash image with full custom image
  make reset      - reset board
  make version    - display git and release versions
  make help       - help (you're looking at it)

  Specify the BUILD type on the command line or set the environment variable BUILD.
  For example "BUILD=example make flash"
$
```

