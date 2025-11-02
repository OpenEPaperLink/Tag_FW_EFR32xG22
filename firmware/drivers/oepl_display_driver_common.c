// -----------------------------------------------------------------------------
//                                   Includes
// -----------------------------------------------------------------------------
#include "oepl_display_driver_common.h"
#include "oepl_hw_abstraction.h"
#include "oepl_efr32_hwtypes.h"
#include <spidrv.h>
#include "string.h"
#include "sl_udelay.h"
#include "sl_sleeptimer.h"
#include "gpiointerrupt.h"
#include "sl_power_manager.h"
#include "oepl_drawing_capi.h"

// -----------------------------------------------------------------------------
//                              Configuration values
// -----------------------------------------------------------------------------
#ifndef DISPLAY_COMMON_DEBUG_PRINT
#define DISPLAY_COMMON_DEBUG_PRINT 1
#endif

// -----------------------------------------------------------------------------
//                              Macros and Typedefs
// -----------------------------------------------------------------------------
#if DISPLAY_COMMON_DEBUG_PRINT
#define DPRINTF(fmt_, ...) oepl_hw_debugprint(DBG_DISPLAY, (fmt_), ##__VA_ARGS__)
#else
#define DPRINTF(...)
#endif

typedef struct {
  uint8_t* buf;
  size_t bufsize;
  size_t xstart;
  size_t xbytes;
  size_t ystart;
  size_t ylines;
  size_t cur_y;
  int color;
  bool mirrorX;
  bool mirrorY;
  uint8_t cs_mask;
} scan_parameters_t;

// -----------------------------------------------------------------------------
//                          Static Function Declarations
// -----------------------------------------------------------------------------
// Callback function for the GPIO interrupt
static void busyint_cb(uint8_t pin, void* ctx);

/// Callback function for the busywait timer
static void busywait_timer_cb(sl_sleeptimer_timer_handle_t *handle, void *data);
/// Internal callback for the busywait GPIO
static void busywait_internal_cb(oepl_display_driver_common_event_t event);

// Callback function for SPI driver
static void spicb(struct SPIDRV_HandleData *handle, Ecode_t transferStatus, int itemsTransferred);

// -----------------------------------------------------------------------------
//                                Global Variables
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
//                                Static Variables
// -----------------------------------------------------------------------------
static const oepl_efr32xg22_tagconfig_t* cfg = NULL;
static SPIDRV_HandleData_t handledata;
static SPIDRV_Handle_t const handle = &handledata;

static volatile oepl_display_driver_common_callback_t cb_after_busy = NULL;
static volatile oepl_display_driver_common_callback_t cb_after_scan = NULL;

// Busywait timer is responsible for waking up the system periodically to check
// the busy pin.
static sl_sleeptimer_timer_handle_t busywait_timer_handle;
static volatile bool busywait_timer_expired = false;
static bool pinstate_expected = false;
static volatile bool pinchange_detected = false;
static scan_parameters_t scan_parameters = {
  .buf = NULL
};

// -----------------------------------------------------------------------------
//                          Public Function Definitions
// -----------------------------------------------------------------------------

void oepl_display_driver_common_init(void)
{
  if(cfg != NULL) {
    return;
  }

  cfg = oepl_efr32xg22_get_config();
  if(cfg == NULL || cfg->display == NULL) {
    oepl_hw_crash(DBG_DISPLAY, false, "No display configured\n");
  }

  // Set all pins to off state
  if(cfg->display->enable.port != gpioPortInvalid) {
    GPIO_PinModeSet(cfg->display->enable.port, cfg->display->enable.pin, gpioModePushPull, cfg->display->enable.idle_state);
  }

  GPIO_PinModeSet(cfg->display->MOSI.port, cfg->display->MOSI.pin, gpioModeInputPull, 0);
  if(cfg->display->MISO.port != gpioPortInvalid) {
    GPIO_PinModeSet(cfg->display->MISO.port, cfg->display->MISO.pin, gpioModeInputPull, 0);
  }
  GPIO_PinModeSet(cfg->display->SCK.port, cfg->display->SCK.pin, gpioModeInputPull, 0);
  GPIO_PinModeSet(cfg->display->nCS.port, cfg->display->nCS.pin, gpioModeInputPull, 0);
  if(cfg->display->nCS2.port != gpioPortInvalid) {
    GPIO_PinModeSet(cfg->display->nCS2.port, cfg->display->nCS2.pin, gpioModeInputPull, 0);
  }
  GPIO_PinModeSet(cfg->display->BUSY.port, cfg->display->BUSY.pin, gpioModeInputPull, 0);
  GPIO_PinModeSet(cfg->display->DC.port, cfg->display->DC.pin, gpioModeInputPull, 0);
  GPIO_PinModeSet(cfg->display->nRST.port, cfg->display->nRST.pin, gpioModeInputPull, 0);

  sl_sleeptimer_start_timer_ms(
    &busywait_timer_handle,
    100,
    busywait_timer_cb,
    NULL, 1, SL_SLEEPTIMER_NO_HIGH_PRECISION_HF_CLOCKS_REQUIRED_FLAG);
  sl_sleeptimer_stop_timer(&busywait_timer_handle);
}

static void _assert_cs(uint8_t cs_mask)
{
  if(cs_mask & CS_LEADER) {
    GPIO_PinOutClear(cfg->display->nCS.port, cfg->display->nCS.pin);
  }
  if((cs_mask & CS_FOLLOWER) && cfg->display->nCS2.port != gpioPortInvalid) {
    GPIO_PinOutClear(cfg->display->nCS2.port, cfg->display->nCS2.pin);
  }
}

static void _deassert_cs(uint8_t cs_mask)
{
  if(cs_mask & CS_LEADER) {
    GPIO_PinOutSet(cfg->display->nCS.port, cfg->display->nCS.pin);
  }
  if((cs_mask & CS_FOLLOWER) && cfg->display->nCS2.port != gpioPortInvalid) {
    GPIO_PinOutSet(cfg->display->nCS2.port, cfg->display->nCS2.pin);
  }
}

static void _assert_data(void)
{
  GPIO_PinOutSet(cfg->display->DC.port, cfg->display->DC.pin);
}

static void _assert_command(void)
{
  GPIO_PinOutClear(cfg->display->DC.port, cfg->display->DC.pin);
}

void oepl_display_driver_common_activate(void)
{
  oepl_display_driver_common_init();

  // Start with powering the display
  if(cfg->display->enable.port != gpioPortInvalid) {
    GPIO_PinModeSet(cfg->display->enable.port, cfg->display->enable.pin, gpioModePushPull, cfg->display->enable.idle_state ? 0 : 1);
  }

  // Manual CS control, set CS pin mode
  GPIO_PinModeSet(cfg->display->nCS.port, cfg->display->nCS.pin, gpioModePushPull, 1);
  if(cfg->display->nCS2.port != gpioPortInvalid) {
    GPIO_PinModeSet(cfg->display->nCS2.port, cfg->display->nCS2.pin, gpioModePushPull, 1);
  }

  // Configure DC and Reset
  GPIO_PinModeSet(cfg->display->DC.port, cfg->display->DC.pin, gpioModePushPull, 1);
  GPIO_PinModeSet(cfg->display->nRST.port, cfg->display->nRST.pin, gpioModePushPull, 1);

  // Configure busy as input and register interrupt
  GPIO_PinModeSet(cfg->display->BUSY.port, cfg->display->BUSY.pin, gpioModeInput, 1);
  unsigned int interrupt = GPIOINT_CallbackRegisterExt(
      cfg->display->BUSY.pin,
      busyint_cb,
      NULL);
  GPIO_ExtIntConfig(cfg->display->BUSY.port,
                    cfg->display->BUSY.pin,
                    interrupt,
                    true,
                    true,
                    false);
  // Setup the IRQ, but don't enable it yet. We'll do this just in time when we expect
  // the busy signal to go low.
  DPRINTF("Registered interrupt on port %d pin %d for BUSY\n", cfg->display->BUSY.port, cfg->display->BUSY.pin);

  // Set up the SPI driver
  SPIDRV_Init_t spi_init = SPIDRV_MASTER_DEFAULT;
  spi_init.port = cfg->display->usart;
  spi_init.portTx = cfg->display->MOSI.port;
  spi_init.pinTx = cfg->display->MOSI.pin;
  if(cfg->display->MISO.port != gpioPortInvalid) {
    spi_init.portRx = cfg->display->MISO.port;
    spi_init.pinRx = cfg->display->MISO.pin;
  } else {
    // Downside of the SPI driver is that it requires a MISO pin to be defined
    // If the display doesn't have explicit MISO, declare MOSI instead and don't
    // forget to change the pin mode to PushPull after SPI init.
    spi_init.portRx = cfg->display->MOSI.port;
    spi_init.pinRx = cfg->display->MOSI.pin;
  }
  spi_init.portClk = cfg->display->SCK.port;
  spi_init.pinClk = cfg->display->SCK.pin;
  spi_init.bitRate = 5000000;
  spi_init.csControl = spidrvCsControlApplication;
  
  SPIDRV_Init(handle, &spi_init);
  GPIO_PinModeSet(cfg->display->MOSI.port, cfg->display->MOSI.pin, gpioModePushPull, 0);
}

void oepl_display_driver_common_pulse_reset(uint32_t ms_before_assert, uint32_t ms_to_assert, uint32_t ms_after_assert)
{
  if(ms_before_assert) {
    DPRINTF("reset delay\n");
    oepl_display_driver_wait(ms_before_assert);
  }

  DPRINTF("pulsing rst\n");
  GPIO_PinOutToggle(cfg->display->nRST.port, cfg->display->nRST.pin);
  oepl_display_driver_wait(ms_to_assert);
  GPIO_PinOutToggle(cfg->display->nRST.port, cfg->display->nRST.pin);
  
  if(ms_after_assert) {
    DPRINTF("waiting after rst pulse\n");
    oepl_display_driver_wait(ms_after_assert);
  }
  DPRINTF("reset done\n");
}

void oepl_display_driver_common_deactivate(void)
{
  // Set all output pins to input with pulldown to avoid backpowering the display
  GPIO_PinModeSet(cfg->display->MOSI.port, cfg->display->MOSI.pin, gpioModeInputPull, 0);
  if(cfg->display->MISO.port != gpioPortInvalid) {
    GPIO_PinModeSet(cfg->display->MISO.port, cfg->display->MISO.pin, gpioModeInputPull, 0);
  }
  GPIO_PinModeSet(cfg->display->SCK.port, cfg->display->SCK.pin, gpioModeInputPull, 0);
  GPIO_PinModeSet(cfg->display->nCS.port, cfg->display->nCS.pin, gpioModeInputPull, 0);
  if(cfg->display->nCS2.port != gpioPortInvalid) {
    GPIO_PinModeSet(cfg->display->nCS2.port, cfg->display->nCS2.pin, gpioModeInputPull, 0);
  }
  GPIO_PinModeSet(cfg->display->BUSY.port, cfg->display->BUSY.pin, gpioModeInputPull, 0);
  GPIO_PinModeSet(cfg->display->DC.port, cfg->display->DC.pin, gpioModeInputPull, 0);
  GPIO_PinModeSet(cfg->display->nRST.port, cfg->display->nRST.pin, gpioModeInputPull, 0);

  // Drive enable low
  GPIO_PinModeSet(cfg->display->enable.port, cfg->display->enable.pin, gpioModeInputPull, cfg->display->enable.idle_state);

  SPIDRV_DeInit(handle);
}

void oepl_display_driver_common_instruction(uint8_t opcode, bool keep_cs_low)
{
  oepl_display_driver_common_instruction_with_data(opcode, NULL, 0, keep_cs_low);
}

void oepl_display_driver_common_instruction_multi(uint8_t opcode, bool keep_cs_low, uint8_t cs_mask)
{
  oepl_display_driver_common_instruction_with_data_multi(opcode, NULL, 0, keep_cs_low, cs_mask);
}

void oepl_display_driver_common_instruction_with_data(uint8_t opcode, const uint8_t* data_buffer, size_t data_len, bool keep_cs_low)
{
  oepl_display_driver_common_instruction_with_data_multi(opcode, data_buffer, data_len, keep_cs_low, CS_LEADER);
}

void oepl_display_driver_common_instruction_with_data_multi(uint8_t opcode, const uint8_t* data_buffer, size_t data_len, bool keep_cs_low, uint8_t cs_mask)
{
  _assert_command();
  _assert_cs(cs_mask);
  
  SPIDRV_MTransmitB(handle, &opcode, 1);

  _assert_data();

  if(data_len > 0) {
    if(cs_mask & CS_LEADER) {
      GPIO_PinOutSet(cfg->display->nCS.port, cfg->display->nCS.pin);
    }
    if((cs_mask & CS_FOLLOWER) && cfg->display->nCS2.port != gpioPortInvalid) {
      GPIO_PinOutSet(cfg->display->nCS2.port, cfg->display->nCS2.pin);
    }

    sl_udelay_wait(1);
    if(cs_mask & CS_LEADER) {
      GPIO_PinOutClear(cfg->display->nCS.port, cfg->display->nCS.pin);
    }
    if((cs_mask & CS_FOLLOWER) && cfg->display->nCS2.port != gpioPortInvalid) {
      GPIO_PinOutClear(cfg->display->nCS2.port, cfg->display->nCS2.pin);
    }
    
    SPIDRV_MTransmitB(handle, data_buffer, data_len);
  }

  if(!keep_cs_low) {
    _deassert_cs(cs_mask);
  }
}

void oepl_display_driver_common_data(const uint8_t* data_buffer, size_t data_len, bool keep_cs_low)
{
  oepl_display_driver_common_data_multi(data_buffer, data_len, keep_cs_low, CS_LEADER);
}

void oepl_display_driver_common_data_multi(const uint8_t* data_buffer, size_t data_len, bool keep_cs_low, uint8_t cs_mask)
{
  _assert_data();
  _assert_cs(cs_mask);

  SPIDRV_MTransmitB(handle, data_buffer, data_len);

  if(!keep_cs_low) {
    _deassert_cs(cs_mask);
  }
}

void oepl_display_driver_common_dataread(uint8_t* data_buffer, size_t data_len, bool keep_cs_low)
{
  cfg->display->usart->CTRL_SET = USART_CTRL_LOOPBK_ENABLE;
  cfg->display->usart->CMD = USART_CMD_TXTRIEN;

  _assert_data();
  _assert_cs(CS_LEADER);

  SPIDRV_MTransferB(handle, data_buffer, data_buffer, data_len);

  cfg->display->usart->CTRL_CLR = USART_CTRL_LOOPBK_ENABLE;
  cfg->display->usart->CMD = USART_CMD_TXTRIDIS;

  if(!keep_cs_low) {
    _deassert_cs(CS_LEADER);
  }
}

void oepl_display_scan_frame(uint8_t* xbuf, size_t bufsize, size_t xstart, size_t xbytes, size_t ystart, size_t ylines, int color, bool mirrorX, bool mirrorY)
{
  oepl_display_scan_frame_multi(xbuf, bufsize, xstart, xbytes, ystart, ylines, color, mirrorX, mirrorY, CS_LEADER);
}

void oepl_display_scan_frame_multi(uint8_t* xbuf, size_t bufsize, size_t xstart, size_t xbytes, size_t ystart, size_t ylines, int color, bool mirrorX, bool mirrorY, uint8_t cs_mask)
{
  if(!xbuf) {
    oepl_hw_crash(DBG_DISPLAY, true, "No line buffer given!\n");
  }

  uint8_t* swapbuf = malloc(bufsize);

  if(swapbuf == NULL) {
    oepl_hw_crash(DBG_DISPLAY, true, "Malloc ran out");
  }

  uint8_t* outbuf = &xbuf[xstart];

  for(size_t line = ystart; line < ystart + ylines; line++) {
    memset(xbuf, 0, bufsize);
    if(mirrorY) {
      C_renderDrawLine(xbuf, ystart + ylines - 1 - (line - ystart), color);
    } else {
      C_renderDrawLine(xbuf, line, color);
    }
    if(mirrorX) {
      for(size_t i = 0; i < xbytes; i++) {
        swapbuf[xbytes - 1 - i] = SL_RBIT8(xbuf[xstart + i]);
        outbuf = swapbuf;
      }
    } else {
      outbuf = &xbuf[xstart];
    }
    oepl_display_driver_common_data_multi(outbuf, xbytes, false, cs_mask);
  }

  free(swapbuf);

}

void oepl_display_scan_frame_async(uint8_t* xbuf, size_t bufsize, size_t xstart, size_t xbytes, size_t ystart, size_t ylines, int color, bool mirrorX, bool mirrorY, oepl_display_driver_common_callback_t cb_done)
{
  oepl_display_scan_frame_async_multi(xbuf, bufsize, xstart, xbytes, ystart, ylines, color, mirrorX, mirrorY, CS_LEADER, cb_done);
}

void oepl_display_scan_frame_async_multi(uint8_t* xbuf, size_t bufsize, size_t xstart, size_t xbytes, size_t ystart, size_t ylines, int color, bool mirrorX, bool mirrorY, uint8_t cs_mask, oepl_display_driver_common_callback_t cb_done)
{
  cb_after_scan = cb_done;

  if(scan_parameters.buf) {
    oepl_hw_crash(DBG_DISPLAY, true, "Can't scan a new frame when another scan is still running\n");
  }

  scan_parameters.buf = xbuf;
  scan_parameters.bufsize = bufsize;
  scan_parameters.xstart = xstart;
  scan_parameters.xbytes = xbytes;
  scan_parameters.ystart = ystart;
  scan_parameters.ylines = ylines;
  scan_parameters.cur_y = ystart;
  scan_parameters.color = color;
  scan_parameters.mirrorX = mirrorX;
  scan_parameters.mirrorY = mirrorY;
  scan_parameters.cs_mask = cs_mask;

  // Kick off the operation
  sl_power_manager_add_em_requirement(SL_POWER_MANAGER_EM1);
  spicb(NULL, 0, 0);
}

void oepl_display_driver_wait(size_t timeout_ms)
{
  busywait_timer_expired = false;
  cb_after_busy = busywait_internal_cb;
  sl_status_t status = sl_sleeptimer_restart_timer_ms(
                        &busywait_timer_handle,
                        timeout_ms,
                        busywait_timer_cb,
                        NULL, 1, SL_SLEEPTIMER_NO_HIGH_PRECISION_HF_CLOCKS_REQUIRED_FLAG);
  if(status == SL_STATUS_OK) {
    while(!busywait_timer_expired) {
      sl_power_manager_sleep();
    }
  } else {
    DPRINTF("Couldn't start timer for %ld ms, resorting to busywait\n", timeout_ms);
    while(timeout_ms > 100) {
      sl_udelay_wait(100*1000);
      timeout_ms -= 100;
    }
    sl_udelay_wait(timeout_ms * 1000);
  }
  cb_after_busy = NULL;
}

void oepl_display_driver_wait_busy(size_t timeout_ms, bool expected_pin_state)
{
  uint32_t start_ticks = sl_sleeptimer_get_tick_count();
  cb_after_busy = busywait_internal_cb;
  switch(cfg->display->BUSY.port) {
    case gpioPortA:
      break;
    case gpioPortB:
      break;
    default:
      // Other ports can't generate interrupts from low power modes
      sl_power_manager_add_em_requirement(SL_POWER_MANAGER_EM1);
      break;
  }

  busywait_timer_expired = false;
  pinstate_expected = expected_pin_state;
  pinchange_detected = false;

  GPIO_IntClear(1<<(cfg->display->BUSY.pin));
  GPIO_IntEnable(1<<(cfg->display->BUSY.pin));

  if(timeout_ms) {
    sl_status_t status = sl_sleeptimer_restart_timer_ms(
                          &busywait_timer_handle,
                          timeout_ms,
                          busywait_timer_cb,
                          NULL, 0, SL_SLEEPTIMER_NO_HIGH_PRECISION_HF_CLOCKS_REQUIRED_FLAG);
    if(status != SL_STATUS_OK) {
      DPRINTF("Couldn't start sleeptimer!!!\n");
    }
  }
  
  while(GPIO_PinInGet(cfg->display->BUSY.port, cfg->display->BUSY.pin) != expected_pin_state ? 1 : 0) {
    sl_power_manager_sleep();
    if(pinchange_detected) {
      DPRINTF("BUSY deasserted\n");
      sl_sleeptimer_stop_timer(&busywait_timer_handle);
      pinchange_detected = false;
    }
    if(busywait_timer_expired) {
      DPRINTF("Display took longer than expected (>%dms) to clear busy\n", timeout_ms);
      // Avoid printing endless, but keep waiting for signal
      busywait_timer_expired = false;

      // Avoid a potential lockup situation where we may end up not detecting the busy pin gone high
      sl_sleeptimer_restart_timer_ms(
        &busywait_timer_handle,
        500,
        busywait_timer_cb,
        NULL, 0, SL_SLEEPTIMER_NO_HIGH_PRECISION_HF_CLOCKS_REQUIRED_FLAG);
    }
  }

  // Turn off the GPIO interrupt
  GPIO_IntDisable(1<<(cfg->display->BUSY.pin));
  GPIO_IntClear(1<<(cfg->display->BUSY.pin));
  cb_after_busy = NULL;
  switch(cfg->display->BUSY.port) {
    case gpioPortA:
      break;
    case gpioPortB:
      break;
    default:
      // Other ports can't generate interrupts from low power modes
      sl_power_manager_remove_em_requirement(SL_POWER_MANAGER_EM1);
      break;
  }

  uint32_t ms = sl_sleeptimer_tick_to_ms(sl_sleeptimer_get_tick_count() - start_ticks);
  DPRINTF("Display action executed in %d.%03d s\n", ms/1000, ms%1000);
}

void oepl_display_driver_wait_busy_async(oepl_display_driver_common_callback_t cb_idle, size_t timeout_ms, bool expected_pin_state)
{
  pinstate_expected = expected_pin_state;
  sl_sleeptimer_start_timer_ms(&busywait_timer_handle,
                               timeout_ms,
                               busywait_timer_cb,
                               NULL, 0, SL_SLEEPTIMER_NO_HIGH_PRECISION_HF_CLOCKS_REQUIRED_FLAG);
  cb_after_busy = cb_idle;
  GPIO_IntClear(1<<(cfg->display->BUSY.pin));
  GPIO_IntEnable(1<<(cfg->display->BUSY.pin));
}

// -----------------------------------------------------------------------------
//                          Static Function Definitions
// -----------------------------------------------------------------------------
static void busyint_cb(uint8_t pin, void* ctx)
{
  (void)ctx;
  if(pin == cfg->display->BUSY.pin) {
    if(GPIO_PinInGet(cfg->display->BUSY.port, cfg->display->BUSY.pin) == pinstate_expected ? 1 : 0) {
      GPIO_IntDisable(1<<(cfg->display->BUSY.pin));
      GPIO_IntClear(1<<(cfg->display->BUSY.pin));
      if(cb_after_busy != NULL) {
        cb_after_busy(BUSY_DEASSERTED);
        cb_after_busy = NULL;
      }
    }
  }
}

static void busywait_internal_cb(oepl_display_driver_common_event_t event) {
  if(event == BUSY_TIMEOUT) {
    busywait_timer_expired = true;
  }
  if(event == BUSY_DEASSERTED) {
    pinchange_detected = true;
  }
}

static void busywait_timer_cb(sl_sleeptimer_timer_handle_t *handle, void *data)
{
  (void)handle;
  (void)data;
  if(cb_after_busy != NULL) {
    cb_after_busy(BUSY_TIMEOUT);
  }
}

static void spicb(struct SPIDRV_HandleData *handle, Ecode_t transferStatus, int itemsTransferred)
{
  (void)transferStatus;
  (void)itemsTransferred;
  if(scan_parameters.cur_y >= scan_parameters.ystart + scan_parameters.ylines) {
    // We're done
    _deassert_cs(scan_parameters.cs_mask);
    scan_parameters.buf = NULL;
    cb_after_scan(SCAN_COMPLETE);
    sl_power_manager_remove_em_requirement(SL_POWER_MANAGER_EM1);
  }

  _assert_data();
  _assert_cs(scan_parameters.cs_mask);

  // Todo: check whether this is actually safe to do in IRQ
  memset(scan_parameters.buf, 0, scan_parameters.bufsize);
  if(scan_parameters.mirrorY) {
    C_renderDrawLine(scan_parameters.buf, scan_parameters.ystart + scan_parameters.ylines - (scan_parameters.cur_y - scan_parameters.ystart), scan_parameters.color);
  } else {
    C_renderDrawLine(scan_parameters.buf, scan_parameters.cur_y, scan_parameters.color);
  }
  if(scan_parameters.mirrorX) {
    for(size_t i = 0; i < scan_parameters.xbytes; i++) {
      scan_parameters.buf[scan_parameters.xstart + i] = SL_RBIT8(scan_parameters.buf[scan_parameters.xstart + i]);
    }
  }
  scan_parameters.cur_y += 1;
  SPIDRV_MTransmit(handle, &scan_parameters.buf[scan_parameters.xstart], scan_parameters.xbytes, spicb);
}
