# SES-imagotag EL042TS1 port plan

Branch: `sesimagotag-el042ts1` (off `main`).

## Goal

Add a new hardware variant to `Tag_FW_EFR32xG22` so that an OEPL
firmware build runs on the SES-imagotag EL042TS1 4.2" BWY tag
(EFR32FG22 MCU, JD79653-style controller panel).

The driver, pin map, palette, and all quirks were characterized in
`~/personal_repos/epaper_tag/EL042TS1_DRIVER.md` — read that first.

## Concrete edits required (all in `firmware/`)

### 1. `oepl_efr32_hwtypes.h`

- Reserve a new hwtype ID (suggestion: `OEPL_EFR32XG22_HWTYPE_SESIMAGOTAG_EL042TS1 = 0x08`) in the enum and `#define` list.
  Keep in sync with the bootloader as the comment demands.
- Add a new `oepl_efr32xg22_displaytype_t` enum value: `EPD_SESIMAGOTAG_EL042TS1`. (Alternative: reuse `EPD_SOLUM_AUTODETECT` but override via hwtype; adding a new type is cleaner.)

### 2. `oepl_efr32_hwtypes.c`

Add new static configs:

```c
static const oepl_efr32xg22_flashconfig_t flashconfig_sesimagotag_el042ts1 = {
  .usart = USART0,
  .MOSI = GPIO_UNUSED,
  .MISO = GPIO_UNUSED,
  .SCK  = GPIO_UNUSED,
  .nCS  = GPIO_UNUSED,
  .EN   = GPIO_UNUSED,
};
// NOTE: external flash pins are not yet identified on this board.
// Build with internal-flash-only first; revisit if we need extra image
// storage. See bench TODO below.

static const oepl_efr32xg22_displayconfig_t displayconfig_sesimagotag_el042ts1 = {
  .usart       = USART0,                // or whichever USART serves PC00..PC04
  .usart_clock = cmuClock_USART0,
  .MOSI  = {.port = gpioPortC, .pin = 0},
  .MISO  = GPIO_UNUSED,
  .SCK   = {.port = gpioPortC, .pin = 1},
  .nCS   = {.port = gpioPortC, .pin = 2},
  .nCS2  = GPIO_UNUSED,
  .DC    = {.port = gpioPortC, .pin = 3},
  .BUSY  = {.port = gpioPortA, .pin = 7},
  .nRST  = {.port = gpioPortC, .pin = 4},
  // PC06 is the BS (SPI mode select) pin — MUST BE LOW.
  // Re-using the `enable` field with idle_state = 0 achieves this:
  // OEPL will drive it LOW during display activity.
  // CAVEAT: check that the driver doesn't toggle it off-display; it
  // must stay LOW for the chip to stay in 4-wire SPI mode.
  .enable = {.port = gpioPortC, .pin = 6, .idle_state = 0},
  .type   = EPD_SESIMAGOTAG_EL042TS1,
};

static const oepl_efr32xg22_pinconfig_t pinconfig_sesimagotag_el042ts1 = {
  .gpio = GPIO_UNUSED,
  .nfc_fd = GPIO_UNUSED,       // NFC not yet probed on this board
  .nfc_fd_em4wuval = 0,
  .button1 = GPIO_UNUSED,       // button not yet identified
  .button1_em4wuval = 0,
  .button2 = GPIO_UNUSED,
  .button2_em4wuval = 0,
};

static const oepl_efr32xg22_ledconfig_t ledconfig_sesimagotag_el042ts1 = {
  .red    = {.port = gpioPortB, .pin = 2},
  .green  = {.port = gpioPortB, .pin = 1},
  .blue   = {.port = gpioPortB, .pin = 3},
  .white  = {.port = gpioPortB, .pin = 4},
};

static const oepl_efr32xg22_debugconfig_t debugconfig_sesimagotag_el042ts1 = {
  .type = DBG_SWO,              // SWO until we identify a UART pin
  .output = { .euart = { .tx = GPIO_UNUSED, .rx = GPIO_UNUSED, ... } },
};

static const oepl_efr32xg22_tagconfig_t tagconfig_sesimagotag_el042ts1 = {
  .hwtype = SESIMAGOTAG_EL042TS1,
  .oepl_hwid = SOLUM_M3_BWRY_42,   // tells AP "I'm a 4.2" BWRY"
  .flash   = &flashconfig_sesimagotag_el042ts1,
  .display = &displayconfig_sesimagotag_el042ts1,
  .gpio    = &pinconfig_sesimagotag_el042ts1,
  .led     = &ledconfig_sesimagotag_el042ts1,
  .nfc     = NULL,
  .debug   = &debugconfig_sesimagotag_el042ts1,
};
```

Then register in `tagdb[]`:

```c
static const oepl_efr32xg22_tagconfig_t* tagdb[] = {
  &tagconfig_brd4402b_memlcd,
  &tagconfig_brd4402b_epd,
  &tagconfig_solum,
  &tagconfig_modchip_hd150,
  &tagconfig_sesimagotag_el042ts1,   // NEW
};
```

### 3. `get_displayparams` — new branch for our display type

In `oepl_efr32xg22_get_displayparams`, add a branch for
`display->type == EPD_SESIMAGOTAG_EL042TS1` that hardcodes:

```c
displayparams->xres = 400;
displayparams->yres = 300;
displayparams->xres_working = 400;
displayparams->yres_working = 300;
displayparams->have_thirdcolor  = true;
displayparams->have_fourthcolor = false;   // BWY, not BWRY
displayparams->ctrl = CTRL_JD;
displayparams->xoffset = 0;
displayparams->yoffset = 0;
displayparams->mirrorX = false;
displayparams->mirrorY = false;
displayparams->swapXY = false;
```

We cannot reuse `EPD_SOLUM_AUTODETECT` because our tag's userdata
layout does not match Solum's (offset 0x09 reads 0x30 on ours, not
the expected JD driver ID 0x2B — the SES-imagotag FCC-ID generation
pre-merger had different userdata formats).

### 4. `get_oepl_hwid` — return the hardcoded `SOLUM_M3_BWRY_42`

The existing `else` branch (`return tagcfg->oepl_hwid;`) already
handles this correctly since we set `oepl_hwid = SOLUM_M3_BWRY_42`
in our tagconfig. No change needed here, as long as `hwtype !=
SOLUM_AUTODETECT` for us (which is the case).

### 5. Pull in DanSchoppe's 400×300 JD init block

`firmware/drivers/oepl_display_driver_jd.c` — add the 400×300 init
from https://github.com/DanSchoppe/Tag_FW_EFR32xG22/pull/1 into the
`display_reinit` switch. Verbatim, it's:

```c
} else if(params->x_res_effective == 400 && params->y_res_effective == 300) {
    // From Waveshare 400x300 sample
    EMIT_INSTRUCTION_STATIC_DATA(0x4D, {0x78});
    EMIT_INSTRUCTION_STATIC_DATA(0x00, {0x0F,0x29});
    EMIT_INSTRUCTION_STATIC_DATA(0x06, {0x0D,0x12,0x24,0x25,0x12,0x29,0x10});
    EMIT_INSTRUCTION_STATIC_DATA(0x30, {0x08});
    EMIT_INSTRUCTION_STATIC_DATA(0x50, {0x37});
    EMIT_INSTRUCTION_VAR_DATA(EPD_CMD_RESOLUTION_SETTING, {...});
    EMIT_INSTRUCTION_STATIC_DATA(0xAE, {0xCF});
    EMIT_INSTRUCTION_STATIC_DATA(0xB0, {0x13});
    EMIT_INSTRUCTION_STATIC_DATA(0xBD, {0x07});
    EMIT_INSTRUCTION_STATIC_DATA(0xBE, {0xFE});
    EMIT_INSTRUCTION_STATIC_DATA(0xE9, {0x01});
    EMIT_INSTRUCTION_NO_DATA(0x04);
    sl_udelay_wait(500);
    oepl_display_driver_wait(2000);
}
```

### 6. Bootloader

The bootloader folder has its own readme. It needs a matching
hwtype value, otherwise `oepl_efr32xg22_get_config()` won't find
our tagconfig at runtime. Walk through `bootloader/` to find where
`btl_id` is set. Likely a Makefile define or a constants header.

## Build

Use the Dockerfile so we don't need Simplicity Studio locally:

```
docker build -t silabs-builder -f Dockerfile .
docker run --rm -it --user builder -v $(pwd):/build -v ~/.gitconfig:/home/builder/.gitconfig silabs-builder
./build_all.sh   # inside the container
```

Expected output: new `*.s37` images in `full_binaries/` including one
matching our hwtype.

## Flash

Same J-Link tooling we used in the custom firmware:
- For ad-hoc/erase/probe: `-device Cortex-M33`.
- For flashing (loadfile): `-device EFR32BG22C224F512IM40`
  (the BG22 profile that worked in the original project).

## Bench TODOs

Before a first end-to-end test:

1. **Physically trace ONE display FPC pin to the MCU** to confirm the
   logical pin map. The reset-pulse sweep that identified RST=PC04
   was very thorough, but a multimeter continuity check is a cheap
   belt-and-suspenders move before committing a hardware config.
2. **Find the external flash pins**, if there is external flash. If
   we build with `flashconfig = GPIO_UNUSED` and OEPL tries to use
   the flash we'll get runtime errors. Visible flash IC on the board?
3. **Identify a debug UART pad** (OEPL wants VCOM for prints). If
   none routed, stick with SWO via the J-Link.
4. **Button pin** — the board probably has a physical button; we
   haven't identified which MCU pin it's on.

## Upstream PR path

Once we have a clean working build:
1. Squash the commits into a single "Add SES-imagotag EL042TS1
   hardware variant" PR against `Tag_FW_EFR32xG22:main`.
2. Reference issue #15 and link our findings.
3. Offer our driver reference (`EL042TS1_DRIVER.md`) as documentation
   under `documentation/`.

## State check

- Fork created:  https://github.com/nuno407/Tag_FW_EFR32xG22
- Local clone:   /Users/nuno/personal_repos/Tag_FW_EFR32xG22
- Branch:        sesimagotag-el042ts1
- Submodules:    initialized (via https, not git@)
- Nothing yet committed on the branch. Start of next session picks up
  here.
