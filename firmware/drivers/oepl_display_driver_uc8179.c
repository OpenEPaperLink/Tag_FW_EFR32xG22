// -----------------------------------------------------------------------------
//                                   Includes
// -----------------------------------------------------------------------------
#include "oepl_display_driver_uc8179.h"
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
#ifndef UC8179_DEBUG_PRINT
#define UC8179_DEBUG_PRINT 1
#endif

// -----------------------------------------------------------------------------
//                              Macros and Typedefs
// -----------------------------------------------------------------------------
#if UC8179_DEBUG_PRINT
#define DPRINTF(fmt_, ...) oepl_hw_debugprint(DBG_DISPLAY, (fmt_), ##__VA_ARGS__)
#else
#define DPRINTF(...)
#endif

#define CMD_PANEL_SETTING 0x00
#define CMD_POWER_SETTING 0x01
#define CMD_POWER_OFF 0x02
#define CMD_POWER_OFF_SEQUENCE 0x03
#define CMD_POWER_ON 0x04
#define CMD_POWER_ON_MEASURE 0x05
#define CMD_BOOSTER_SOFT_START 0x06
#define CMD_DEEP_SLEEP 0x07
#define CMD_DISPLAY_START_TRANSMISSION_DTM1 0x10
#define CMD_DATA_STOP 0x11
#define CMD_DISPLAY_REFRESH 0x12
#define CMD_DISPLAY_START_TRANSMISSION_DTM2 0x13
#define CMD_PLL_CONTROL 0x30
#define CMD_TEMPERATURE_CALIB 0x40
#define CMD_TEMPERATURE_SELECT 0x41
#define CMD_TEMPERATURE_WRITE 0x42
#define CMD_TEMPERATURE_READ 0x43
#define CMD_VCOM_INTERVAL 0x50
#define CMD_LOWER_POWER_DETECT 0x51
#define CMD_TCON_SETTING 0x60
#define CMD_RESOLUTION_SETING 0x61
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
#define CMD_CASCADE_SET 0xE0
#define CMD_POWER_SAVING 0xE3
#define CMD_FORCE_TEMPERATURE 0xE5

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
const oepl_display_driver_desc_t oepl_display_driver_uc8179 =
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
  DPRINTF("Initialising UC8179 driver\n");
  oepl_display_driver_common_init();
  
  if(params == NULL) {
    params = malloc(sizeof(oepl_display_parameters_t));
    if(params == NULL) {
      oepl_hw_crash(DBG_DISPLAY, true, "Can't allocate memory to save display params\n");
    }
  }

  // Make local copy since we'll be using most of these
  memcpy(params, display_params, sizeof(*display_params));
}

static void display_draw(void)
{
  DPRINTF("enter UC8179 draw\n");
  display_reinit();

  uint8_t* linebuf = malloc(params->x_res_effective / 8);

  DPRINTF("Black:\n");
  oepl_display_driver_common_instruction(CMD_DISPLAY_START_TRANSMISSION_DTM1, true);
  oepl_display_scan_frame(
    // Buffer + buffer size
    linebuf, params->x_res_effective/8,
    // Which part of the buffer to output
    0, params->x_res_effective/8,
    // Which lines to scan
    params->y_offset, params->y_offset + params->y_res_effective,
    // Which color to scan
    0,
    // Which modifications to make
    params->mirrorH, params->mirrorV
  );

  if(params->num_colors > 2) {
    DPRINTF("RED:\n");
    oepl_display_driver_common_instruction(CMD_DISPLAY_START_TRANSMISSION_DTM2, true);
    oepl_display_scan_frame(
      // Buffer + buffer size
      linebuf, params->x_res_effective/8,
      // Which part of the buffer to output
      0, params->x_res_effective/8,
      // Which lines to scan
      params->y_offset, params->y_offset + params->y_res_effective,
      // Which color to scan
      1,
      // Which modifications to make
      params->mirrorH, params->mirrorV
    );
  }

  free(linebuf);

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
  EMIT_INSTRUCTION_NO_DATA(CMD_POWER_ON);
  sl_udelay_wait(2000);
  oepl_display_driver_wait_busy(0, true);
  EMIT_INSTRUCTION_NO_DATA(CMD_DISPLAY_REFRESH);
  sl_udelay_wait(2000);
  oepl_display_driver_wait_busy(0, true);
}

static void display_reinit(void)
{
  // Reset the display
  display_reset();

  EMIT_INSTRUCTION_STATIC_DATA(CMD_PANEL_SETTING, {0x0F});
  EMIT_INSTRUCTION_STATIC_DATA(CMD_VCOM_INTERVAL, {0x30, 0x07});
  EMIT_INSTRUCTION_VAR_DATA(CMD_RESOLUTION_SETING, {params->x_res_effective >> 8, params->x_res_effective & 0xFF, params->y_res_effective >> 8, params->y_res_effective & 0xff});
}
