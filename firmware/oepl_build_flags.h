#ifndef OEPL_BUILD_FLAGS_H
#define OEPL_BUILD_FLAGS_H

// OEPL_SMALL_FLASH: build for xG22 parts with only 256 kB of main flash
// (e.g. the EFR32FG22 on SES-imagotag EL042TS1 tags). The standard build
// assumes a 352 kB part and external SPI flash for image storage; on a
// 256 kB part with no external flash, the application must shrink to
// leave room for NVM3 and one internal image slot:
//
//   0x00000 - 0x06000  bootloader            ( 24 kB)
//   0x06000 - 0x32000  application           (176 kB)
//   0x32000 - 0x38000  NVM3                  ( 24 kB)
//   0x38000 - 0x40000  image storage, 1 slot ( 32 kB)
//
// To make the application fit, this flag stubs out the artwork and the
// 24 pt font used only by >= 6" panel layouts, and drops the display
// drivers for panels other than the JD-family. Do not use the resulting
// binary on large-panel tags.
#define OEPL_SMALL_FLASH 1

#endif // OEPL_BUILD_FLAGS_H
