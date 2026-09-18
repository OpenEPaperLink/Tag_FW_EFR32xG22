// -----------------------------------------------------------------------------
//                                   Includes
// -----------------------------------------------------------------------------
#include "oepl_hw_abstraction.h"
#include "oepl_nvm.h"
#include "oepl_app.h"
#include "oepl_radio.h"
#include "oepl_display.h"

#include "em_cmu.h"
#include "em_gpio.h"
#include "em_emu.h"
#include "em_wdog.h"
#include "em_i2c.h"
#include "em_iadc.h"
#include "em_rmu.h"
#include "gpiointerrupt.h"
#include "sl_sleeptimer.h"
#include "sl_power_manager.h"
#include "app_properties_config.h"

#include <stdio.h>
#include <stdarg.h>

#include "sl_udelay.h"
#include "sl_mx25_flash_shutdown.h"
#include "sl_rail_util_pti_config.h"
#include "oepl_efr32_hwtypes.h"
#include <sl_iostream_handles.h>
#include "sl_rail_util_init.h"
#include "rail.h"

// Todo: abstract away for boards

// -----------------------------------------------------------------------------
//                              Configuration values
// -----------------------------------------------------------------------------
#ifndef HW_ABSTRACTION_DEBUG_PRINT
#define HW_ABSTRACTION_DEBUG_PRINT 1
#endif

// -----------------------------------------------------------------------------
//                              Macros and Typedefs
// -----------------------------------------------------------------------------
#if HW_ABSTRACTION_DEBUG_PRINT
#define DPRINTF(fmt_, ...) oepl_hw_debugprint(DBG_HW, (fmt_), ##__VA_ARGS__)
#else
#define DPRINTF(...)
#endif

/* NFC record types (rec_type field). Wire bytes an agent must ground on --
 * kept here (not in per-repo constants) for that reason. */
#define OD_NFC_REC_TEXT                0u
#define OD_NFC_REC_URI                 1u
#define OD_NFC_REC_WELL_KNOWN_RAW      2u
#define OD_NFC_REC_MIME                3u
#define OD_NFC_REC_RAW_NDEF            4u

// -----------------------------------------------------------------------------
//                          Static Function Declarations
// -----------------------------------------------------------------------------
static void em_cb(sl_power_manager_em_t from, sl_power_manager_em_t to);
static void gpioint_cb(uint8_t pin, void* ctx);
static void hardware_error_handler(void);
static void tnb132m_probe(const oepl_efr32xg22_tagconfig_t* tagconfig);
static void tnb132m_write_ndef_text(const oepl_efr32xg22_tagconfig_t* tagconfig, const char* text);
static bool od_nfc_write_record_raw(const oepl_efr32xg22_tagconfig_t* tagconfig, uint8_t rec_type, const uint8_t *data, uint16_t data_len);

// -----------------------------------------------------------------------------
//                                Global Variables
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
//                                Static Variables
// -----------------------------------------------------------------------------
static uint8_t hwid;

static sl_power_manager_em_transition_event_handle_t event_handle;
static const sl_power_manager_em_transition_event_info_t event_info = {
    .event_mask = SL_POWER_MANAGER_EVENT_TRANSITION_ENTERING_EM2 | SL_POWER_MANAGER_EVENT_TRANSITION_ENTERING_EM3 |
                  SL_POWER_MANAGER_EVENT_TRANSITION_LEAVING_EM2 | SL_POWER_MANAGER_EVENT_TRANSITION_LEAVING_EM3,
    .on_event = em_cb
};

static uint8_t button1_hwval, button2_hwval, gpio_hwval, nfcfd_hwval, nfcpwr_hwval, nfcsda_hwval;
static uint8_t white_hwval, red_hwval, blue_hwval, green_hwval;
static void* gpio_cb = NULL;

static bool is_devkit = false;

// Deepsleep entry timer will periodically check if the radio is idle so that
// we can enter deepsleep.
static sl_sleeptimer_timer_handle_t deepsleep_entry_timer_handle;
static sl_sleeptimer_timer_handle_t nfc_poll_timer_handle;

// -----------------------------------------------------------------------------
//                          Public Function Definitions
// -----------------------------------------------------------------------------

// Override of a GSDK power manager application hook. Tells the power manager
// (called from the main event loop) whether it is OK to go to sleep.
bool app_is_ok_to_sleep(void)
{
  if(oepl_display_is_drawing()) {
    //DPRINTF("$");
    return true;
  } else if(oepl_radio_is_event_pending() || oepl_app_is_event_pending()) {
    DPRINTF("@");
    return false;
  } else {
    return true;
  }
}

// Override of a GSDK power manager application hook. Tells the power manager
// (called from the main event loop) whether the application has events to
// process (otherwise it might go back down to sleep immediately).
sl_power_manager_on_isr_exit_t app_sleep_on_isr_exit(void)
{
  // The things we might have been awoken for:
  // - radio event
  // - button press
  // - application timer (todo)
  // - other subtask event, e.g. display SPI (todo)
  if(oepl_radio_is_event_pending() || oepl_app_is_event_pending() || oepl_display_is_drawing()) {
    return SL_POWER_MANAGER_WAKEUP;
  } else {
    return SL_POWER_MANAGER_SLEEP;
  }
}

static void nfc_poll_timer_cb(sl_sleeptimer_timer_handle_t *handle, void *data)
{
  (void)handle;
  (void)data;
  //GPIO_PinModeSet((nfcsda_hwval & 0x70) >> 4, nfcsda_hwval & 0xF, gpioModePushPull, 1);
  
  if((nfcfd_hwval & 0x70) == 0x20 ||
     (nfcfd_hwval & 0x70) == 0x30) {
    // FD pin is on port C or D, meaning we need to do manual polling.
    if(GPIO_PinInGet((nfcfd_hwval & 0x70) >> 4, nfcfd_hwval & 0xF)) {
      gpioint_cb(nfcfd_hwval & 0xF, gpio_cb);
    }
  }

  if((nfcpwr_hwval & 0x70) == 0x20 ||
     (nfcpwr_hwval & 0x70) == 0x30) {
    // FD pin is on port C or D, meaning we need to do manual polling.
    if(GPIO_PinInGet((nfcpwr_hwval & 0x70) >> 4, nfcpwr_hwval & 0xF)) {
      gpioint_cb(nfcfd_hwval & 0xF, gpio_cb);
    }
  }

  //GPIO_PinModeSet((nfcsda_hwval & 0x70) >> 4, nfcsda_hwval & 0xF, gpioModeInput, 0);
}

void oepl_hw_init(void)
{
  // Disable these pins, they're decoy pins used to make the SDK's debug EUART config happy
  GPIO_PinModeSet(gpioPortB, 5, gpioModeDisabled, 1);
  GPIO_PinModeSet(gpioPortB, 6, gpioModeDisabled, 1);
  GPIO_PinModeSet(gpioPortB, 7, gpioModeDisabled, 1);
  GPIO_PinModeSet(gpioPortB, 8, gpioModeDisabled, 1);

  const oepl_efr32xg22_tagconfig_t* tagconfig = oepl_efr32xg22_get_config();
  if(tagconfig == NULL) {
    // Shoot this in the ether. It may or may not work since no hardware has been set up.
    DPRINTF("No hardware configuration defined\n");
    while(1) {
      sl_power_manager_sleep();
    }
  }

  // Setup debugprint infrastructure
  extern sl_iostream_instance_info_t sl_iostream_instance_euart_debug_info;
  #if SL_CATALOG_IOSTREAM_RTT_PRESENT
  extern sl_iostream_instance_info_t sl_iostream_instance_rtt_info;
  #endif
  extern sl_iostream_instance_info_t sl_iostream_instance_swo_info;

  if(tagconfig->debug->type != DBG_SWO) {
    // Stop messing with the SWO pin as we may be reusing it somewhere else
    GPIO_DbgSWOEnable(false);
    GPIO_PinModeSet((GPIO_Port_TypeDef)GPIO_SWV_PORT, GPIO_SWV_PIN, gpioModeDisabled, 0);
  }

  if(tagconfig->debug->type != DBG_EUART) {
    // Turn off EUART when not in use to avoid it taking control over pins it shouldn't
    extern sl_iostream_uart_t *sl_iostream_uart_euart_debug_handle;
    sl_iostream_uart_deinit(sl_iostream_uart_euart_debug_handle);
  }

  // Set the requested debug print output
  switch(tagconfig->debug->type)
  {
    case DBG_SWO:
      sl_iostream_set_system_default(sl_iostream_instance_swo_info.handle);
      break;
    #if SL_CATALOG_IOSTREAM_RTT_PRESENT
    case DBG_RTT:
      sl_iostream_set_system_default(sl_iostream_instance_rtt_info.handle);
      break;
    #endif
    case DBG_EUART:
      // Adjust the pinout for the EUART
      GPIO_PinModeSet(tagconfig->debug->output.euart.tx.port, tagconfig->debug->output.euart.tx.pin, gpioModePushPull, 1);

      GPIO->EUARTROUTE->TXROUTE = (tagconfig->debug->output.euart.tx.port << _GPIO_EUART_TXROUTE_PORT_SHIFT)
                                | (tagconfig->debug->output.euart.tx.pin << _GPIO_EUART_TXROUTE_PIN_SHIFT);
      if(tagconfig->debug->output.euart.rx.port != gpioPortInvalid) {
        GPIO_PinModeSet(tagconfig->debug->output.euart.rx.port, tagconfig->debug->output.euart.rx.pin, gpioModeInput, 0);
        GPIO->EUARTROUTE->RXROUTE = (tagconfig->debug->output.euart.rx.port << _GPIO_EUART_RXROUTE_PORT_SHIFT)
                                  | (tagconfig->debug->output.euart.rx.pin << _GPIO_EUART_RXROUTE_PIN_SHIFT);
      }

      if(tagconfig->debug->output.euart.enable.port != gpioPortInvalid) {
        GPIO_PinModeSet(tagconfig->debug->output.euart.enable.port, tagconfig->debug->output.euart.enable.pin, gpioModePushPull, 1);
      }

      sl_iostream_set_system_default(sl_iostream_instance_euart_debug_info.handle);
      break;
    default:
      DPRINTF("Unrecognised debug output\n");
      while(1) {
        sl_power_manager_sleep();
      }
  }

  // Now that we have debug output, log a message if we're coming back from a HW error
  uint32_t resetCause = RMU_ResetCauseGet();
  RMU_ResetCauseClear();

  if (resetCause & EMU_RSTCAUSE_WDOG0) {
    DPRINTF("*** Rebooted due to catching a hardware fault ***\n");
  }

  CMU_ClockEnable(cmuClock_WDOG0, true);
  CMU_ClockSelectSet(cmuClock_WDOG0, cmuSelect_ULFRCO);

  // Setup flash
  if(tagconfig->hwtype == BRD4402B_WSTK ||
     tagconfig->hwtype == BRD4402B_WSTK_EPD ||
     tagconfig->hwtype == MODCHIP_HD150) {
    is_devkit = true;
    //oepl_hw_flash_deepsleep();
  }
  
  // Setup pins
  if(tagconfig->gpio) {
    if(tagconfig->gpio->button1.port != gpioPortInvalid) {
      GPIO_PinModeSet(tagconfig->gpio->button1.port, tagconfig->gpio->button1.pin, gpioModeInputPullFilter, 1);
      button1_hwval = 0x80 | tagconfig->gpio->button1.port << 4| tagconfig->gpio->button1.pin;
    } else {
      button1_hwval = 0;
    }

    if(tagconfig->gpio->button2.port != gpioPortInvalid) {
      GPIO_PinModeSet(tagconfig->gpio->button2.port, tagconfig->gpio->button2.pin, gpioModeInputPullFilter, 1);
      button2_hwval = 0x80 | tagconfig->gpio->button2.port << 4| tagconfig->gpio->button2.pin;
    } else {
      button2_hwval = 0;
    }

    if(tagconfig->gpio->gpio.port != gpioPortInvalid) {
      GPIO_PinModeSet(tagconfig->gpio->gpio.port, tagconfig->gpio->gpio.pin, gpioModeInput, 1);
      gpio_hwval = 0x80 | tagconfig->gpio->gpio.port << 4 | tagconfig->gpio->gpio.pin;
    } else {
      gpio_hwval = 0;
    }

    if(tagconfig->gpio->nfc_fd.port != gpioPortInvalid) {
      GPIO_PinModeSet(tagconfig->gpio->nfc_fd.port, tagconfig->gpio->nfc_fd.pin, gpioModeInput, 0);
      nfcfd_hwval = 0x80 | tagconfig->gpio->nfc_fd.port << 4| tagconfig->gpio->nfc_fd.pin;
    } else {
      nfcfd_hwval = 0;
    }

    nfcpwr_hwval = 0;
    nfcsda_hwval = 0;
  } else {
    button1_hwval = 0;
    button2_hwval = 0;
    gpio_hwval = 0;
    nfcfd_hwval = 0;
    nfcpwr_hwval = 0;
    nfcsda_hwval = 0;
  }

  // Setup led(s)
  if(tagconfig->led) {
    if(tagconfig->led->white.port != gpioPortInvalid) {
      GPIO_PinModeSet(tagconfig->led->white.port, tagconfig->led->white.pin, gpioModePushPull, 1);
      white_hwval =  0x80 | tagconfig->led->white.port << 4| tagconfig->led->white.pin;
    } else {
      white_hwval = 0;
    }
    if(tagconfig->led->red.port != gpioPortInvalid) {
      GPIO_PinModeSet(tagconfig->led->red.port, tagconfig->led->red.pin, gpioModePushPull, 1);
      red_hwval =  0x80 | tagconfig->led->red.port << 4| tagconfig->led->red.pin;
    } else {
      red_hwval = 0;
    }
    if(tagconfig->led->green.port != gpioPortInvalid) {
      GPIO_PinModeSet(tagconfig->led->green.port, tagconfig->led->green.pin, gpioModePushPull, 1);
      green_hwval =  0x80 | tagconfig->led->green.port << 4| tagconfig->led->green.pin;
    } else {
      green_hwval = 0;
    }
    if(tagconfig->led->blue.port != gpioPortInvalid) {
      GPIO_PinModeSet(tagconfig->led->blue.port, tagconfig->led->blue.pin, gpioModePushPull, 1);
      blue_hwval =  0x80 | tagconfig->led->blue.port << 4| tagconfig->led->blue.pin;
    } else {
      blue_hwval = 0;
    }
  } else {
    white_hwval = 0;
    red_hwval = 0;
    green_hwval = 0;
    blue_hwval = 0;
  }

  // Setup NFC is done ad-hoc since it may involve power up/down of the NFC chip
  if(tagconfig->nfc) {
    if(tagconfig->hwtype == SOLUM_AUTODETECT) {
      // All solum EFR32BG22 based tags seem to have TNB132M NFC chips which are undocumented.
      
      // Pending useful documentation of how to talk to it, let's just use the power output to
      // detect a field.

      GPIO_PinModeSet(tagconfig->nfc->SCL.port, tagconfig->nfc->SCL.pin, gpioModeWiredAndFilter, 0);
      GPIO_PinModeSet(tagconfig->nfc->SDA.port, tagconfig->nfc->SDA.pin, gpioModeWiredAndFilter, 0);

      GPIO_PinModeSet(tagconfig->nfc->power.port, tagconfig->nfc->power.pin, gpioModeWiredOrPullDown, 1);

      // Init sequence captured on HW
      sl_udelay_wait(40000);

      {
        // Use default settings
        I2C_Init_TypeDef i2cInit = I2C_INIT_DEFAULT;

        size_t i2cnum;
        switch((uint32_t)tagconfig->nfc->i2c) {
          #if defined(I2C0)
          case (uint32_t) I2C0:
            i2cnum = 0;
            CMU_ClockEnable(cmuClock_I2C0, true);
            break;
          #endif
          #if defined(I2C1)
          case (uint32_t) I2C1:
            i2cnum = 1;
            CMU_ClockEnable(cmuClock_I2C1, true);
            break;
          #endif
          #if defined(I2C2)
          case (uint32_t) I2C2:
            i2cnum = 2;
            CMU_ClockEnable(cmuClock_I2C2, true);
            break;
          #endif
          default:
            oepl_hw_crash(DBG_HW, false, "Unknown I2C peripheral\n");
            while(1);
        }

        // Route I2C pins to GPIO
        GPIO->I2CROUTE[i2cnum].SDAROUTE = (GPIO->I2CROUTE[0].SDAROUTE & ~_GPIO_I2C_SDAROUTE_MASK)
                              | (tagconfig->nfc->SDA.port << _GPIO_I2C_SDAROUTE_PORT_SHIFT
                              | (tagconfig->nfc->SDA.pin << _GPIO_I2C_SDAROUTE_PIN_SHIFT));
        GPIO->I2CROUTE[i2cnum].SCLROUTE = (GPIO->I2CROUTE[0].SCLROUTE & ~_GPIO_I2C_SCLROUTE_MASK)
                              | (tagconfig->nfc->SCL.port << _GPIO_I2C_SCLROUTE_PORT_SHIFT
                              | (tagconfig->nfc->SCL.pin << _GPIO_I2C_SCLROUTE_PIN_SHIFT));
        GPIO->I2CROUTE[i2cnum].ROUTEEN = GPIO_I2C_ROUTEEN_SDAPEN | GPIO_I2C_ROUTEEN_SCLPEN;

        // Initialize the I2C
        I2C_Init(tagconfig->nfc->i2c, &i2cInit);

        // Enable automatic STOP on NACK
        tagconfig->nfc->i2c->CTRL = I2C_CTRL_AUTOSN;
      }

      {
        // Transfer structure
        I2C_TransferSeq_TypeDef i2cTransfer;
        I2C_TransferReturn_TypeDef result;
        uint8_t txBuffer[2];

        txBuffer[0] = 0x21;
        txBuffer[1] = 0x04;

        // Initialize I2C transfer
        i2cTransfer.addr          = 0x30 << 1;
        i2cTransfer.flags         = I2C_FLAG_WRITE;
        i2cTransfer.buf[0].data   = txBuffer;
        i2cTransfer.buf[0].len    = 2;
        i2cTransfer.buf[1].data   = NULL;
        i2cTransfer.buf[1].len    = 0;

        result = I2C_TransferInit(tagconfig->nfc->i2c, &i2cTransfer);

        // Send data
        while (result == i2cTransferInProgress) {
          result = I2C_Transfer(tagconfig->nfc->i2c);
        }

        if (result != i2cTransferDone) {
          DPRINTF("I2C fail %08x\n", result);
        }
      }

      {
        // Transfer structure
        I2C_TransferSeq_TypeDef i2cTransfer;
        I2C_TransferReturn_TypeDef result;
        uint8_t txBuffer[1 + 1];

        txBuffer[0] = 0x25;
        txBuffer[1] = 0x00;

        // Initialize I2C transfer
        i2cTransfer.addr          = 0x30 << 1;
        i2cTransfer.flags         = I2C_FLAG_WRITE_READ;
        i2cTransfer.buf[0].data   = txBuffer;
        i2cTransfer.buf[0].len    = 1;
        i2cTransfer.buf[1].data   = &txBuffer[1];
        i2cTransfer.buf[1].len    = 1;

        result = I2C_TransferInit(tagconfig->nfc->i2c, &i2cTransfer);

        // Send data
        while (result == i2cTransferInProgress) {
          result = I2C_Transfer(tagconfig->nfc->i2c);
        }

        if (result != i2cTransferDone) {
           DPRINTF("I2C fail %08x\n", result);
        } else {
          DPRINTF("I2C Response %02x\n", txBuffer[1]);
        }
      }

      sl_udelay_wait(20000);

      {
        // Transfer structure
        I2C_TransferSeq_TypeDef i2cTransfer;
        I2C_TransferReturn_TypeDef result;
        uint8_t txBuffer[1 + 16];

        txBuffer[0] = 0x30;

        // Initialize I2C transfer
        i2cTransfer.addr          = 0x43 << 1;
        i2cTransfer.flags         = I2C_FLAG_WRITE_READ;
        i2cTransfer.buf[0].data   = txBuffer;
        i2cTransfer.buf[0].len    = 1;
        i2cTransfer.buf[1].data   = txBuffer;
        i2cTransfer.buf[1].len    = 16;

        result = I2C_TransferInit(tagconfig->nfc->i2c, &i2cTransfer);

        // Send data
        while (result == i2cTransferInProgress) {
          result = I2C_Transfer(tagconfig->nfc->i2c);
        }

        if (result != i2cTransferDone) {
           DPRINTF("I2C fail %08x\n", result);
        } else {
          DPRINTF("I2C Response: ");
          for(size_t i = 0; i < sizeof(txBuffer) - 1; i++) {
            DPRINTF("%02x ", txBuffer[1+i]);
          }
          DPRINTF("\n");
        }
      }

      DPRINTF("NFC Orig content\n");
      tnb132m_probe(tagconfig);
      tnb132m_write_ndef_text(tagconfig, "HelloOEPL");
      DPRINTF("NFC rewritten content\n");
      tnb132m_probe(tagconfig);

      sl_udelay_wait(20000);

      {
        // Transfer structure
        I2C_TransferSeq_TypeDef i2cTransfer;
        I2C_TransferReturn_TypeDef result;
        uint8_t txBuffer[2];

        txBuffer[0] = 0x21;
        txBuffer[1] = 0x01;

        // Initialize I2C transfer
        i2cTransfer.addr          = 0x30 << 1;
        i2cTransfer.flags         = I2C_FLAG_WRITE;
        i2cTransfer.buf[0].data   = txBuffer;
        i2cTransfer.buf[0].len    = 2;
        i2cTransfer.buf[1].data   = NULL;
        i2cTransfer.buf[1].len    = 0;

        result = I2C_TransferInit(tagconfig->nfc->i2c, &i2cTransfer);

        // Send data
        while (result == i2cTransferInProgress) {
          result = I2C_Transfer(tagconfig->nfc->i2c);
        }

        if (result != i2cTransferDone) {
           DPRINTF("I2C fail %08x\n", result);
        }
      }

      sl_udelay_wait(14000);

      // Turn it off
      GPIO_PinOutClear(tagconfig->nfc->power.port, tagconfig->nfc->power.pin);
      GPIO_PinModeSet(tagconfig->nfc->SCL.port, tagconfig->nfc->SCL.pin, gpioModeInput, 1);
      GPIO_PinModeSet(tagconfig->nfc->SDA.port, tagconfig->nfc->SDA.pin, gpioModeInput, 1);

      GPIO_PinModeSet(tagconfig->nfc->power.port, tagconfig->nfc->power.pin, gpioModeInput, 1);

      nfcpwr_hwval = 0x80 | (tagconfig->nfc->power.port << 4) | tagconfig->nfc->power.pin;
      nfcsda_hwval = 0x80 | (tagconfig->nfc->SDA.port << 4) | tagconfig->nfc->SDA.pin;
    }
  }

  // Setup power manager infrastructure
  sl_power_manager_subscribe_em_transition_event(&event_handle, &event_info);

  // Setup application NVM
  oepl_nvm_status_t status = oepl_nvm_init_default();
  if(status == NVM_ERROR) {
    // Clean our slate
    DPRINTF("Need to autodetect, lost NVM\n");
    oepl_nvm_factory_reset(oepl_efr32xg22_get_oepl_hwid());
  }

  if(oepl_nvm_setting_get(OEPL_HWID, &hwid, sizeof(hwid)) != NVM_SUCCESS) {
    // Clean our slate
    DPRINTF("Need to autodetect, lost NVM\n");
    oepl_nvm_factory_reset(oepl_efr32xg22_get_oepl_hwid());
  }

  // Cache our HWID
  oepl_nvm_setting_get(OEPL_HWID, &hwid, sizeof(hwid));
  DPRINTF("Hello OEPL tag type 0x%02x\n", hwid);

  size_t slots, slot_size;
  oepl_nvm_get_num_img_slots(&slots, &slot_size);
  DPRINTF("Have %d image slots of %d bytes\n", slots, slot_size);

  // Setup display
  oepl_efr32xg22_displayparams_t displayconfig;
  bool is_valid = oepl_efr32xg22_get_displayparams(&displayconfig);
  if(!is_valid) {
    DPRINTF("Error: no valid display configuration\n");
    while(1) {
      sl_power_manager_sleep();
    }
  }

  oepl_display_init(&displayconfig);
}

void oepl_hw_set_led(uint8_t color, bool on)
{
  if(red_hwval || green_hwval || blue_hwval) {
    // todo: color support using PWM on a timer
    (void) color;
    if(on) {
      if(color & 0b11100000)
      GPIO_PinOutClear((red_hwval & 0x70) >> 4, red_hwval & 0x0F);
      if(color & 0b00011100)
      GPIO_PinOutClear((green_hwval & 0x70) >> 4, green_hwval & 0x0F);
      if(color & 0b00000011)
      GPIO_PinOutClear((blue_hwval & 0x70) >> 4, blue_hwval & 0x0F);
    } else {
      if(color & 0b11100000)
      GPIO_PinOutSet((red_hwval & 0x70) >> 4, red_hwval & 0x0F);
      if(color & 0b00011100)
      GPIO_PinOutSet((green_hwval & 0x70) >> 4, green_hwval & 0x0F);
      if(color & 0b00000011)
      GPIO_PinOutSet((blue_hwval & 0x70) >> 4, blue_hwval & 0x0F);
    }
  } else if(white_hwval) {
    if(on) {
      GPIO_PinOutClear((white_hwval & 0x70) >> 4, white_hwval & 0x0F);
    } else {
      GPIO_PinOutSet((white_hwval & 0x70) >> 4, white_hwval & 0x0F);
    }
  }
}

static void gpioint_cb(uint8_t pin, void* ctx)
{
  if(ctx == NULL) {
    return;
  }

  if(button1_hwval && (pin == (button1_hwval & 0xF))) {
    ((oepl_hw_gpio_cb_t)ctx)(BUTTON_1, RISING);
  }
  if(button2_hwval && (pin == (button2_hwval & 0xF))) {
    ((oepl_hw_gpio_cb_t)ctx)(BUTTON_2, RISING);
  }
  if(gpio_hwval && (pin == (gpio_hwval & 0xF))) {
    ((oepl_hw_gpio_cb_t)ctx)(GENERIC_GPIO, RISING);
  }
  if(nfcfd_hwval && (pin == (nfcfd_hwval & 0xF))) {
    ((oepl_hw_gpio_cb_t)ctx)(NFC_WAKE, RISING);
  }
}

void oepl_hw_init_gpio(oepl_hw_gpio_cb_t cb)
{
  gpio_cb = cb;
  if(button1_hwval) {
    unsigned int interrupt = GPIOINT_CallbackRegisterExt(
      button1_hwval & 0xF,
      gpioint_cb,
      cb);
    GPIO_ExtIntConfig((button1_hwval & 0x70) >> 4,
                      button1_hwval & 0xF,
                      interrupt,
                      true,
                      false,
                      true);
    GPIO_IntClear(1<<(button1_hwval & 0xF));
    GPIO_IntEnable(1<<(button1_hwval & 0xF));
    DPRINTF("Registered interrupt on pin %d for Button 1\n", (button1_hwval & 0xF));
  }

  if(button2_hwval) {
    unsigned int interrupt = GPIOINT_CallbackRegisterExt(
      button2_hwval & 0xF,
      gpioint_cb,
      cb);
    GPIO_ExtIntConfig((button2_hwval & 0x70) >> 4,
                      button2_hwval & 0xF,
                      interrupt,
                      true,
                      false,
                      true);
    GPIO_IntClear(1<<(button2_hwval & 0xF));
    GPIO_IntEnable(1<<(button2_hwval & 0xF));
    DPRINTF("Registered interrupt on pin %d for Button 2\n", (button2_hwval & 0xF));
  }

  if(gpio_hwval) {
    unsigned int interrupt = GPIOINT_CallbackRegisterExt(
      gpio_hwval & 0xF,
      gpioint_cb,
      cb);
    GPIO_ExtIntConfig((gpio_hwval & 0x70) >> 4,
                      gpio_hwval & 0xF,
                      interrupt,
                      true,
                      false,
                      true);
    GPIO_IntClear(1<<(gpio_hwval & 0xF));
    GPIO_IntEnable(1<<(gpio_hwval & 0xF));
    DPRINTF("Registered interrupt on pin %d for GPIO\n", (gpio_hwval & 0xF));
  }

  if(nfcfd_hwval) {
    unsigned int interrupt = GPIOINT_CallbackRegisterExt(
      nfcfd_hwval & 0xF,
      gpioint_cb,
      cb);
    GPIO_ExtIntConfig((nfcfd_hwval & 0x70) >> 4,
                      nfcfd_hwval & 0xF,
                      interrupt,
                      true,
                      false,
                      true);
    GPIO_IntClear(1<<(nfcfd_hwval & 0xF));
    GPIO_IntEnable(1<<(nfcfd_hwval & 0xF));
    DPRINTF("Registered interrupt on pin %d for NFC field detect\n", (nfcfd_hwval & 0xF));
  }
}

bool oepl_hw_get_temperature(int8_t* temperature_degc)
{
  *temperature_degc = (int8_t)EMU_TemperatureGet();
  //EMU->CTRL &= ~EMU_CTRL_EM2DBGEN;
  return true;
}

bool oepl_hw_get_voltage(uint16_t* voltage_mv, bool force_measurement)
{
  static uint32_t last_measurement_ticks = 0;
  static uint16_t voltage_reading_cache = 0;

  if(force_measurement
     || sl_sleeptimer_tick_to_ms(sl_sleeptimer_get_tick_count() - last_measurement_ticks) > 60*60*1000
     || voltage_reading_cache == 0) {
    // Declare init structs
    IADC_Init_t init = IADC_INIT_DEFAULT;
    IADC_AllConfigs_t initAllConfigs = IADC_ALLCONFIGS_DEFAULT;
    IADC_InitSingle_t initSingle = IADC_INITSINGLE_DEFAULT;
    IADC_SingleInput_t initSingleInput = IADC_SINGLEINPUT_DEFAULT;

    // Enable IADC0 clock
    CMU_ClockEnable(cmuClock_IADC0, true);
    
    // Reset IADC to reset configuration in case it has been modified by
    // other code
    IADC_reset(IADC0);

    // Select clock for IADC
    CMU_ClockSelectSet(cmuClock_IADCCLK, cmuSelect_FSRCO);  // FSRCO - 20MHz

    // Modify init structs and initialize
    init.warmup = iadcWarmupNormal;

    // Set the HFSCLK prescale value here
    init.srcClkPrescale = IADC_calcSrcClkPrescale(IADC0, 20000000, 0);

    // Configuration 0 is used by both scan and single conversions by default
    // Use internal bandgap (supply voltage in mV) as reference
    initAllConfigs.configs[0].reference = iadcCfgReferenceInt1V2;
    initAllConfigs.configs[0].vRef = 1210;
    initAllConfigs.configs[0].osrHighSpeed = iadcCfgOsrHighSpeed2x;
    initAllConfigs.configs[0].analogGain = iadcCfgAnalogGain1x;

    // Divides CLK_SRC_ADC to set the CLK_ADC frequency
    initAllConfigs.configs[0].adcClkPrescale = IADC_calcAdcClkPrescale(IADC0,
                                              10000000,
                                              0,
                                              iadcCfgModeNormal,
                                              init.srcClkPrescale);

    // Assign pins to positive and negative inputs in differential mode
    initSingleInput.posInput   = iadcPosInputAvdd;
    initSingleInput.negInput   = iadcNegInputGnd;

    // Initialize the IADC
    IADC_init(IADC0, &init, &initAllConfigs);

    // Initialize the Single conversion inputs
    IADC_initSingle(IADC0, &initSingle, &initSingleInput);

    // Start IADC conversion
    IADC_command(IADC0, iadcCmdStartSingle);

    // Wait for conversion to be complete
    while((IADC0->STATUS & (_IADC_STATUS_CONVERTING_MASK
                | _IADC_STATUS_SINGLEFIFODV_MASK)) != IADC_STATUS_SINGLEFIFODV);

    // Get ADC result
    uint32_t sample = IADC_pullSingleFifoResult(IADC0).data;
    voltage_reading_cache = ((sample * 4 * 1200) / 4095);

    IADC_command(IADC0, iadcCmdStopSingle);
    IADC_reset(IADC0);
    CMU_ClockEnable(cmuClock_IADC0, false);
    
    last_measurement_ticks = sl_sleeptimer_get_tick_count();
    DPRINTF("Supply voltage %d mv\n", voltage_reading_cache);

    uint16_t lowbat_voltage;
    if(oepl_nvm_setting_get(OEPL_LOWBAT_VOLTAGE_MV, &lowbat_voltage, sizeof(lowbat_voltage)) == NVM_SUCCESS) {
      if(voltage_reading_cache <= lowbat_voltage) {
        oepl_display_set_overlay(ICON_LOW_BATTERY, true);
      } else {
        oepl_display_set_overlay(ICON_LOW_BATTERY, false);
      }
    }
  }
  *voltage_mv = voltage_reading_cache;
  return true;
}

uint8_t oepl_hw_get_hwid(void)
{
  return hwid;
}

uint8_t oepl_hw_get_capabilities(void)
{
  return oepl_efr32xg22_get_oepl_hwcapa(); 
}

uint16_t oepl_hw_get_swversion(void)
{
  extern char linker_vectors_begin;
  void** vtable = (void*)(&linker_vectors_begin);

  ApplicationProperties_t* appinfo_p = (ApplicationProperties_t*)vtable[13];
  if(appinfo_p != NULL) {
    return appinfo_p->app.version;
  } else {
    return SL_APPLICATION_VERSION;
  }
}

const char* oepl_hw_get_swsuffix(void)
{
#if defined(GIT_COMMIT_ID)
  return GIT_COMMIT_ID;
#else
  return "Dev";
#endif
}

bool oepl_hw_nfc_write_url(const uint8_t* url_buffer, size_t length)
{
  const oepl_efr32xg22_tagconfig_t* tagconfig = oepl_efr32xg22_get_config();

  if (tagconfig->nfc == NULL || tagconfig->hwtype != SOLUM_AUTODETECT) {
    return false;
  }

  GPIO_PinModeSet(tagconfig->nfc->SCL.port, tagconfig->nfc->SCL.pin, gpioModeWiredAndFilter, 0);
  GPIO_PinModeSet(tagconfig->nfc->SDA.port, tagconfig->nfc->SDA.pin, gpioModeWiredAndFilter, 0);
  if (tagconfig->nfc->power.port != gpioPortInvalid) {
    GPIO_PinModeSet(tagconfig->nfc->power.port, tagconfig->nfc->power.pin, gpioModePushPull, 1);
    sl_udelay_wait(40000);
  } else {
    sl_udelay_wait(10000);
  }

  bool res = od_nfc_write_record_raw(tagconfig, OD_NFC_REC_URI, url_buffer, length);
  
  if (tagconfig->nfc->power.port != gpioPortInvalid) {
    GPIO_PinOutClear(tagconfig->nfc->power.port, tagconfig->nfc->power.pin);
    GPIO_PinModeSet(tagconfig->nfc->power.port, tagconfig->nfc->power.pin, gpioModeInput, 1);
  }
  GPIO_PinModeSet(tagconfig->nfc->SCL.port, tagconfig->nfc->SCL.pin, gpioModeInput, 1);
  GPIO_PinModeSet(tagconfig->nfc->SDA.port, tagconfig->nfc->SDA.pin, gpioModeInput, 1);
}

bool oepl_hw_nfc_write_raw(const uint8_t* raw_buffer, size_t length)
{
  const oepl_efr32xg22_tagconfig_t* tagconfig = oepl_efr32xg22_get_config();

  if (tagconfig->nfc == NULL || tagconfig->hwtype != SOLUM_AUTODETECT) {
    return false;
  }

  GPIO_PinModeSet(tagconfig->nfc->SCL.port, tagconfig->nfc->SCL.pin, gpioModeWiredAndFilter, 0);
  GPIO_PinModeSet(tagconfig->nfc->SDA.port, tagconfig->nfc->SDA.pin, gpioModeWiredAndFilter, 0);
  if (tagconfig->nfc->power.port != gpioPortInvalid) {
    GPIO_PinModeSet(tagconfig->nfc->power.port, tagconfig->nfc->power.pin, gpioModePushPull, 1);
    sl_udelay_wait(40000);
  } else {
    sl_udelay_wait(10000);
  }

  bool res = od_nfc_write_record_raw(tagconfig, OD_NFC_REC_RAW_NDEF, &raw_buffer[2], length - 2);

  if (tagconfig->nfc->power.port != gpioPortInvalid) {
    GPIO_PinOutClear(tagconfig->nfc->power.port, tagconfig->nfc->power.pin);
    GPIO_PinModeSet(tagconfig->nfc->power.port, tagconfig->nfc->power.pin, gpioModeInput, 1);
  }
  GPIO_PinModeSet(tagconfig->nfc->SCL.port, tagconfig->nfc->SCL.pin, gpioModeInput, 1);
  GPIO_PinModeSet(tagconfig->nfc->SDA.port, tagconfig->nfc->SDA.pin, gpioModeInput, 1);
}

static void deepsleep_timer_cb(sl_sleeptimer_timer_handle_t *handle, void *data)
{
  (void) handle;
  (void) data;
  if(RAIL_GetRadioState(sl_rail_util_get_handle(SL_RAIL_UTIL_HANDLE_INST0)) & RAIL_RF_STATE_IDLE) {
    // Waking up from EM4 will take us through reset, so set all interface pins back to idle state here.
    const oepl_efr32xg22_tagconfig_t* tagconfig = oepl_efr32xg22_get_config();
    if(tagconfig->debug && tagconfig->debug->type == DBG_EUART) {
      GPIO_PinModeSet(tagconfig->debug->output.euart.tx.port, tagconfig->debug->output.euart.tx.pin, gpioModeDisabled, 1);
      if(tagconfig->debug->output.euart.rx.port != gpioPortInvalid) {
        GPIO_PinModeSet(tagconfig->debug->output.euart.rx.port, tagconfig->debug->output.euart.rx.pin, gpioModeDisabled, 1);
      }
      if(tagconfig->debug->output.euart.enable.port != gpioPortInvalid) {
        GPIO_PinModeSet(tagconfig->debug->output.euart.enable.port, tagconfig->debug->output.euart.enable.pin, gpioModeDisabled, 1);
      }
    }

    // Turn off LEDs
    if(tagconfig->led) {
      if(tagconfig->led->white.port != gpioPortInvalid) {
        GPIO_PinModeSet(tagconfig->led->white.port, tagconfig->led->white.pin, gpioModeDisabled, 1);
      }
      if(tagconfig->led->red.port != gpioPortInvalid) {
        GPIO_PinModeSet(tagconfig->led->red.port, tagconfig->led->red.pin, gpioModeDisabled, 1);
      }
      if(tagconfig->led->blue.port != gpioPortInvalid) {
        GPIO_PinModeSet(tagconfig->led->blue.port, tagconfig->led->blue.pin, gpioModeDisabled, 1);
      }
      if(tagconfig->led->green.port != gpioPortInvalid) {
        GPIO_PinModeSet(tagconfig->led->green.port, tagconfig->led->green.pin, gpioModeDisabled, 1);
      }
    }

    // Enter EM4
    EMU_EM4Init_TypeDef em4init;
    em4init.em4State = emuEM4Shutoff;
    em4init.retainLfxo = false;
    em4init.retainLfrco = false;
    em4init.retainUlfrco = false;
    em4init.pinRetentionMode = emuPinRetentionEm4Exit;

    uint32_t em4mask = 0;
    if(tagconfig->gpio) {
      em4mask |= tagconfig->gpio->button1_em4wuval;
      em4mask |= tagconfig->gpio->button2_em4wuval;
      em4mask |= tagconfig->gpio->nfc_fd_em4wuval;
    }

    DPRINTF("Wakeup mask from EM4: 0x%08lx\n", em4mask);

    EMU_EM4Init(&em4init);
    GPIO_EM4EnablePinWakeup(em4mask, 0);
    EMU_EnterEM4();
  } else {
    // Postpone 5 more ms
    sl_sleeptimer_start_timer_ms(&deepsleep_entry_timer_handle,
                               5, deepsleep_timer_cb,
                               NULL, 0, SL_SLEEPTIMER_NO_HIGH_PRECISION_HF_CLOCKS_REQUIRED_FLAG);
  }
}

void oepl_hw_enter_deepsleep(void)
{
  if(is_devkit) {
    sl_mx25_flash_shutdown();
  }
  // Todo: cut power to the EPD if possible
  // Todo: cut power to the NFC if possible

  sl_sleeptimer_start_timer_ms(&deepsleep_entry_timer_handle,
                               5, deepsleep_timer_cb,
                               NULL, 0, SL_SLEEPTIMER_NO_HIGH_PRECISION_HF_CLOCKS_REQUIRED_FLAG);
}

bool oepl_hw_get_screen_properties(size_t* x, size_t* y, size_t* bpp)
{
  oepl_efr32xg22_displayparams_t displayparams;
  bool retval = oepl_efr32xg22_get_displayparams(&displayparams);
  if(retval) {
    *x = displayparams.xres;
    *y = displayparams.yres;
    *bpp = 1 + (displayparams.have_thirdcolor ? 1 : 0);
  }
  return retval;
}

bool oepl_hw_get_screen_controller(uint8_t* controller_type)
{
  oepl_efr32xg22_displayparams_t displayparams;
  bool retval = oepl_efr32xg22_get_displayparams(&displayparams);
  if(retval) {
    *controller_type = displayparams.ctrl;
  }
  return retval;
}

// ----------------------- External flash --------------------------------------
void oepl_hw_flash_deepsleep(void)
{
  if(is_devkit) {
    sl_mx25_flash_shutdown();
  }
}

void oepl_hw_flash_wake(void)
{
  if(is_devkit) {
    CMU_ClockEnable(cmuClock_GPIO, true);
    GPIO_PinModeSet(SL_MX25_FLASH_SHUTDOWN_CS_PORT, SL_MX25_FLASH_SHUTDOWN_CS_PIN, gpioModePushPull, 1);
    // Wake up flash in case the device is in deep power down mode already.
    GPIO_PinOutClear(SL_MX25_FLASH_SHUTDOWN_CS_PORT, SL_MX25_FLASH_SHUTDOWN_CS_PIN);
    sl_udelay_wait(20);                  // wait for tCRDP=20us
    GPIO_PinOutSet(SL_MX25_FLASH_SHUTDOWN_CS_PORT, SL_MX25_FLASH_SHUTDOWN_CS_PIN);
    sl_udelay_wait(35);                  // wait for tRDP=35us
    GPIO_PinModeSet(SL_MX25_FLASH_SHUTDOWN_CS_PORT, SL_MX25_FLASH_SHUTDOWN_CS_PIN, gpioModeDisabled, 1);
  }
}

// ----------------------- Crash / Debug ---------------------------------------
void oepl_hw_reboot(void)
{
  NVIC_SystemReset();
}

void oepl_hw_crash(oepl_hw_debug_module_t module, bool reboot, const char* fmt, ...)
{
  switch(module) {
    case DBG_APP:
      printf("\n[APP-CRASH] ");
      break;
    case DBG_HW:
      printf("\n[HW-CRASH] ");
      break;
    case DBG_RADIO:
      printf("\n[RADIO-CRASH] ");
      break;
    case DBG_GPIO:
      printf("\n[GPIO-CRASH] ");
      break;
    case DBG_LED:
      printf("\n[LED-CRASH] ");
      break;
    case DBG_DISPLAY:
      printf("\n[DISP-CRASH] ");
      break;
    case DBG_NVM:
      printf("\n[NVM-CRASH] ");
      break;
    case DBG_FLASH:
      printf("\n[FLASH-CRASH] ");
      break;
    case DBG_OTHER:
      printf("\n[OTHER-CRASH] ");
      break;
  }
  
  va_list argp;
  va_start(argp, fmt);
  vprintf(fmt, argp);
  va_end(argp);

  if(reboot) {
    printf("-----------------------------------------\n");
    printf("Rebooting\n");
    printf("-----------------------------------------\n");
    oepl_hw_reboot();
  } else {
    printf("-----------------------------------------\n");
    printf("Sleeping forever\n");
    printf("-----------------------------------------\n");
    // In case of crashes early in the program, delay sleep entry such that
    // we can still connect a debugger.
    for(size_t i = 0; i < 10; i++) {
      sl_udelay_wait(100000);
    }
    oepl_hw_enter_deepsleep();
  }
}

#if GLOBAL_DEBUG_ENABLE
void oepl_hw_debugprint(oepl_hw_debug_module_t module, const char* fmt, ...)
{
  static oepl_hw_debug_module_t last_seen = DBG_OTHER;
  if(module != last_seen) {
    switch(module) {
      case DBG_APP:
        printf("\n[APP]");
        break;
      case DBG_HW:
        printf("\n[HW]");
        break;
      case DBG_RADIO:
        printf("\n[RADIO]");
        break;
      case DBG_GPIO:
        printf("\n[GPIO]");
        break;
      case DBG_LED:
        printf("\n[LED]");
        break;
      case DBG_DISPLAY:
        printf("\n[DISP]");
        break;
      case DBG_NVM:
        printf("\n[NVM]");
        break;
      case DBG_FLASH:
        printf("\n[FLASH]");
        break;
      case DBG_OTHER:
        printf("\n[OTHER]");
        break;
    }
  }
  last_seen = module;
  
  va_list argp;
  va_start(argp, fmt);
  vprintf(fmt, argp);
  va_end(argp);
}
#endif

/* Redirect all hardware errors to our custom handler */

void NMI_Handler(void)
{
  hardware_error_handler();
}

void HardFault_Handler(void)
{
  hardware_error_handler();
}

void BusFault_Handler(void)
{
  hardware_error_handler();
}

// -----------------------------------------------------------------------------
//                          Static Function Definitions
// -----------------------------------------------------------------------------
static void em_cb(sl_power_manager_em_t from,
                  sl_power_manager_em_t to)
{
  (void) from;
  (void) to;
  /*
  if(to == SL_POWER_MANAGER_EM2)  {
    DPRINTF("++S++");
  } else {
    DPRINTF("--D--");
  }
  */

  // Check whether we need a timer to detect NFC events on devices with dubious pinouts
  if((nfcfd_hwval & 0x70) == 0x20 ||
     (nfcfd_hwval & 0x70) == 0x30) {
    switch(to) {
      case SL_POWER_MANAGER_EM2:
      case SL_POWER_MANAGER_EM3:
        sl_sleeptimer_start_periodic_timer_ms(
          &nfc_poll_timer_handle,
          500,
          nfc_poll_timer_cb, NULL, 0xFF, SL_SLEEPTIMER_NO_HIGH_PRECISION_HF_CLOCKS_REQUIRED_FLAG 
        );
        break;
      default:
        sl_sleeptimer_stop_timer(&nfc_poll_timer_handle);
        break;
    }
  }
}

static void hardware_error_handler(void)
{
  // Use watchdog to trigger a reboot in a second
  WDOG_Init_TypeDef wdogInit = WDOG_INIT_DEFAULT;
  wdogInit.perSel = wdogPeriod_1k;

  WDOGn_Init(WDOG0, &wdogInit);

  // Debug print (watchdog will make us reset if this should fail/hang)
  DPRINTF("** Caught an exception! **\n");
  DPRINTF("  IRQ num %d\n", SCB->ICSR & SCB_ICSR_VECTACTIVE_Msk);
  DPRINTF("  SHCSR %08x\n", SCB->SHCSR);
  DPRINTF("  CFSR %08x\n", SCB->CFSR);
  DPRINTF("  HFSR %08x\n", SCB->HFSR);
  DPRINTF("  DFSR %08x\n", SCB->DFSR);
  DPRINTF("  MMFAR %08x\n", SCB->MMFAR);
  DPRINTF("  BFAR %08x\n", SCB->BFAR);
  DPRINTF("  AFSR %08x\n", SCB->AFSR);

  // Wait for watchdog
  while(1);
}

// Probe TNB132M (distilled from https://github.com/OpenDisplay/Firmware_Silabs)
static bool od_nfc_type3_paged_block_read16(const oepl_efr32xg22_tagconfig_t* tagconfig,
                                            uint8_t dev7, uint8_t sub, uint8_t *out16)
{
  // Transfer structure
  I2C_TransferSeq_TypeDef i2cTransfer;
  I2C_TransferReturn_TypeDef result;

  // Initialize I2C transfer
  i2cTransfer.addr          = dev7 << 1;
  i2cTransfer.flags         = I2C_FLAG_WRITE_READ;
  i2cTransfer.buf[0].data   = &sub;
  i2cTransfer.buf[0].len    = 1;
  i2cTransfer.buf[1].data   = out16;
  i2cTransfer.buf[1].len    = 16;

  result = I2C_TransferInit(tagconfig->nfc->i2c, &i2cTransfer);

  // Send data
  while (result == i2cTransferInProgress) {
    result = I2C_Transfer(tagconfig->nfc->i2c);
  }

  if (result != i2cTransferDone) {
      DPRINTF("I2C fail %08x\n", result);
      return false;
  } else {
    DPRINTF("I2C Response: ");
    for(size_t i = 0; i < 16; i++) {
      DPRINTF("%02x ", out16[i]);
    }
    DPRINTF("\n");
    return true;
  }
}

/* Type-3 16-byte block write: START, dev+W, sub, 16 data bytes, STOP. Mirrors the
 * paged read on the write side — AI goes to dev=0x48 sub=0, NDEF data blocks go to
 * dev=0x40 sub=0x10/0x20/... (same byte-offset mapping as the read path). */
static bool od_nfc_type3_paged_block_write16(const oepl_efr32xg22_tagconfig_t* tagconfig,
                                             uint8_t dev7, uint8_t sub, const uint8_t *in16)
{
  bool a;
  if (in16 == NULL) {
    return false;
  }

  // Transfer structure
  I2C_TransferSeq_TypeDef i2cTransfer;
  I2C_TransferReturn_TypeDef result;

  // Initialize I2C transfer
  i2cTransfer.addr          = dev7 << 1;
  i2cTransfer.flags         = I2C_FLAG_WRITE_WRITE;
  i2cTransfer.buf[0].data   = &sub;
  i2cTransfer.buf[0].len    = 1;
  i2cTransfer.buf[1].data   = in16;
  i2cTransfer.buf[1].len    = 16;

  result = I2C_TransferInit(tagconfig->nfc->i2c, &i2cTransfer);

  // Send data
  while (result == i2cTransferInProgress) {
    result = I2C_Transfer(tagconfig->nfc->i2c);
  }

  if (result != i2cTransferDone) {
      return false;
  } else {
    return true;
  }
}

/* Wake TNB132M host I2C + open the byte-offset Type-3 data window at 0x40.
 * After EEPROM/block writes via 0x48/0x40, callers must run this again or
 * 0x48 sub=0 still returns updated AI while 0x40 sub=0x10 reads all 0xFF (RF /
 * tag RAM cache repopulates; MCU window must be re-opened). */
static void od_nfc_tnb132m_prime_type3(const oepl_efr32xg22_tagconfig_t* tagconfig)
{
  {
    // Transfer structure
    I2C_TransferSeq_TypeDef i2cTransfer;
    I2C_TransferReturn_TypeDef result;
    uint8_t txBuffer[2];

    txBuffer[0] = 0x21;
    txBuffer[1] = 0x04;

    // Initialize I2C transfer
    i2cTransfer.addr          = 0x30 << 1;
    i2cTransfer.flags         = I2C_FLAG_WRITE;
    i2cTransfer.buf[0].data   = txBuffer;
    i2cTransfer.buf[0].len    = 2;
    i2cTransfer.buf[1].data   = NULL;
    i2cTransfer.buf[1].len    = 0;

    result = I2C_TransferInit(tagconfig->nfc->i2c, &i2cTransfer);

    // Send data
    while (result == i2cTransferInProgress) {
      result = I2C_Transfer(tagconfig->nfc->i2c);
    }

    if (result != i2cTransferDone) {
      DPRINTF("I2C fail %08x\n", result);
    }
  }

  {
    // Transfer structure
    I2C_TransferSeq_TypeDef i2cTransfer;
    I2C_TransferReturn_TypeDef result;
    uint8_t txBuffer[1 + 1];

    txBuffer[0] = 0x25;
    txBuffer[1] = 0x00;

    // Initialize I2C transfer
    i2cTransfer.addr          = 0x30 << 1;
    i2cTransfer.flags         = I2C_FLAG_WRITE_READ;
    i2cTransfer.buf[0].data   = txBuffer;
    i2cTransfer.buf[0].len    = 1;
    i2cTransfer.buf[1].data   = &txBuffer[1];
    i2cTransfer.buf[1].len    = 1;

    result = I2C_TransferInit(tagconfig->nfc->i2c, &i2cTransfer);

    // Send data
    while (result == i2cTransferInProgress) {
      result = I2C_Transfer(tagconfig->nfc->i2c);
    }

    if (result != i2cTransferDone) {
        DPRINTF("I2C fail %08x\n", result);
    } else {
      DPRINTF("I2C Response %02x\n", txBuffer[1]);
    }
  }

  sl_udelay_wait(20000);

  {
    // Transfer structure
    I2C_TransferSeq_TypeDef i2cTransfer;
    I2C_TransferReturn_TypeDef result;
    uint8_t txBuffer[1 + 16];

    txBuffer[0] = 0x30;

    // Initialize I2C transfer
    i2cTransfer.addr          = 0x43 << 1;
    i2cTransfer.flags         = I2C_FLAG_WRITE_READ;
    i2cTransfer.buf[0].data   = txBuffer;
    i2cTransfer.buf[0].len    = 1;
    i2cTransfer.buf[1].data   = txBuffer;
    i2cTransfer.buf[1].len    = 16;

    result = I2C_TransferInit(tagconfig->nfc->i2c, &i2cTransfer);

    // Send data
    while (result == i2cTransferInProgress) {
      result = I2C_Transfer(tagconfig->nfc->i2c);
    }

    if (result != i2cTransferDone) {
        DPRINTF("I2C fail %08x\n", result);
    } else {
      DPRINTF("I2C Response: ");
      for(size_t i = 0; i < sizeof(txBuffer) - 1; i++) {
        DPRINTF("%02x ", txBuffer[1+i]);
      }
      DPRINTF("\n");
    }
  }
}


static void tnb132m_probe(const oepl_efr32xg22_tagconfig_t* tagconfig)
{
  uint8_t ai[16];
  uint8_t data[64];
  uint32_t ln;
  uint16_t sum_calc, sum_read;
  unsigned need_blocks;

  od_nfc_tnb132m_prime_type3(tagconfig);
  sl_udelay_wait(2000);

  if (!od_nfc_type3_paged_block_read16(tagconfig, 0x48u, 0x00u, ai)) {
    printf("[OD] NFC ndef AI @0x48 sub=0: NACK\r\n");
    return;
  }

  if (ai[0] != 0x10u) {
    DPRINTF("NFC ndef AI ver=%02x (not 1.x) — skip decode\r\n", ai[0]);
    return;
  }
  ln = ((uint32_t)ai[11] << 16) | ((uint32_t)ai[12] << 8) | (uint32_t)ai[13];
  sum_read = (uint16_t)(((uint16_t)ai[14] << 8) | (uint16_t)ai[15]);
  sum_calc = 0;
  for (unsigned i = 0; i < 14u; i++) {
    sum_calc = (uint16_t)(sum_calc + ai[i]);
  }
  DPRINTF("NFC ndef AI Ver=%u.%u Nbr=%u Nbw=%u Nmaxb=%u RWFlag=%02x Ln=%lu Sum=%04x (calc %04x %s)\r\n",
        (unsigned)(ai[0] >> 4), (unsigned)(ai[0] & 0x0Fu), (unsigned)ai[1], (unsigned)ai[2],
        (unsigned)(((uint16_t)ai[3] << 8) | ai[4]), ai[10], (unsigned long)ln, (unsigned)sum_read,
        (unsigned)sum_calc, sum_calc == sum_read ? "OK" : "BAD");
  if (ln == 0u || ln > sizeof(data)) {
    DPRINTF("NFC ndef Ln=%lu out of range (max %u)\r\n",
          (unsigned long)ln, (unsigned)sizeof(data));
    return;
  }
  need_blocks = (unsigned)((ln + 15u) / 16u);
  for (unsigned b = 0; b < need_blocks; b++) {
    uint8_t byte_off = (uint8_t)(0x10u + b * 0x10u);
    uint8_t throwaway[16];
    (void)od_nfc_type3_paged_block_read16(tagconfig, 0x48u, 0x00u,
                                          throwaway);
    sl_udelay_wait(500);
    if (!od_nfc_type3_paged_block_read16(tagconfig, 0x40u,
                                        byte_off, &data[b * 16u])) {
      DPRINTF("NFC ndef blk%u @0x40 sub=%02x: NACK\r\n", b + 1u, (unsigned)byte_off);
      return;
    }
    DPRINTF("NFC ndef blk%u %04x:", b + 1u, b * 16u);
    for (unsigned j = 0; j < 16u; j++) {
      DPRINTF(" %02x", data[b * 16u + j]);
    }
    DPRINTF("\r\n");
    sl_udelay_wait(200);
  }
  {
    uint8_t tnf0 = data[0] & 0x07u;
    if (ln < 4u || tnf0 == 0u || tnf0 >= 6u) {
      DPRINTF("NFC ndef record too short or bad TNF (ln=%lu hdr=%02x tnf=%u)\r\n",
            (unsigned long)ln, data[0], tnf0);
      return;
    }
  }
  {
    uint8_t hdr = data[0];
    uint8_t tnf = hdr & 0x07u;
    bool sr = (hdr & 0x10u) != 0;
    bool il = (hdr & 0x08u) != 0;
    unsigned tlen = data[1];
    unsigned plen;
    unsigned off;
    unsigned ilen = 0;
    if (sr) {
      plen = data[2];
      off = 3u;
    } else {
      if (ln < 6u) {
        DPRINTF("NFC ndef long-format truncated\r\n");
        return;
      }
      plen = ((unsigned)data[2] << 24) | ((unsigned)data[3] << 16) | ((unsigned)data[4] << 8) | data[5];
      off = 6u;
    }
    if (il) {
      if (ln < off + 1u) {
        DPRINTF("NFC ndef IL truncated\r\n");
        return;
      }
      ilen = data[off];
      off += 1u;
    }
    if (off + tlen + ilen + plen > ln) {
      DPRINTF("NFC ndef fields exceed Ln (off=%u tlen=%u il=%u plen=%u ln=%lu)\r\n",
            off, tlen, ilen, plen, (unsigned long)ln);
      return;
    }
    DPRINTF("NFC ndef rec TNF=%u SR=%u IL=%u tlen=%u plen=%u type=\"%.*s\"\r\n",
          tnf, (unsigned)sr, (unsigned)il, tlen, plen, tlen, (const char *)&data[off]);
    off += tlen + ilen;
    if (tnf == 0x01u && tlen == 1u && data[off - tlen - ilen] == 'T' && plen >= 1u) {
      unsigned stat = data[off];
      unsigned lang_len = stat & 0x3Fu;
      if (1u + lang_len <= plen) {
        DPRINTF("NFC ndef Text lang=\"%.*s\" utf%u text=\"%.*s\"\r\n",
              lang_len, (const char *)&data[off + 1u], (stat & 0x80u) ? 16u : 8u,
              (unsigned)(plen - 1u - lang_len), (const char *)&data[off + 1u + lang_len]);
      }
    }
  }
}

static void tnb132m_write_ndef_text(const oepl_efr32xg22_tagconfig_t* tagconfig, const char* text)
{
  uint8_t ai[16] = { 0 };
  uint8_t blocks[32] = { 0 };
  uint8_t cur_ai[16];
  size_t tlen = (text != NULL) ? strlen(text) : 0u;
  unsigned plen;
  unsigned recln;
  uint16_t sum;
  unsigned i;
  unsigned need_blocks;

  od_nfc_tnb132m_prime_type3(tagconfig);
  sl_udelay_wait(2000);

  if (tlen == 0u || tlen > 25u) {
    DPRINTF("NFC write: text len %u out of range (1..25)\r\n", (unsigned)tlen);
    return false;
  }
  plen = 1u + 2u + (unsigned)tlen;
  recln = 4u + plen;

  blocks[0] = 0xD1u;
  blocks[1] = 0x01u;
  blocks[2] = (uint8_t)plen;
  blocks[3] = 0x54u;
  blocks[4] = 0x02u;
  blocks[5] = (uint8_t)'e';
  blocks[6] = (uint8_t)'n';
  for (i = 0; i < tlen; i++) {
    blocks[7u + i] = (uint8_t)text[i];
  }

  ai[0] = 0x10u;
  ai[1] = 0x02u;
  ai[2] = 0x01u;
  ai[3] = 0x00u;
  ai[4] = 0x3Cu;
  ai[11] = (uint8_t)((recln >> 16) & 0xFFu);
  ai[12] = (uint8_t)((recln >> 8) & 0xFFu);
  ai[13] = (uint8_t)(recln & 0xFFu);
  if (od_nfc_type3_paged_block_read16(tagconfig, 0x48u, 0x00u, cur_ai)
      && cur_ai[0] == 0x10u) {
    ai[10] = cur_ai[10];
  }
  sum = 0;
  for (i = 0; i < 14u; i++) {
    sum = (uint16_t)(sum + ai[i]);
  }
  ai[14] = (uint8_t)(sum >> 8);
  ai[15] = (uint8_t)(sum & 0xFFu);

  need_blocks = (recln + 15u) / 16u;
  DPRINTF("NFC write: Ln=%u RWFlag=%02x text=\"%s\" (%u blk)\r\n",
         recln, ai[10], text, need_blocks);
  if (!od_nfc_type3_paged_block_write16(tagconfig, 0x48u, 0x00u, ai)) {
    DPRINTF("NFC write: AI @0x48 sub=0 NACK\r\n");
    return false;
  }
  sl_udelay_wait(10000);
  for (i = 0; i < need_blocks; i++) {
    uint8_t byte_off = (uint8_t)(0x10u + i * 0x10u);
    if (!od_nfc_type3_paged_block_write16(tagconfig, 0x40u, byte_off,
                                          &blocks[i * 16u])) {
      DPRINTF("NFC write: blk%u @0x40 sub=%02x NACK\r\n", i + 1u, (unsigned)byte_off);
      return false;
    }
    sl_udelay_wait(10000);
  }
  DPRINTF("NFC write: OK\r\n");
  sl_udelay_wait(50000);
  return true;
}

static bool od_nfc_write_record_raw(const oepl_efr32xg22_tagconfig_t* tagconfig, uint8_t rec_type, const uint8_t *data, uint16_t data_len)
{
  uint8_t ai[16] = { 0 };
  uint8_t cur_ai[16];
  static uint8_t s_od_nfc_write_blocks[512];
  uint8_t *blocks = s_od_nfc_write_blocks;
  uint16_t sum;
  uint16_t record_len;
  uint16_t payload_len;
  uint8_t need_blocks;
  uint8_t i;
  CORE_DECLARE_IRQ_STATE;

  if (data == NULL || data_len == 0u) {
    return false;
  }

  DPRINTF("NFC write type %d, content:\n", rec_type);
  for(size_t i = 0; i < data_len; i++) {
    DPRINTF("%02x", data[i]);
  }
  DPRINTF("\n");

  /* Defense-in-depth: bound data_len so the uint16_t payload_len math below
   * (1 + hdr + data_len) cannot wrap and overrun s_od_nfc_write_blocks. Callers
   * validate too, but this keeps the record builder memory-safe on its own. */
  if (data_len > sizeof(s_od_nfc_write_blocks)) {
    return false;
  }
  memset(blocks, 0, sizeof(s_od_nfc_write_blocks));

  if (rec_type == OD_NFC_REC_TEXT) {
    payload_len = (uint16_t)(1u + 2u + data_len);
    if (payload_len > 255u || payload_len > (uint16_t)(sizeof(s_od_nfc_write_blocks) - 4u)) {
      return false;
    }
    record_len = (uint16_t)(4u + payload_len);
    blocks[0] = 0xD1u;
    blocks[1] = 0x01u;
    blocks[2] = (uint8_t)payload_len;
    blocks[3] = 0x54u;
    blocks[4] = 0x02u;
    blocks[5] = (uint8_t)'e';
    blocks[6] = (uint8_t)'n';
    memcpy(&blocks[7], data, data_len);
  } else if (rec_type == OD_NFC_REC_URI) {
    payload_len = (uint16_t)(1u + data_len);
    if (payload_len > 255u || payload_len > (uint16_t)(sizeof(s_od_nfc_write_blocks) - 4u)) {
      return false;
    }
    record_len = (uint16_t)(4u + payload_len);
    blocks[0] = 0xD1u;
    blocks[1] = 0x01u;
    blocks[2] = (uint8_t)payload_len;
    blocks[3] = 0x55u;
    blocks[4] = 0x00u;
    memcpy(&blocks[5], data, data_len);
  } else if (rec_type == OD_NFC_REC_WELL_KNOWN_RAW) {
    uint8_t type_len;
    uint16_t raw_payload_len;
    if (data_len < 2u) {
      return false;
    }
    type_len = data[0];
    if (type_len == 0u || (uint16_t)(1u + type_len) > data_len) {
      return false;
    }
    raw_payload_len = (uint16_t)(data_len - 1u - type_len);
    if (raw_payload_len > 255u) {
      return false;
    }
    record_len = (uint16_t)(3u + type_len + raw_payload_len);
    if (record_len > sizeof(s_od_nfc_write_blocks)) {
      return false;
    }
    blocks[0] = 0xD1u;
    blocks[1] = type_len;
    blocks[2] = (uint8_t)raw_payload_len;
    memcpy(&blocks[3], &data[1], type_len);
    if (raw_payload_len > 0u) {
      memcpy(&blocks[3u + type_len], &data[1u + type_len], raw_payload_len);
    }
  } else if (rec_type == OD_NFC_REC_MIME) {
    uint8_t mime_tl;
    uint16_t body_len;

    if (data_len < 3u) {
      return false;
    }
    mime_tl = data[0];
    if (mime_tl == 0u || (uint16_t)(1u + mime_tl) > data_len) {
      return false;
    }
    body_len = (uint16_t)(data_len - 1u - mime_tl);
    if (body_len > 255u) {
      return false;
    }
    record_len = (uint16_t)(3u + mime_tl + body_len);
    if (record_len > sizeof(s_od_nfc_write_blocks)) {
      return false;
    }
    blocks[0] = 0xD2u; /* MB | ME | SR ; TNF = MIME */
    blocks[1] = mime_tl;
    blocks[2] = (uint8_t)body_len;
    memcpy(&blocks[3], &data[1], mime_tl);
    if (body_len > 0u) {
      memcpy(&blocks[3u + mime_tl], &data[1u + mime_tl], body_len);
    }
  } else if (rec_type == OD_NFC_REC_RAW_NDEF) {
    record_len = data_len;
    if (record_len == 0u || record_len > sizeof(s_od_nfc_write_blocks)) {
      return false;
    }
    memcpy(blocks, data, record_len);
  } else {
    return false;
  }

  CORE_ENTER_CRITICAL();

  od_nfc_tnb132m_prime_type3(tagconfig);
  sl_udelay_wait(2000);

  ai[0] = 0x10u;
  ai[1] = 0x02u;
  ai[2] = 0x01u;
  ai[3] = 0x00u;
  ai[4] = 0x3Cu;
  ai[11] = (uint8_t)((record_len >> 16) & 0xFFu);
  ai[12] = (uint8_t)((record_len >> 8) & 0xFFu);
  ai[13] = (uint8_t)(record_len & 0xFFu);

  if (od_nfc_type3_paged_block_read16(tagconfig, 0x48u, 0x00u, cur_ai)
      && (cur_ai[0] & 0xF0u) == 0x10u) {
    ai[10] = cur_ai[10];
  }
  sum = 0u;
  for (i = 0u; i < 14u; i++) {
    sum = (uint16_t)(sum + ai[i]);
  }
  ai[14] = (uint8_t)(sum >> 8);
  ai[15] = (uint8_t)(sum & 0xFFu);

  if (!od_nfc_type3_paged_block_write16(tagconfig, 0x48u, 0x00u, ai)) {
    CORE_EXIT_CRITICAL();
    return false;
  }
  sl_udelay_wait(10000);
  need_blocks = (uint8_t)((record_len + 15u) / 16u);
  for (i = 0u; i < need_blocks; i++) {
    uint8_t byte_off = (uint8_t)(0x10u + i * 0x10u);
    if (!od_nfc_type3_paged_block_write16(tagconfig, 0x40u, byte_off, &blocks[i * 16u])) {
      CORE_EXIT_CRITICAL();
      return false;
    }
    sl_udelay_wait(10000);
  }

  CORE_EXIT_CRITICAL();
  return true;
}
