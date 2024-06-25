// -----------------------------------------------------------------------------
//                                   Includes
// -----------------------------------------------------------------------------
#include "oepl_display_driver_uc8159.h"
#include "oepl_display_driver_common.h"
#include "oepl_drawing_capi.h"
#include "sl_udelay.h"

// For debugprint
#include "oepl_hw_abstraction.h"

#include <string.h>
#include <stdlib.h>

// -----------------------------------------------------------------------------
//                              Configuration values
// -----------------------------------------------------------------------------
#ifndef UC8159_DEBUG_PRINT
#define UC8159_DEBUG_PRINT 1
#endif

// -----------------------------------------------------------------------------
//                              Macros and Typedefs
// -----------------------------------------------------------------------------
#if UC8159_DEBUG_PRINT
#define DPRINTF(fmt_, ...) oepl_hw_debugprint(DBG_DISPLAY, (fmt_), ##__VA_ARGS__)
#else
#define DPRINTF(...)
#endif

#define CMD_PANEL_SETTING 0x00
#define CMD_POWER_SETTING 0x01
#define CMD_POWER_OFF 0x02
#define CMD_POWER_OFF_SEQUENCE 0x03
#define CMD_POWER_ON 0x04
#define CMD_BOOSTER_SOFT_START 0x06
#define CMD_DEEP_SLEEP 0x07
#define CMD_DISPLAY_START_TRANSMISSION_DTM1 0x10
#define CMD_DATA_STOP 0x11
#define CMD_DISPLAY_REFRESH 0x12
#define CMD_DISPLAY_IMAGE_PROCESS 0x13
#define CMD_VCOM_LUT_C 0x20
#define CMD_LUT_B 0x21
#define CMD_LUT_W 0x22
#define CMD_LUT_G1 0x23
#define CMD_LUT_G2 0x24
#define CMD_LUT_R0 0x25
#define CMD_LUT_R1 0x26
#define CMD_LUT_R2 0x27
#define CMD_LUT_R3 0x28
#define CMD_LUT_XON 0x29
#define CMD_PLL_CONTROL 0x30
#define CMD_TEMPERATURE_DOREADING 0x40
#define CMD_TEMPERATURE_SELECT 0x41
#define CMD_TEMPERATURE_WRITE 0x42
#define CMD_TEMPERATURE_READ 0x43
#define CMD_VCOM_INTERVAL 0x50
#define CMD_LOWER_POWER_DETECT 0x51
#define CMD_TCON_SETTING 0x60
#define CMD_RESOLUTION_SETING 0x61
#define CMD_SPI_FLASH_CONTROL 0x65
#define CMD_REVISION 0x70
#define CMD_STATUS 0x71
#define CMD_AUTO_MEASUREMENT_VCOM 0x80
#define CMD_READ_VCOM 0x81
#define CMD_VCOM_DC_SETTING 0x82
#define CMD_PARTIAL_WINDOW 0x90
#define CMD_PARTIAL_IN 0x91
#define CMD_PARTIAL_OUT 0x92
#define CMD_PROGRAM_MODE 0xA0
#define CMD_ACTIVE_PROGRAM 0xA1
#define CMD_READ_OTP 0xA2
#define CMD_EPD_EEPROM_SLEEP 0xB9
#define CMD_EPD_EEPROM_WAKE 0xAB
#define CMD_CASCADE_SET 0xE0
#define CMD_POWER_SAVING 0xE3
#define CMD_FORCE_TEMPERATURE 0xE5
#define CMD_LOAD_FLASH_LUT 0xE5

// -----------------------------------------------------------------------------
//                          Static Function Declarations
// -----------------------------------------------------------------------------
static void display_init(const oepl_display_parameters_t* display_params);
static void display_draw(void);

static void display_reset(void);
static void display_reinit(void);
static void display_sleep(void);
static void display_refresh_and_wait(void);

// -----------------------------------------------------------------------------
//                                Global Variables
// -----------------------------------------------------------------------------
const oepl_display_driver_desc_t oepl_display_driver_uc8159 =
{
  .init = &display_init,
  .draw = &display_draw
};

// -----------------------------------------------------------------------------
//                                Static Variables
// -----------------------------------------------------------------------------
static oepl_display_parameters_t* params = NULL;

// -----------------------------------------------------------------------------
//                          Public Function Definitions
// -----------------------------------------------------------------------------
static void display_init(const oepl_display_parameters_t* display_params)
{
  DPRINTF("Initialising UC8159 driver\n");
  oepl_display_driver_common_init();
  
  if(params == NULL) {
    params = malloc(sizeof(oepl_display_parameters_t));
    if(params == NULL) {
      oepl_hw_crash(DBG_DISPLAY, true, "Can't allocate memory to save display params\n");
    }
  }

  // Make local copy since we'll be using most of these
  memcpy(params, display_params, sizeof(*display_params));

  // Todo: Add pin definition for UC8159's onboard flash CS line
  oepl_hw_crash(DBG_DISPLAY, false, "UC8159 is not yet supported\n");
}

static void interleave_buffer(uint8_t *dst, uint8_t b, uint8_t r) {
  b ^= 0xFF;
  uint8_t b_out = 0;
  for (int8_t shift = 3; shift >= 0; shift--) {
    b_out = 0;
    if (((b >> 2 * shift) & 0x01) && ((r >> 2 * shift) & 0x01)) {
        b_out |= 0x04;  // 0x30
    } else if ((b >> 2 * shift) & 0x01) {
        b_out |= 0x03;                     // 0x30
    } else if ((r >> 2 * shift) & 0x01) {  // 4 or 5
        b_out |= 0x04;                     // 0x30
    } else {
    }

    if (((b >> 2 * shift) & 0x02) && ((r >> 2 * shift) & 0x02)) {
        b_out |= 0x40;  // 0x30
    } else if ((b >> 2 * shift) & 0x02) {
        b_out |= 0x30;  // 0x30
    } else if ((r >> 2 * shift) & 0x02) {
        b_out |= 0x40;  // 0x30
    } else {
    }
    *dst++ = b_out;
  }
}

static void display_draw(void)
{
  DPRINTF("enter UC8179 draw\n");
  display_reinit();

  size_t blocksize = 16;
  size_t rowsize = params->x_res_effective / 8;

  uint8_t* bw_buf = malloc(rowsize * blocksize);
  uint8_t* r_buf = malloc(rowsize * blocksize);
  uint8_t* interleaved_buf = malloc(rowsize * 4);

  if(bw_buf == NULL || r_buf == NULL || interleaved_buf == NULL) {
    oepl_hw_crash(DBG_DISPLAY, true, "Could not allocate display buffers\n");
  }

  for(size_t cur_y = 0; cur_y < params->y_res_effective; cur_y += blocksize) {
    memset(bw_buf, 0, rowsize * blocksize);
    memset(r_buf, 0, rowsize * blocksize);

    for(size_t block_i = 0; block_i < blocksize; block_i++) {
      C_renderDrawLine(bw_buf + (block_i * rowsize), block_i + cur_y, 0);
    }

    for(size_t block_i = 0; block_i < blocksize; block_i++) {
      C_renderDrawLine(r_buf + (block_i * rowsize), block_i + cur_y, 1);
    }

    for(size_t block_i = 0; block_i < blocksize; block_i++) {
      for(size_t cur_x = 0; cur_x < rowsize; cur_x++) {
        interleave_buffer(interleaved_buf + (cur_x * 4), bw_buf[cur_x + (rowsize * block_i)], r_buf[cur_x + (rowsize * block_i)]);
      }
      oepl_display_driver_common_data(interleaved_buf, rowsize * 4, false);
    }
  }

  free(bw_buf);
  free(r_buf);
  free(interleaved_buf);

  display_refresh_and_wait();
  display_sleep();
}

// -----------------------------------------------------------------------------
//                          Static Function Definitions
// -----------------------------------------------------------------------------
static void display_reset(void)
{
  oepl_display_driver_wait(20);
  DPRINTF("Activating driver\n");
  oepl_display_driver_common_activate();
  DPRINTF("Pulsing reset\n");
  oepl_display_driver_common_pulse_reset(12, 20, 20);
}

static void display_sleep(void)
{
  EMIT_INSTRUCTION_STATIC_DATA(CMD_VCOM_INTERVAL, {0x17});
  oepl_display_driver_wait(10);
  EMIT_INSTRUCTION_STATIC_DATA(CMD_VCOM_DC_SETTING, {0x00});
  oepl_display_driver_wait(10);
  EMIT_INSTRUCTION_NO_DATA(CMD_POWER_OFF);
  oepl_display_driver_wait(10);
  EMIT_INSTRUCTION_STATIC_DATA(CMD_DEEP_SLEEP, {0xA5});
  oepl_display_driver_wait(10);

  oepl_display_driver_common_deactivate();
}

static void display_refresh_and_wait(void)
{
  DPRINTF("Sending refresh\n");
  EMIT_INSTRUCTION_NO_DATA(CMD_DISPLAY_REFRESH);
  sl_udelay_wait(2000);
  oepl_display_driver_wait_busy(0, true);
}

static void display_reinit(void)
{
  // Reset the display
  display_reset();

  oepl_hw_crash(DBG_DISPLAY, false, "Todo: add support for UC8179 BS pin\n");
  //digitalWrite(EPD_BS, LOW);

  EMIT_INSTRUCTION_STATIC_DATA(CMD_PANEL_SETTING, {0xEF, 0x08});  // default = 0xE7-0x08 // 0xEF-0x08  = right-side up
  EMIT_INSTRUCTION_STATIC_DATA(CMD_POWER_SETTING, {0x37, 0x00, 0x05, 0x05});
  EMIT_INSTRUCTION_STATIC_DATA(CMD_POWER_OFF_SEQUENCE, {0x00});
  EMIT_INSTRUCTION_STATIC_DATA(CMD_BOOSTER_SOFT_START, {0xC7, 0xCC, 0x1D});
  oepl_display_driver_wait_busy(250, true);
  EMIT_INSTRUCTION_NO_DATA(CMD_POWER_ON);
  oepl_display_driver_wait_busy(250, true);

  EMIT_INSTRUCTION_STATIC_DATA(CMD_DISPLAY_IMAGE_PROCESS, {0x00});
  EMIT_INSTRUCTION_STATIC_DATA(CMD_PLL_CONTROL, {0x3C});
  EMIT_INSTRUCTION_STATIC_DATA(CMD_TEMPERATURE_SELECT, {0x00});
  EMIT_INSTRUCTION_STATIC_DATA(CMD_VCOM_INTERVAL, {0x77});
  EMIT_INSTRUCTION_STATIC_DATA(CMD_TCON_SETTING, {0x22});
  EMIT_INSTRUCTION_STATIC_DATA(CMD_RESOLUTION_SETING, {0x02, 0x58, 0x01, 0xC0});  // set for 600x448
  EMIT_INSTRUCTION_STATIC_DATA(CMD_SPI_FLASH_CONTROL, {0x00});

  oepl_hw_crash(DBG_DISPLAY, false, "Todo: add support for UC8179 flash read\n");
  //uint8_t bracket = getTempBracket();
  //loadFrameRatePLL(bracket);
  //oepl_display_driver_wait_busy(250, true);
  //loadTempVCOMDC(bracket);
  //oepl_display_driver_wait_busy(250, true);
}
