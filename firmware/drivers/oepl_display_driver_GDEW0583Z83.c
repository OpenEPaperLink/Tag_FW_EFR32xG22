// -----------------------------------------------------------------------------
//                                   Includes
// -----------------------------------------------------------------------------
#include "oepl_display_driver_GDEW0583Z83.h"
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
#ifndef GDEW0583Z83_DEBUG_PRINT
#define GDEW0583Z83_DEBUG_PRINT 1
#endif

// -----------------------------------------------------------------------------
//                              Macros and Typedefs
// -----------------------------------------------------------------------------
#if GDEW0583Z83_DEBUG_PRINT
#define DPRINTF(fmt_, ...) oepl_hw_debugprint(DBG_DISPLAY, (fmt_), ##__VA_ARGS__)
#else
#define DPRINTF(...)
#endif

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
const oepl_display_driver_desc_t oepl_display_driver_gdew0583z83 =
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
  DPRINTF("Initialising GDEW0583Z83 driver\n");
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
  DPRINTF("enter GDEW0583Z83 draw\n");
  display_reinit();

  uint8_t* linebuf = malloc(params->x_res_effective / 8);

  DPRINTF("Black:\n");
  oepl_display_driver_common_instruction(0x10, true);
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
    oepl_display_driver_common_instruction(0x13, true);
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
  oepl_display_driver_common_pulse_reset(10, 1, 1);
  oepl_display_driver_wait_busy(1, true);
}

static void display_sleep(void)
{
  oepl_display_driver_wait(1);
  EMIT_INSTRUCTION_STATIC_DATA(0x50, {0xF7});
  oepl_display_driver_wait(10);
  EMIT_INSTRUCTION_NO_DATA(0x02);
  oepl_display_driver_wait(10);

  oepl_display_driver_common_deactivate();
}

static void display_refresh_and_wait(void)
{
  DPRINTF("Sending refresh\n");
  oepl_display_driver_wait(1);
  EMIT_INSTRUCTION_NO_DATA(0x04);
  sl_udelay_wait(2000);
  oepl_display_driver_wait_busy(0, true);
  EMIT_INSTRUCTION_NO_DATA(0x12);
  sl_udelay_wait(2000);
  oepl_display_driver_wait_busy(0, true);
}

static void display_reinit(void)
{
  // Reset the display
  display_reset();

  EMIT_INSTRUCTION_STATIC_DATA(0x06, {0xEF, 0xEE, 0x38});
  EMIT_INSTRUCTION_STATIC_DATA(0xF8, {0x60, 0xA5});
  EMIT_INSTRUCTION_STATIC_DATA(0xF8, {0x89, 0xA5});
  EMIT_INSTRUCTION_STATIC_DATA(0xF8, {0xA1, 0x00});
  EMIT_INSTRUCTION_STATIC_DATA(0xF8, {0x73, 0x05});
  EMIT_INSTRUCTION_STATIC_DATA(0xF8, {0x7E, 0x31});
  EMIT_INSTRUCTION_STATIC_DATA(0xF8, {0xB8, 0x80});
  EMIT_INSTRUCTION_STATIC_DATA(0xE8, {0x00});
  EMIT_INSTRUCTION_STATIC_DATA(0x26, {0x0F});
  EMIT_INSTRUCTION_STATIC_DATA(0x00, {0x03});
  EMIT_INSTRUCTION_STATIC_DATA(0x61, {0x02, 0x88, 0x01, 0xE0});
  EMIT_INSTRUCTION_STATIC_DATA(0x50, {0x77});
  EMIT_INSTRUCTION_STATIC_DATA(0xE0, {0x02});
  EMIT_INSTRUCTION_STATIC_DATA(0xE5, {0x1A});

  oepl_display_driver_wait(2);
}
