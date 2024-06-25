// -----------------------------------------------------------------------------
//                                   Includes
// -----------------------------------------------------------------------------
#include "oepl_display_driver_dualssd.h"
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
#ifndef DUALSSD_DEBUG_PRINT
#define DUALSSD_DEBUG_PRINT 1
#endif

// -----------------------------------------------------------------------------
//                              Macros and Typedefs
// -----------------------------------------------------------------------------
#if DUALSSD_DEBUG_PRINT
#define DPRINTF(fmt_, ...) oepl_hw_debugprint(DBG_DISPLAY, (fmt_), ##__VA_ARGS__)
#else
#define DPRINTF(...)
#endif

#define CMD_DRV_OUTPUT_CTRL 0x01
#define CMD_SOFT_START_CTRL 0x0C
#define CMD_ENTER_SLEEP 0x10
#define CMD_DATA_ENTRY_MODE 0x11
#define CMD_SOFT_RESET 0x12
#define CMD_SOFT_RESET2 0x13
#define CMD_SETUP_VOLT_DETECT 0x15
#define CMD_TEMP_SENSOR_CONTROL 0x18
#define CMD_ACTIVATION 0x20
#define CMD_DISP_UPDATE_CTRL 0x21
#define CMD_DISP_UPDATE_CTRL2 0x22
#define CMD_WRITE_FB_BW 0x24
#define CMD_WRITE_FB_RED 0x26
#define CMD_VCOM_GLITCH_CTRL 0x2B
#define CMD_LOAD_OTP_LUT 0x31
#define CMD_WRITE_LUT 0x32
#define CMD_BORDER_WAVEFORM_CTRL 0x3C
#define CMD_WINDOW_X_SIZE 0x44
#define CMD_WINDOW_Y_SIZE 0x45
#define CMD_WRITE_PATTERN_RED 0x46
#define CMD_WRITE_PATTERN_BW 0x47
#define CMD_XSTART_POS 0x4E
#define CMD_YSTART_POS 0x4F
#define CMD_ANALOG_BLK_CTRL 0x74
#define CMD_DIGITAL_BLK_CTRL 0x7E

#define SCREEN_CMD_CLOCK_ON 0x80
#define SCREEN_CMD_CLOCK_OFF 0x01
#define SCREEN_CMD_ANALOG_ON 0x40
#define SCREEN_CMD_ANALOG_OFF 0x02
#define SCREEN_CMD_LATCH_TEMPERATURE_VAL 0x20
#define SCREEN_CMD_LOAD_LUT 0x10
#define SCREEN_CMD_USE_MODE_2 0x08  // modified commands 0x10 and 0x04
#define SCREEN_CMD_REFRESH 0xC7

#define CONTROLLER_ONE 0x00
#define CONTROLLER_TWO 0x80

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
const oepl_display_driver_desc_t oepl_display_driver_dualssd =
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
  DPRINTF("Initialising DualSSD driver\n");
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
  DPRINTF("enter DualSSD draw\n");
  display_reinit();

  uint8_t* linebuf = malloc(params->x_res_effective / 8);

  DPRINTF("Black 1:\n");
  oepl_display_driver_common_instruction(CMD_WRITE_FB_BW + CONTROLLER_ONE, true);
  oepl_display_scan_frame(
    // Buffer + buffer size
    linebuf, params->x_res_effective/8,
    // Which part of the buffer to output
    params->x_res_effective/16, params->x_res_effective/16 + 1,
    // Which lines to scan
    params->y_offset, params->y_offset + params->y_res_effective,
    // Which color to scan
    0,
    // Which modifications to make
    params->mirrorH, params->mirrorV
  );

  if(params->num_colors > 2) {
    DPRINTF("RED 1:\n");
    oepl_display_driver_common_instruction(CMD_WRITE_FB_RED + CONTROLLER_ONE, true);
    oepl_display_scan_frame(
      // Buffer + buffer size
      linebuf, params->x_res_effective/8,
      // Which part of the buffer to output
      params->x_res_effective/16, params->x_res_effective/16 + 1,
      // Which lines to scan
      params->y_offset, params->y_offset + params->y_res_effective,
      // Which color to scan
      1,
      // Which modifications to make
      params->mirrorH, params->mirrorV
    );
  }

  DPRINTF("Black 2:\n");
  oepl_display_driver_common_instruction(CMD_WRITE_FB_BW + CONTROLLER_TWO, true);
  oepl_display_scan_frame(
    // Buffer + buffer size
    linebuf, params->x_res_effective/8,
    // Which part of the buffer to output
    0, params->x_res_effective/16 + 1,
    // Which lines to scan
    params->y_offset, params->y_offset + params->y_res_effective,
    // Which color to scan
    0,
    // Which modifications to make
    params->mirrorH, params->mirrorV
  );

  if(params->num_colors > 2) {
    DPRINTF("RED 2:\n");
    oepl_display_driver_common_instruction(CMD_WRITE_FB_RED + CONTROLLER_TWO, true);
    oepl_display_scan_frame(
      // Buffer + buffer size
      linebuf, params->x_res_effective/8,
      // Which part of the buffer to output
      0, params->x_res_effective/16 + 1,
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
  EMIT_INSTRUCTION_STATIC_DATA(CMD_ENTER_SLEEP, {0x01});
  oepl_display_driver_wait(10);

  oepl_display_driver_common_deactivate();
}

static void display_refresh_and_wait(void)
{
  DPRINTF("Sending refresh\n");
  EMIT_INSTRUCTION_NO_DATA(CMD_ACTIVATION);
  sl_udelay_wait(2000);
  oepl_display_driver_wait_busy(120000, false);
}

static void display_reinit(void)
{
  // Reset the display
  display_reset();

  EMIT_INSTRUCTION_STATIC_DATA(CMD_DATA_ENTRY_MODE + CONTROLLER_ONE, {0x02});
  EMIT_INSTRUCTION_STATIC_DATA(CMD_DATA_ENTRY_MODE + CONTROLLER_TWO, {0x03});

  if (params->num_colors > 2) {
      EMIT_INSTRUCTION_STATIC_DATA(CMD_DISP_UPDATE_CTRL, {0x08, 0x10});
  } else {
      EMIT_INSTRUCTION_STATIC_DATA(CMD_DISP_UPDATE_CTRL, {0x48, 0x10});
  }

  EMIT_INSTRUCTION_STATIC_DATA(CMD_WINDOW_X_SIZE + CONTROLLER_ONE, {0x31, 0x00});
  EMIT_INSTRUCTION_STATIC_DATA(CMD_WINDOW_Y_SIZE + CONTROLLER_ONE, {0x00, 0x00, 0x0F, 0x01});
  EMIT_INSTRUCTION_STATIC_DATA(CMD_XSTART_POS + CONTROLLER_ONE, {0x31});
  EMIT_INSTRUCTION_STATIC_DATA(CMD_YSTART_POS + CONTROLLER_ONE, {0x00, 0x00});

  EMIT_INSTRUCTION_STATIC_DATA(CMD_WINDOW_X_SIZE + CONTROLLER_TWO, {0x00, 0x31});
  EMIT_INSTRUCTION_STATIC_DATA(CMD_WINDOW_Y_SIZE + CONTROLLER_TWO, {0x00, 0x00, 0x0F, 0x01});
  EMIT_INSTRUCTION_STATIC_DATA(CMD_XSTART_POS + CONTROLLER_TWO, {0x00});
  EMIT_INSTRUCTION_STATIC_DATA(CMD_YSTART_POS + CONTROLLER_TWO, {0x0F, 0x01});

  EMIT_INSTRUCTION_STATIC_DATA(CMD_BORDER_WAVEFORM_CTRL, {0x01});
}
