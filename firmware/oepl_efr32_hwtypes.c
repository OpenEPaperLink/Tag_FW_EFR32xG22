#include "oepl_efr32_hwtypes.h"
#include "application_properties.h"
#include "oepl-definitions.h"

#define GPIO_UNUSED {.port = gpioPortInvalid, .pin = 0, .idle_state = 0}

// -----------------------------------------------------------------------------
//                              Flash pinouts
// -----------------------------------------------------------------------------
static const oepl_efr32xg22_flashconfig_t flashconfig_brd4402b =
{
  .usart = USART0,
  .MOSI = {.port = gpioPortC, .pin = 0},
  .MISO = {.port = gpioPortC, .pin = 1},
  .SCK = {.port = gpioPortC, .pin = 2},
  .nCS = {.port = gpioPortC, .pin = 4},
  .EN = GPIO_UNUSED
};

static const oepl_efr32xg22_flashconfig_t flashconfig_solum =
{
  .usart = USART0,
  .MOSI = {.port = gpioPortC, .pin = 1},
  .MISO = {.port = gpioPortC, .pin = 0},
  .SCK = {.port = gpioPortC, .pin = 2},
  .nCS = {.port = gpioPortC, .pin = 3},
  .EN = GPIO_UNUSED
};

static const oepl_efr32xg22_flashconfig_t flashconfig_displaydata =
{
  .usart = USART0,
  .MOSI = {.port = gpioPortC, .pin = 0},
  .MISO = {.port = gpioPortC, .pin = 1},
  .SCK = {.port = gpioPortC, .pin = 2},
  .nCS = {.port = gpioPortC, .pin = 3},
  .EN = GPIO_UNUSED
};

static const oepl_efr32xg22_flashconfig_t flashconfig_modchip =
{
  .usart = USART0,
  .MOSI = {.port = gpioPortC, .pin = 1},
  .MISO = {.port = gpioPortC, .pin = 0},
  .SCK = {.port = gpioPortC, .pin = 2},
  .nCS = {.port = gpioPortC, .pin = 3},
  .EN = GPIO_UNUSED
};

// ----- Add new flash pinouts here and keep in sync with bootloader ----

// ----- ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^ ----

// -----------------------------------------------------------------------------
//                              BRD4402B config
// -----------------------------------------------------------------------------
static const oepl_efr32xg22_displayconfig_t displayconfig_brd4402_memlcd =
{
  .usart = USART0,
  .usart_clock = cmuClock_USART0,
  .MOSI = {.port = gpioPortC, .pin = 0},
  .MISO = GPIO_UNUSED,
  .SCK = {.port = gpioPortC, .pin = 2},
  .nCS = {.port = gpioPortC, .pin = 6},
  // nCS2 for memory LCD is EXTCOMIN
  .nCS2 = {.port = gpioPortA, .pin = 0},
  .DC = GPIO_UNUSED,
  .BUSY = GPIO_UNUSED,
  .nRST = GPIO_UNUSED,
  .enable = {.port = gpioPortC, .pin = 7, .idle_state = 1},
  .type = WSTK_MEMLCD
};

static const oepl_efr32xg22_displayconfig_t displayconfig_brd4402_epd =
{
  .usart = USART1,
  .usart_clock = cmuClock_USART1,
  .MOSI = {
    // EXP 12
    .port = gpioPortA,
    .pin = 5
  },
  .MISO = GPIO_UNUSED,
  .SCK = {
    // EXP 14
    .port = gpioPortA,
    .pin = 6
  },
  .nCS = {
    // EXP 16
    .port = gpioPortB,
    .pin = 3
  },
  .nCS2 = GPIO_UNUSED,
  .DC = {
    // EXP11, override of LED 0
    .port = gpioPortD,
    .pin = 2
  },
  .BUSY = {
    // EXP15
    .port = gpioPortB,
    .pin= 2
  },
  .nRST = {
    // EXP10
    .port = gpioPortC,
    .pin = 3
  },
  .enable = GPIO_UNUSED,
  .type = EPD_SEEED_264_176_BWR
};

static const oepl_efr32xg22_pinconfig_t pinconfig_brd4402b = {
  .gpio = {.port = gpioPortInvalid},
  .nfc_fd = {.port = gpioPortInvalid },
  .nfc_fd_em4wuval = 0,
  .button1 = {.port = gpioPortB, .pin = 0},
  .button1_em4wuval = 0,
  .button2 = {.port = gpioPortB, .pin = 1},
  .button2_em4wuval = GPIO_IEN_EM4WUIEN3
};

static const oepl_efr32xg22_ledconfig_t ledconfig_brd4402b = {
  // Use LED 1
  .white = {.port = gpioPortD, .pin = 3},
  .blue = GPIO_UNUSED,
  .red = GPIO_UNUSED,
  .green = GPIO_UNUSED
};

static const oepl_efr32xg22_debugconfig_t debugconfig_brd4402b_swo = {
  .output = {
    .euart = {
      .tx = {.port = gpioPortA, .pin = 5},
      .rx = {.port = gpioPortA, .pin = 6},
      .rts = GPIO_UNUSED,
      .cts = GPIO_UNUSED,
      .enable = {.port = gpioPortB, .pin = 4},
    }
  },
  .type = DBG_SWO
};

static const oepl_efr32xg22_debugconfig_t debugconfig_brd4402b_euart = {
  .output = {
    .euart = {
      .tx = {.port = gpioPortA, .pin = 5},
      .rx = {.port = gpioPortA, .pin = 6},
      .rts = GPIO_UNUSED,
      .cts = GPIO_UNUSED,
      .enable = {.port = gpioPortB, .pin = 4},
    }
  },
  .type = DBG_EUART
};

static const oepl_efr32xg22_tagconfig_t tagconfig_brd4402b_memlcd = {
  .hwtype = BRD4402B_WSTK,
  .oepl_hwid = 0xDC,
  .flash = &flashconfig_brd4402b,
  .display = &displayconfig_brd4402_memlcd,
  .gpio = &pinconfig_brd4402b,
  .led = &ledconfig_brd4402b,
  .nfc = NULL,
  .debug = &debugconfig_brd4402b_euart
};

static const oepl_efr32xg22_tagconfig_t tagconfig_brd4402b_epd = {
  .hwtype = BRD4402B_WSTK_EPD,
  .oepl_hwid = 0xDD,
  .flash = &flashconfig_brd4402b,
  .display = &displayconfig_brd4402_epd,
  .gpio = &pinconfig_brd4402b,
  .led = &ledconfig_brd4402b,
  .nfc = NULL,
  .debug = &debugconfig_brd4402b_swo
};

// -----------------------------------------------------------------------------
//                           Solum universal config
// -----------------------------------------------------------------------------
static const oepl_efr32xg22_displayconfig_t displayconfig_solum =
{
  .usart = USART1,
  .usart_clock = cmuClock_USART1,
  .MOSI = {.port = gpioPortA, .pin = 3},
  .MISO = GPIO_UNUSED,
  .SCK = {.port = gpioPortA, .pin = 4},
  .nCS = {.port = gpioPortB, .pin = 0 },
  .nCS2 = GPIO_UNUSED,
  .DC = {.port = gpioPortA, .pin = 6},
  .BUSY = {.port = gpioPortA, .pin = 8},
  .nRST = {.port = gpioPortA, .pin = 7},
  .enable = {.port = gpioPortA, .pin = 0, .idle_state = 0},
  .type = EPD_SOLUM_AUTODETECT
};

static const oepl_efr32xg22_pinconfig_t pinconfig_solum = {
  .gpio = GPIO_UNUSED,
  // NOTE: Ports C and D are not available for IRQ generation in regular sleep.
  // This means NFC wake is only supported from deep sleep, and won't work during
  // regular operation.
  .nfc_fd = {.port = gpioPortD, .pin = 2},
  .nfc_fd_em4wuval = GPIO_IEN_EM4WUIEN9,
  .button1 = {.port = gpioPortB, .pin = 1},
  .button1_em4wuval = GPIO_IEN_EM4WUIEN3,
  .button2 = {.port = gpioPortA, .pin = 5},
  .button2_em4wuval = GPIO_IEN_EM4WUIEN0
};

static const oepl_efr32xg22_ledconfig_t ledconfig_solum = {
  .white = GPIO_UNUSED,
  .blue = {.port = gpioPortC, .pin = 5},
  .red = {.port = gpioPortC, .pin = 6},
  .green = {.port = gpioPortC, .pin = 7}
};

static const oepl_efr32xg22_nfcconfig_t nfcconfig_solum = {
  .i2c = I2C0,
  .SDA = {.port = gpioPortD, .pin = 3},
  .SCL = {.port = gpioPortD, .pin = 1},
  .power = {.port = gpioPortD, .pin = 0}
};

static const oepl_efr32xg22_debugconfig_t debugconfig_solum = {
  .type = DBG_EUART,
  .output = {
    .euart = {
      .tx = {.port = gpioPortB, .pin = 2},
      .rx = {.port = gpioPortB, .pin = 3},
      .rts = GPIO_UNUSED,
      .cts = GPIO_UNUSED,
      .enable = GPIO_UNUSED,
    }
  }
};

static const oepl_efr32xg22_tagconfig_t tagconfig_solum = {
  .hwtype = SOLUM_AUTODETECT,
  .oepl_hwid = 0, // Autodetect based on userdata
  .flash = &flashconfig_solum,
  .display = &displayconfig_solum,
  .gpio = &pinconfig_solum,
  .led = &ledconfig_solum,
  .nfc = &nfcconfig_solum,
  .debug = &debugconfig_solum
};

// -----------------------------------------------------------------------------
//                           Modchip
// -----------------------------------------------------------------------------
static const oepl_efr32xg22_displayconfig_t displayconfig_modchip_hd150 =
{
  .usart = USART1,
  .usart_clock = cmuClock_USART1,
  .MOSI = {.port = gpioPortA, .pin = 4},
  .MISO = GPIO_UNUSED,
  .SCK = {.port = gpioPortB, .pin = 0},
  .nCS = {.port = gpioPortA, .pin = 0},
  .nCS2 = {.port = gpioPortA, .pin = 3},
  .DC = {.port = gpioPortD, .pin = 1},
  .BUSY = {.port = gpioPortB, .pin = 2},
  .nRST = {.port = gpioPortD, .pin = 0},
  .enable = {.port = gpioPortA, .pin = 6, .idle_state = 0},
  .type = EPD_HD150
};

static const oepl_efr32xg22_pinconfig_t pinconfig_modchip = {
  .gpio = GPIO_UNUSED,
  .nfc_fd = GPIO_UNUSED,
  .nfc_fd_em4wuval = 0,
  .button1 = {.port = gpioPortB, .pin = 1},
  .button1_em4wuval = GPIO_IEN_EM4WUIEN3,
  .button2 = GPIO_UNUSED,
  .button2_em4wuval = 0
};

static const oepl_efr32xg22_ledconfig_t ledconfig_modchip = {
  .white = GPIO_UNUSED,
  .blue = GPIO_UNUSED,
  .red = GPIO_UNUSED,
  .green = GPIO_UNUSED
};

static const oepl_efr32xg22_debugconfig_t debugconfig_modchip = {
  .type = DBG_EUART,
  .output = {
    .euart = {
      .tx = {.port = gpioPortA, .pin = 5},
      .rx = GPIO_UNUSED,
      .rts = GPIO_UNUSED,
      .cts = GPIO_UNUSED,
      .enable = GPIO_UNUSED,
    }
  }
};

static const oepl_efr32xg22_tagconfig_t tagconfig_modchip_hd150 = {
  .hwtype = MODCHIP_HD150,
  .oepl_hwid = MODCHIP_HD150_BWR_58,
  .flash = &flashconfig_modchip,
  .display = &displayconfig_modchip_hd150,
  .gpio = &pinconfig_modchip,
  .led = &ledconfig_modchip,
  .nfc = NULL,
  .debug = &debugconfig_modchip
};

// -----------------------------------------------------------------------------
//                           Other hardware
// -----------------------------------------------------------------------------
// ----- Add new HW types here ----

// ----- ^^^^^^^^^^^^^^^^^^^^^ ----

// -----------------------------------------------------------------------------
//                  Tag config database for universal firmware
// -----------------------------------------------------------------------------

static const oepl_efr32xg22_tagconfig_t* tagdb[] = {
  &tagconfig_brd4402b_memlcd,
  &tagconfig_brd4402b_epd,
  &tagconfig_solum,
  &tagconfig_modchip_hd150,
};

const oepl_efr32xg22_tagconfig_t* oepl_efr32xg22_get_config(void)
{
  const ApplicationProperties_t* app_p = *((const ApplicationProperties_t**) (13*4));
  uint8_t btl_id = app_p->app.version & 0xFF;

  for(size_t i = 0; i < sizeof(tagdb) / sizeof(tagdb[0]); i++) {
    if(tagdb[i]->hwtype == btl_id) {
      return tagdb[i];
    }
  }

  return NULL;
}

uint8_t oepl_efr32xg22_get_oepl_hwid(void)
{
  const oepl_efr32xg22_tagconfig_t* tagcfg = oepl_efr32xg22_get_config();
  if(tagcfg == NULL) {
    return 0;
  }

  if(tagcfg->hwtype == SOLUM_AUTODETECT) {
    uint8_t solum_tagtype = *((uint8_t*) (USERDATA_BASE + 0x16));
    switch(solum_tagtype) {
      case STYPE_SIZE_016:
        return SOLUM_M3_BWR_16;
      case STYPE_SIZE_022:
        return SOLUM_M3_BWR_22;
      case STYPE_SIZE_022_LITE:
        return SOLUM_M3_BWR_22_LITE;
      case STYPE_SIZE_026:
        return SOLUM_M3_BWR_26;
      case STYPE_SIZE_029:
        return SOLUM_M3_BWR_29;
      case STYPE_SIZE_029_FREEZER:
        return SOLUM_M3_BW_29;
      case STYPE_SIZE_042:
        return SOLUM_M3_BWR_42;
      case STYPE_SIZE_043:
        return SOLUM_M3_BWR_43;
      case STYPE_SIZE_058:
        return SOLUM_M3_BWR_58;
      case STYPE_SIZE_058_FREEZER:
        return SOLUM_M3_BW_58;
      case STYPE_SIZE_060:
        return SOLUM_M3_BWR_60;
      case STYPE_SIZE_075:
        return SOLUM_M3_BWR_75;
      case STYPE_SIZE_097:
        return SOLUM_M3_BWR_97;
      case STYPE_SIZE_013:
        return SOLUM_M3_PEGHOOK_BWR_13;
      case STYPE_SIZE_16_BWRY:
        return SOLUM_M3_BWRY_16;
      case STYPE_SIZE_16_BWRY_HIGHRES:
        return SOLUM_M3_BWRY_16_HIGHRES;
      case STYPE_SIZE_22_BWRY:
        return SOLUM_M3_BWRY_22;
      case STYPE_SIZE_24_BWRY:
        return SOLUM_M3_BWRY_24;
      case STYPE_SIZE_29_BWRY:
        return SOLUM_M3_BWRY_29;
      case STYPE_SIZE_30_BWRY:
        return SOLUM_M3_BWRY_30;
      case STYPE_SIZE_43_BWRY:
        return SOLUM_M3_BWRY_43;
      case STYPE_SIZE_75_BWRY:
        return SOLUM_M3_BWRY_75;
      default:
        return 0;
    }
  } else {
    return tagcfg->oepl_hwid;
  }
}

uint8_t oepl_efr32xg22_get_oepl_hwcapa(void)
{
  const oepl_efr32xg22_tagconfig_t* tagcfg = oepl_efr32xg22_get_config();
  if(tagcfg == NULL) {
    return 0;
  }

  if(tagcfg->hwtype == SOLUM_AUTODETECT) {
    uint8_t solum_capa0 = *((uint8_t*) (USERDATA_BASE + 0x12));
    uint8_t solum_capa1 = *((uint8_t*) (USERDATA_BASE + 0x13));
    uint8_t capabyte = 0;

    // Compression is a firmware attribute
    capabyte |= CAPABILITY_SUPPORTS_COMPRESSION;

    // Figure out whether we have buttons
    if((solum_capa0 & 0x80) != 0 || (solum_capa1 & 0x01) != 0) {
      capabyte |= CAPABILITY_HAS_WAKE_BUTTON;
    }

    // Figure out whether we have LED
    if((solum_capa1 & 0x10) != 0) {
      capabyte |= CAPABILITY_HAS_LED;
    }

    // All tags have NFC?
    capabyte |= CAPABILITY_HAS_NFC;
    capabyte |= CAPABILITY_NFC_WAKE;

    return capabyte;
  } else {
    // construct capabyte from hardcoded parameters
    uint8_t capabyte = 0;
    capabyte |= CAPABILITY_SUPPORTS_COMPRESSION;
    if(tagcfg->led != NULL && (
        tagcfg->led->white.port != gpioPortInvalid ||
        tagcfg->led->red.port != gpioPortInvalid ||
        tagcfg->led->green.port != gpioPortInvalid ||
        tagcfg->led->blue.port != gpioPortInvalid)) {
      // Have at least one LED
      capabyte |= CAPABILITY_HAS_LED;
    }

    if(tagcfg->nfc != NULL) {
      capabyte |= CAPABILITY_HAS_NFC;
      capabyte |= CAPABILITY_NFC_WAKE;
    }

    if(tagcfg->gpio != NULL && (
        tagcfg->gpio->button1.port != gpioPortInvalid ||
        tagcfg->gpio->button2.port != gpioPortInvalid)) {
      capabyte |= CAPABILITY_HAS_WAKE_BUTTON;
    }

    return capabyte;
  }
}

bool oepl_efr32xg22_get_displayparams(oepl_efr32xg22_displayparams_t* displayparams)
{
  const oepl_efr32xg22_tagconfig_t* tagcfg = oepl_efr32xg22_get_config();
  if(tagcfg == NULL || tagcfg->display == NULL) {
    return false;
  }
  
  if(tagcfg->display->type == EPD_SOLUM_AUTODETECT) {
    uint8_t solum_ctrltype = *((uint8_t*) (USERDATA_BASE + 0x09));
    uint8_t solum_colortype = *((uint8_t*) (USERDATA_BASE + 0x0A));
    uint16_t solum_xres = *((uint8_t*) (USERDATA_BASE + 0x0B)) + (((uint16_t)(*((uint8_t*) (USERDATA_BASE + 0x0C)))) << 8);
    uint16_t solum_yres = *((uint8_t*) (USERDATA_BASE + 0x0D)) + (((uint16_t)(*((uint8_t*) (USERDATA_BASE + 0x0E)))) << 8);
    uint8_t solum_tagtype = *((uint8_t*) (USERDATA_BASE + 0x16));

    displayparams->xres = solum_xres;
    displayparams->yres = solum_yres;
    displayparams->have_thirdcolor = (solum_colortype == 0x01 || solum_colortype == 0x02 || solum_colortype == 0x03);
    displayparams->have_fourthcolor = solum_colortype == 0x03;

    switch(solum_ctrltype) {
      case 0x0F:
      case 0x12:
      case 0x15:
      case 0x19:
        if (solum_xres == 792 && solum_yres == 272) {
          displayparams->ctrl = CTRL_DUALSSD;
        } else {
          displayparams->ctrl = CTRL_SSD;
        }
        break;
      case 0x0D:
        displayparams->ctrl = CTRL_EPDVAR29;
        break;
      case 0x0E:
      case 0x1A:  // 4.3 variant with buttons? probably var43
        displayparams->ctrl = CTRL_EPDVAR43;
        break;
      case 0x11:
        displayparams->ctrl = CTRL_UC8159;
        break;
      case 0x10:
        displayparams->ctrl = CTRL_UC8179;
        break;
      case 0x17:
        // Drycoded from nRF52 firmware. May not work, no samples available.
        displayparams->ctrl = CTRL_UCBWRY;
        break;
      // Maybe these are the same?
      case 0x1C:
        // 1.6" BWRY
      case 0x1E:
        // 2.2" BWRY WT
      case 0x20:
        // 2.9" BWRY
      case 0x2C:
        // 7.5" BWRY
      case 0x2A:
        // 4.3" BWRY
        displayparams->ctrl = CTRL_JD;
        break;
      default:
        return false;
    }

    // Set default values
    displayparams->xres_working = solum_xres;
    displayparams->yres_working = solum_yres;

    displayparams->xoffset = 0;
    displayparams->yoffset = 0;

    displayparams->mirrorX = false;
    displayparams->mirrorY = false;
    displayparams->swapXY = false;

    // Apply overrides based on tagtype
    switch(solum_tagtype) {
      case STYPE_SIZE_016:
        displayparams->mirrorY = true;
        break;
      case STYPE_SIZE_022:
        displayparams->swapXY = true;
        displayparams->xoffset = 8;
        break;
      case STYPE_SIZE_022_LITE:
        displayparams->swapXY = true;
        displayparams->xoffset = 8;
        break;
      case STYPE_SIZE_026:
        displayparams->swapXY = true;
        displayparams->xoffset = 8;
        break;
      case STYPE_SIZE_029:
        displayparams->swapXY = true;
        displayparams->xoffset = 8;
        break;
      case STYPE_SIZE_029_FREEZER:
        displayparams->swapXY = true;
        displayparams->xoffset = 8;
        break;
      case STYPE_SIZE_042:
        displayparams->mirrorY = true;
        break;
      case STYPE_SIZE_043:
        displayparams->swapXY = true;
        break;
      case STYPE_SIZE_058:
        break;
      case STYPE_SIZE_058_FREEZER:
        break;
      case STYPE_SIZE_060:
        break;
      case STYPE_SIZE_075:
        displayparams->xres = solum_yres;
        displayparams->yres = solum_xres;
        displayparams->xres_working = displayparams->xres;
        displayparams->yres_working = displayparams->yres;
        break;
      case STYPE_SIZE_097:
        displayparams->swapXY = true;
        break;
      case STYPE_SIZE_013:
        displayparams->swapXY = true;
        displayparams->xoffset = 8;
        displayparams->xres_working = displayparams->yres;
        displayparams->yres_working = displayparams->xres;
        break;
      case STYPE_SIZE_16_BWRY:
        displayparams->swapXY = false;
        displayparams->mirrorY = true;
        break;
      case STYPE_SIZE_24_BWRY:
        displayparams->swapXY = true;
        break;
      case STYPE_SIZE_16_BWRY_HIGHRES:
        displayparams->swapXY = false;
        displayparams->mirrorY = false;
        break;
      case STYPE_SIZE_22_BWRY:
        // Todo: BWRY support?
        break;
      case STYPE_SIZE_29_BWRY:
        displayparams->swapXY = true;
        break;
      case STYPE_SIZE_30_BWRY:
        // Todo: BWRY support?
        break;
      case STYPE_SIZE_43_BWRY:
        // Todo: BWRY support?
        break;
      case STYPE_SIZE_75_BWRY:
        // Todo: BWRY support?
        break;
      default:
        return false;
    }

    if(displayparams->swapXY) {
      displayparams->xres_working = solum_yres;
      displayparams->yres_working = solum_xres;
    }

    return true;
  } else if(tagcfg->display->type == EPD_SEEED_264_176_BWR) {
    displayparams->xres = 264;
    displayparams->yres = 176;
    displayparams->xres_working = 176;
    displayparams->yres_working = 264;
    displayparams->xoffset = 0;
    displayparams->yoffset = 0;
    displayparams->have_thirdcolor = true;
    displayparams->have_fourthcolor = false;
    displayparams->swapXY = true;
    displayparams->mirrorX = false;
    displayparams->mirrorY = false;
    displayparams->ctrl = CTRL_IL91874;
    return true;
  } else if(tagcfg->display->type == WSTK_MEMLCD) {
    displayparams->xres = 128;
    displayparams->yres = 128;
    displayparams->xres_working = 128;
    displayparams->yres_working = 128;
    displayparams->xoffset = 0;
    displayparams->yoffset = 0;
    displayparams->have_thirdcolor = false;
    displayparams->have_fourthcolor = false;
    displayparams->swapXY = false;
    displayparams->mirrorX = false;
    displayparams->mirrorY = false;
    displayparams->ctrl = CTRL_MEMLCD;
    return true;
  } else if(tagcfg->display->type == EPD_HD150) {
    displayparams->xres = 648;
    displayparams->yres = 480;
    displayparams->xres_working = 648;
    displayparams->yres_working = 480;
    displayparams->xoffset = 0;
    displayparams->yoffset = 0;
    displayparams->have_thirdcolor = true;
    displayparams->have_fourthcolor = false;
    displayparams->swapXY = false;
    displayparams->mirrorX = false;
    displayparams->mirrorY = false;
    displayparams->ctrl = CTRL_GDEW0583Z83;
    return true;
  // ----- Add new HW types here ----

  // ----- ^^^^^^^^^^^^^^^^^^^^^ ----
  } else {
    return false;
  }
}
