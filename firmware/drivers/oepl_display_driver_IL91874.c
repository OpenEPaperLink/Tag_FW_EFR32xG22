// -----------------------------------------------------------------------------
//                                   Includes
// -----------------------------------------------------------------------------
#include "oepl_display_driver_IL91874.h"
#include "oepl_drawing_capi.h"
#include "oepl_display_driver_common.h"

#include "oepl_efr32_hwtypes.h"
#include "em_gpio.h"
#include "em_usart.h"
#include "em_cmu.h"
#include "sl_udelay.h"
#include "sl_power_manager.h"
#include "oepl_hw_abstraction.h"

#include <string.h>

// -----------------------------------------------------------------------------
//                              Configuration values
// -----------------------------------------------------------------------------
#ifndef IL91874_DEBUG_PRINT
#define IL91874_DEBUG_PRINT 1
#endif

// -----------------------------------------------------------------------------
//                              Macros and Typedefs
// -----------------------------------------------------------------------------
#if IL91874_DEBUG_PRINT
#define DPRINTF(fmt_, ...) oepl_hw_debugprint(DBG_DISPLAY, (fmt_), ##__VA_ARGS__)
#else
#define DPRINTF(...)
#endif

#define EPD_WIDTH       176
#define EPD_HEIGHT      264

#define PANEL_SETTING                               0x00
#define POWER_SETTING                               0x01
#define POWER_OFF                                   0x02
#define POWER_OFF_SEQUENCE_SETTING                  0x03
#define POWER_ON                                    0x04
#define POWER_ON_MEASURE                            0x05
#define BOOSTER_SOFT_START                          0x06
#define DEEP_SLEEP                                  0x07
#define DATA_START_TRANSMISSION_1                   0x10
#define DATA_STOP                                   0x11
#define DISPLAY_REFRESH                             0x12
#define DATA_START_TRANSMISSION_2                   0x13
#define PARTIAL_DATA_START_TRANSMISSION_1           0x14 
#define PARTIAL_DATA_START_TRANSMISSION_2           0x15 
#define PARTIAL_DISPLAY_REFRESH                     0x16
#define LUT_FOR_VCOM                                0x20 
#define LUT_WHITE_TO_WHITE                          0x21
#define LUT_BLACK_TO_WHITE                          0x22
#define LUT_WHITE_TO_BLACK                          0x23
#define LUT_BLACK_TO_BLACK                          0x24
#define PLL_CONTROL                                 0x30
#define TEMPERATURE_SENSOR_COMMAND                  0x40
#define TEMPERATURE_SENSOR_CALIBRATION              0x41
#define TEMPERATURE_SENSOR_WRITE                    0x42
#define TEMPERATURE_SENSOR_READ                     0x43
#define VCOM_AND_DATA_INTERVAL_SETTING              0x50
#define LOW_POWER_DETECTION                         0x51
#define TCON_SETTING                                0x60
#define TCON_RESOLUTION                             0x61
#define SOURCE_AND_GATE_START_SETTING               0x62
#define GET_STATUS                                  0x71
#define AUTO_MEASURE_VCOM                           0x80
#define VCOM_VALUE                                  0x81
#define VCM_DC_SETTING_REGISTER                     0x82
#define PROGRAM_MODE                                0xA0
#define ACTIVE_PROGRAM                              0xA1
#define READ_OTP_DATA                               0xA2

// -----------------------------------------------------------------------------
//                          Static Function Declarations
// -----------------------------------------------------------------------------
static void display_init(const oepl_display_parameters_t* params);
static void display_draw(void);

static void display_reset(void);
static void display_reinit(void);
static void display_clear_frame(void);
static void display_sleep(void);
static void display_refresh_and_wait(void);

// -----------------------------------------------------------------------------
//                                Global Variables
// -----------------------------------------------------------------------------
const oepl_display_driver_desc_t oepl_display_driver_IL91874 =
{
  .init = &display_init,
  .draw = &display_draw
};

// -----------------------------------------------------------------------------
//                                Static Variables
// -----------------------------------------------------------------------------
const uint8_t lut_20_vcomDC[]  =
{
  0x00  , 0x00,
  0x00  , 0x1A  , 0x1A  , 0x00  , 0x00  , 0x01,
  0x00  , 0x0A  , 0x0A  , 0x00  , 0x00  , 0x08,
  0x00  , 0x0E  , 0x01  , 0x0E  , 0x01  , 0x10,
  0x00  , 0x0A  , 0x0A  , 0x00  , 0x00  , 0x08,
  0x00  , 0x04  , 0x10  , 0x00  , 0x00  , 0x05,
  0x00  , 0x03  , 0x0E  , 0x00  , 0x00  , 0x0A,
  0x00  , 0x23  , 0x00  , 0x00  , 0x00  , 0x01
};
//R21H
const uint8_t lut_21[]  = {
  0x90  , 0x1A  , 0x1A  , 0x00  , 0x00  , 0x01,
  0x40  , 0x0A  , 0x0A  , 0x00  , 0x00  , 0x08,
  0x84  , 0x0E  , 0x01  , 0x0E  , 0x01  , 0x10,
  0x80  , 0x0A  , 0x0A  , 0x00  , 0x00  , 0x08,
  0x00  , 0x04  , 0x10  , 0x00  , 0x00  , 0x05,
  0x00  , 0x03  , 0x0E  , 0x00  , 0x00  , 0x0A,
  0x00  , 0x23  , 0x00  , 0x00  , 0x00  , 0x01
};
//R22H  r
const uint8_t lut_22_red[]  = {
  0xA0  , 0x1A  , 0x1A  , 0x00  , 0x00  , 0x01,
  0x00  , 0x0A  , 0x0A  , 0x00  , 0x00  , 0x08,
  0x84  , 0x0E  , 0x01  , 0x0E  , 0x01  , 0x10,
  0x90  , 0x0A  , 0x0A  , 0x00  , 0x00  , 0x08,
  0xB0  , 0x04  , 0x10  , 0x00  , 0x00  , 0x05,
  0xB0  , 0x03  , 0x0E  , 0x00  , 0x00  , 0x0A,
  0xC0  , 0x23  , 0x00  , 0x00  , 0x00  , 0x01
};
//R23H  w
const uint8_t lut_23_white[]  = {
  0x90  , 0x1A  , 0x1A  , 0x00  , 0x00  , 0x01,
  0x40  , 0x0A  , 0x0A  , 0x00  , 0x00  , 0x08,
  0x84  , 0x0E  , 0x01  , 0x0E  , 0x01  , 0x10,
  0x80  , 0x0A  , 0x0A  , 0x00  , 0x00  , 0x08,
  0x00  , 0x04  , 0x10  , 0x00  , 0x00  , 0x05,
  0x00  , 0x03  , 0x0E  , 0x00  , 0x00  , 0x0A,
  0x00  , 0x23  , 0x00  , 0x00  , 0x00  , 0x01
};
//R24H  b
const uint8_t lut_24_black[]  = {
  0x90  , 0x1A  , 0x1A  , 0x00  , 0x00  , 0x01,
  0x20  , 0x0A  , 0x0A  , 0x00  , 0x00  , 0x08,
  0x84  , 0x0E  , 0x01  , 0x0E  , 0x01  , 0x10,
  0x10  , 0x0A  , 0x0A  , 0x00  , 0x00  , 0x08,
  0x00  , 0x04  , 0x10  , 0x00  , 0x00  , 0x05,
  0x00  , 0x03  , 0x0E  , 0x00  , 0x00  , 0x0A,
  0x00  , 0x23  , 0x00  , 0x00  , 0x00  , 0x01
};

// -----------------------------------------------------------------------------
//                          Public Function Definitions
// -----------------------------------------------------------------------------
static void display_init(const oepl_display_parameters_t* params)
{
  (void) params;
  oepl_display_driver_common_init();
}

static void display_draw(void)
{
  display_reinit();

  const uint8_t window_data[] = {0,0,0,0,EPD_WIDTH >> 8, EPD_WIDTH & 0xFF, EPD_HEIGHT >> 8, EPD_HEIGHT & 0xFF};

  uint8_t* linebuf = malloc(EPD_WIDTH/8);
  if(linebuf == NULL) {
    oepl_hw_crash(DBG_DISPLAY, true, "Couldn't allocate linebuffer\n");
  }

  oepl_display_driver_common_instruction_with_data(
    PARTIAL_DATA_START_TRANSMISSION_1, window_data, sizeof(window_data), true
  );

  oepl_display_scan_frame(
    linebuf, EPD_WIDTH/8,
    0, EPD_WIDTH/8,
    0, EPD_HEIGHT,
    0, false, false
  );

  DPRINTF("RED:\n");
  oepl_display_driver_common_instruction_with_data(
    PARTIAL_DATA_START_TRANSMISSION_2, window_data, sizeof(window_data), true
  );
  
  oepl_display_scan_frame(
    linebuf, EPD_WIDTH/8,
    0, EPD_WIDTH/8,
    0, EPD_HEIGHT,
    1, false, false
  );

  free(linebuf);
  display_refresh_and_wait();
  display_sleep();
}

// -----------------------------------------------------------------------------
//                          Static Function Definitions
// -----------------------------------------------------------------------------
static void display_reset(void)
{
  oepl_display_driver_common_activate();
  oepl_display_driver_common_pulse_reset(10, 200, 200);
}

static void display_clear_frame(void)
{
  const uint8_t window_data[] = {0,0,0,0,EPD_WIDTH >> 8, EPD_WIDTH & 0xFF, EPD_HEIGHT >> 8, EPD_HEIGHT & 0xFF};

  oepl_display_driver_common_instruction_with_data(
    PARTIAL_DATA_START_TRANSMISSION_1, window_data, sizeof(window_data), true
  );
  sl_udelay_wait(2000);
  for(size_t i = 0; i < EPD_WIDTH * EPD_HEIGHT / 8; i++) {
    oepl_display_driver_common_data(&lut_20_vcomDC[0], 1, true);
  }
  oepl_display_driver_common_transaction_done();
  sl_udelay_wait(2000);

  oepl_display_driver_common_instruction_with_data(
    PARTIAL_DATA_START_TRANSMISSION_2, window_data, sizeof(window_data), true
  );
  sl_udelay_wait(2000);
  for(size_t i = 0; i < EPD_WIDTH * EPD_HEIGHT / 8; i++) {
    oepl_display_driver_common_data(&lut_20_vcomDC[0], 1, true);
  }
  oepl_display_driver_common_transaction_done();
  sl_udelay_wait(2000);
}

static void display_sleep(void)
{
  EMIT_INSTRUCTION_NO_DATA(POWER_OFF);
  sl_udelay_wait(20);
  oepl_display_driver_wait_busy(0, true);

  EMIT_INSTRUCTION_STATIC_DATA(DEEP_SLEEP, {0xA5});
  sl_udelay_wait(20);

  // Turn off power
  oepl_display_driver_common_deactivate();
}

static void display_refresh_and_wait(void)
{
  EMIT_INSTRUCTION_NO_DATA(DISPLAY_REFRESH);
  sl_udelay_wait(2000);
  oepl_display_driver_wait_busy(0, true);
}

static void display_reinit(void)
{
  // Reset the display
  display_reset();

  EMIT_INSTRUCTION_STATIC_DATA(POWER_SETTING, {0x03, 0x00, 0x2b, 0x2b, 0x09});
  EMIT_INSTRUCTION_STATIC_DATA(BOOSTER_SOFT_START, {0x07, 0x07, 0x17});
  EMIT_INSTRUCTION_STATIC_DATA(0xF8, {0x60, 0xa5});
  EMIT_INSTRUCTION_STATIC_DATA(0xF8, {0x89, 0xa5});
  EMIT_INSTRUCTION_STATIC_DATA(0xF8, {0x90, 0x00});
  EMIT_INSTRUCTION_STATIC_DATA(0xF8, {0x93, 0x2a});
  EMIT_INSTRUCTION_STATIC_DATA(0xF8, {0x73, 0x41});
  EMIT_INSTRUCTION_STATIC_DATA(PARTIAL_DISPLAY_REFRESH, {0x00});
  EMIT_INSTRUCTION_STATIC_DATA(PANEL_SETTING, {0xaf});
  EMIT_INSTRUCTION_STATIC_DATA(PLL_CONTROL, {0x3a});
  EMIT_INSTRUCTION_STATIC_DATA(TCON_RESOLUTION, {EPD_WIDTH >> 8, EPD_WIDTH & 0xFF, EPD_HEIGHT >> 8, EPD_HEIGHT & 0xFF});
  EMIT_INSTRUCTION_STATIC_DATA(VCM_DC_SETTING_REGISTER, {0x12});
  EMIT_INSTRUCTION_STATIC_DATA(VCOM_AND_DATA_INTERVAL_SETTING, {0x87});

  // Set LUT
  oepl_display_driver_common_instruction_with_data(LUT_FOR_VCOM, lut_20_vcomDC, sizeof(lut_20_vcomDC), false);
  oepl_display_driver_common_instruction_with_data(LUT_WHITE_TO_WHITE, lut_21, sizeof(lut_21), false);
  oepl_display_driver_common_instruction_with_data(LUT_BLACK_TO_WHITE, lut_22_red, sizeof(lut_22_red), false);
  oepl_display_driver_common_instruction_with_data(LUT_WHITE_TO_BLACK, lut_23_white, sizeof(lut_23_white), false);
  oepl_display_driver_common_instruction_with_data(LUT_BLACK_TO_BLACK, lut_24_black, sizeof(lut_24_black), false);

  EMIT_INSTRUCTION_NO_DATA(POWER_ON);
  sl_udelay_wait(2000);
  oepl_display_driver_wait_busy(0, true);
}
