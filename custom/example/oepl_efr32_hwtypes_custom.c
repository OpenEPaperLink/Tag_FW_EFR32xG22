#include "oepl_efr32_hwtypes.h"
#include "application_properties.h"
#include "oepl-definitions.h"

#include "oepl_hw_abstraction.h"
#define DPRINTF(fmt_, ...) oepl_hw_debugprint(DBG_DISPLAY, (fmt_), ##__VA_ARGS__)

#define GPIO_UNUSED {.port = gpioPortInvalid, .pin = 0, .idle_state = 0}

// -----------------------------------------------------------------------------
//                              Flash pinouts
// -----------------------------------------------------------------------------

static const oepl_efr32xg22_flashconfig_t flashconfig_solum =
{
  .usart = USART0,
  .MOSI = {.port = gpioPortC, .pin = 1},
  .MISO = {.port = gpioPortC, .pin = 0},
  .SCK = {.port = gpioPortC, .pin = 2},
  .nCS = {.port = gpioPortC, .pin = 3},
  .EN = GPIO_UNUSED
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

// Solum controller with PDI 7.4" BWRY EPD (E2741QS0B3)
static const oepl_efr32xg22_tagconfig_t tagconfig_custom = {
  .hwtype = CTRL_JD,
//   .oepl_hwid = 0x4c,    // M3 7.5" BWRY
  .oepl_hwid = 0xa3,
  .flash = &flashconfig_solum,
  .display = &displayconfig_solum,
  .gpio = &pinconfig_solum,
  .led = &ledconfig_solum,
  .nfc = &nfcconfig_solum,
  .debug = &debugconfig_solum
};




// ----- ^^^^^^^^^^^^^^^^^^^^^ ----

// -----------------------------------------------------------------------------
//                  Tag config database for universal firmware
// -----------------------------------------------------------------------------

const oepl_efr32xg22_tagconfig_t* oepl_efr32xg22_get_config(void)
{
   return &tagconfig_custom;
}

uint8_t oepl_efr32xg22_get_oepl_hwid(void)
{
  return tagconfig_custom.oepl_hwid;
}

uint8_t oepl_efr32xg22_get_oepl_hwcapa(void) {
   const oepl_efr32xg22_tagconfig_t* tagcfg = &tagconfig_custom;

   // construct capabyte from hardcoded parameters
   uint8_t capabyte = 0;
   capabyte |= CAPABILITY_SUPPORTS_COMPRESSION;
   if (tagcfg->led != NULL 
       && (tagcfg->led->white.port != gpioPortInvalid
           || tagcfg->led->red.port != gpioPortInvalid
           || tagcfg->led->green.port != gpioPortInvalid 
           || tagcfg->led->blue.port != gpioPortInvalid)) 
   {
      // Have at least one LED
      capabyte |= CAPABILITY_HAS_LED;
   }

   if (tagcfg->nfc != NULL) {
      capabyte |= CAPABILITY_HAS_NFC;
      capabyte |= CAPABILITY_NFC_WAKE;
   }

   if (tagcfg->gpio != NULL 
       && (tagcfg->gpio->button1.port != gpioPortInvalid 
           || tagcfg->gpio->button2.port != gpioPortInvalid)) 
   {
      capabyte |= CAPABILITY_HAS_WAKE_BUTTON;
   }

   return capabyte;
}

bool oepl_efr32xg22_get_displayparams(oepl_efr32xg22_displayparams_t* displayparams)
{
  displayparams->xres = 480;
  displayparams->yres = 800;
  displayparams->xres_working = 480;
  displayparams->yres_working = 800;
  displayparams->xoffset = 0;
  displayparams->yoffset = 0;
  displayparams->have_thirdcolor = true;
  displayparams->have_fourthcolor = true;
  displayparams->swapXY = true;
  displayparams->mirrorX = false;
#if CABLE_AT_BOTTOM
  displayparams->mirrorY = false;
#else
  displayparams->mirrorY = true;
#endif
  displayparams->ctrl = CTRL_JD;
  return true;
}

