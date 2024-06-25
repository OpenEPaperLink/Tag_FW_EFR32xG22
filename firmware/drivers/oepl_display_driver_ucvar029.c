// -----------------------------------------------------------------------------
//                                   Includes
// -----------------------------------------------------------------------------
#include "oepl_display_driver_ucvar029.h"
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
#ifndef UCVAR029_DEBUG_PRINT
#define UCVAR029_DEBUG_PRINT 1
#endif

// -----------------------------------------------------------------------------
//                              Macros and Typedefs
// -----------------------------------------------------------------------------
#if UCVAR029_DEBUG_PRINT
#define DPRINTF(fmt_, ...) oepl_hw_debugprint(DBG_DISPLAY, (fmt_), ##__VA_ARGS__)
#else
#define DPRINTF(...)
#endif

#define EPD_CMD_POWER_OFF 0x02
#define EPD_CMD_POWER_ON 0x04
#define EPD_CMD_BOOSTER_SOFT_START 0x06
#define EPD_CMD_DEEP_SLEEP 0x07
#define EPD_CMD_DISPLAY_START_TRANSMISSION_DTM1 0x10
#define EPD_CMD_DISPLAY_REFRESH 0x12
#define EPD_CMD_DISPLAY_START_TRANSMISSION_DTM2 0x13
#define EPD_CMD_VCOM_INTERVAL 0x50
#define EPD_CMD_RESOLUTION_SETTING 0x61
#define EPD_CMD_UNKNOWN 0xF8

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
const oepl_display_driver_desc_t oepl_display_driver_ucvar029 =
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
  DPRINTF("Initialising UC 2.9\" variant driver\n");
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
  DPRINTF("enter UC 2.9\" variant draw\n");
  display_reinit();

  // According to upstream driver, a dummy byte seems necessary here
  const uint8_t dummy[] = {0};
  oepl_display_driver_common_data(dummy, sizeof(dummy), false);

  uint8_t* linebuf = malloc(params->x_res_effective / 8);

  DPRINTF("Black:\n");
  oepl_display_driver_common_instruction(EPD_CMD_DISPLAY_START_TRANSMISSION_DTM1, true);
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
    oepl_display_driver_common_instruction(EPD_CMD_DISPLAY_START_TRANSMISSION_DTM2, true);
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
  EMIT_INSTRUCTION_NO_DATA(EPD_CMD_POWER_OFF);
  oepl_display_driver_wait_busy(50000, true);
  EMIT_INSTRUCTION_STATIC_DATA(EPD_CMD_DEEP_SLEEP, {0xA5});
  oepl_display_driver_wait(201);

  oepl_display_driver_common_deactivate();
}

static void display_refresh_and_wait(void)
{
  DPRINTF("Sending refresh\n");
  EMIT_INSTRUCTION_NO_DATA(EPD_CMD_POWER_ON);
  sl_udelay_wait(2000);
  oepl_display_driver_wait_busy(200, true);
  EMIT_INSTRUCTION_NO_DATA(EPD_CMD_DISPLAY_REFRESH);
  sl_udelay_wait(2000);
  oepl_display_driver_wait_busy(50000, true);
}

static void display_reinit(void)
{
  // Reset the display
  display_reset();

  EMIT_INSTRUCTION_STATIC_DATA(0x4D, {0x55});
  EMIT_INSTRUCTION_STATIC_DATA(0xF3, {0x0A});
  EMIT_INSTRUCTION_STATIC_DATA(0x31, {0x00});
  EMIT_INSTRUCTION_STATIC_DATA(0x06, {0xE5, 0x35, 0x3C});
  EMIT_INSTRUCTION_STATIC_DATA(0x50, {0x57});
  EMIT_INSTRUCTION_STATIC_DATA(0x00, {0x03 | 0x04, 0x09});
}
