// -----------------------------------------------------------------------------
//                                   Includes
// -----------------------------------------------------------------------------
#include "oepl_display_driver_ucbwry.h"
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
#ifndef UCBWRY_DEBUG_PRINT
#define UCBWRY_DEBUG_PRINT 1
#endif

// -----------------------------------------------------------------------------
//                              Macros and Typedefs
// -----------------------------------------------------------------------------
#if UCBWRY_DEBUG_PRINT
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
const oepl_display_driver_desc_t oepl_display_driver_ucbwry =
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
  DPRINTF("Initialising UC BWRY variant driver\n");
  oepl_display_driver_common_init();

  if(params == NULL) {
    params = malloc(sizeof(oepl_display_parameters_t));
    if(params == NULL) {
      oepl_hw_crash(DBG_DISPLAY, true, "Can't allocate memory to save display params\n");
    }
  }

  // Make local copy since we'll be using most of these
  memcpy(params, display_params, sizeof(oepl_display_parameters_t));
}

static void display_draw(void)
{
  DPRINTF("enter UC BWRY variant draw\n");
  display_reinit();

  // According to upstream driver, a dummy byte seems necessary here
  const uint8_t dummy[] = {0};
  oepl_display_driver_common_data(dummy, sizeof(dummy), false);

  // BWRY displays need to be fed a single frame with 4 bits per pixel, instead
  // of BWR displays which are fed two 1bpp frames. This means we need to render
  // the colors for each line and then merge them into a 4bpp encoded line.

  uint8_t* drawline_b = malloc(params->x_res_effective / 8);
  uint8_t* drawline_r = malloc(params->x_res_effective / 8);
  uint8_t* drawline_y = malloc(params->x_res_effective / 8);
  uint8_t* outbuf = malloc(params->x_res_effective / 2);

  if(drawline_b == NULL || drawline_r == NULL || drawline_y == NULL || outbuf == NULL) {
    oepl_hw_crash(DBG_DISPLAY, false, "Out of memory for rendering drawlines");
  }

  oepl_display_driver_common_instruction(EPD_CMD_DISPLAY_START_TRANSMISSION_DTM1, true);

  for (uint16_t curY = 0; curY < params->y_res_effective; curY += 1) {

      memset(drawline_b, 0, params->x_res_effective / 8);
      memset(drawline_r, 0, params->x_res_effective / 8);
      memset(drawline_y, 0, params->x_res_effective / 8);

      if (params->mirrorV) {
          C_renderDrawLine(drawline_b, params->y_res_effective - curY - 1, COLOR_BLACK);
          C_renderDrawLine(drawline_r, params->y_res_effective - curY - 1, COLOR_RED);
          C_renderDrawLine(drawline_y, params->y_res_effective - curY - 1, COLOR_YELLOW);
      } else {
          C_renderDrawLine(drawline_b, curY, COLOR_BLACK);
          C_renderDrawLine(drawline_r, curY, COLOR_RED);
          C_renderDrawLine(drawline_y, curY, COLOR_YELLOW);
      }

      for (uint16_t x = 0; x < params->x_res_effective;) {
          // merge color buffers into one
          uint8_t* temp = &(outbuf[x / 2]);
          for (uint8_t shift = 0; shift < 2; shift++) {
              *temp <<= 4;
              uint8_t curByte = x / 8;
              uint8_t curMask = (1 << (7 - (x % 8)));
              if ((drawline_r[curByte] & curMask)) {
                  *temp |= 0x03;
              } else if (drawline_y[curByte] & curMask) {
                  *temp |= 0x02;
              } else if (drawline_b[curByte] & curMask) {
              } else {
                  *temp |= 0x01;
              }
              x++;
          }
      }
      // start transfer of the 4bpp data line
      oepl_display_driver_common_data(outbuf, (params->x_res_effective / 2), true);
  }

  DPRINTF("Rendering complete");

  free(drawline_b);
  free(drawline_r);
  free(drawline_y);
  free(outbuf);

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
  display_reset();
  oepl_display_driver_wait(100);
  EMIT_INSTRUCTION_NO_DATA(EPD_CMD_POWER_OFF);
  oepl_display_driver_wait(100);
  EMIT_INSTRUCTION_STATIC_DATA(EPD_CMD_DEEP_SLEEP, {0xA5});
  oepl_display_driver_wait(100);

  oepl_display_driver_common_deactivate();
}

static void display_refresh_and_wait(void)
{
  DPRINTF("Sending refresh\n");
  EMIT_INSTRUCTION_STATIC_DATA(0x68, {0x00});
  EMIT_INSTRUCTION_STATIC_DATA(0x12, {0x01});
  EMIT_INSTRUCTION_NO_DATA(EPD_CMD_DISPLAY_REFRESH);
  sl_udelay_wait(2000);
  oepl_display_driver_wait_busy(50000, true);
  sl_udelay_wait(100);
}

static void display_reinit(void)
{
  // Reset the display
  display_reset();

  EMIT_INSTRUCTION_STATIC_DATA(0x66, {0x49, 0x55, 0x13, 0x5D});
  EMIT_INSTRUCTION_STATIC_DATA(0x66, {0x49, 0x55});
  EMIT_INSTRUCTION_STATIC_DATA(0xB0, {0x03});
  EMIT_INSTRUCTION_STATIC_DATA(0x00, {0x17, 0x69});
  EMIT_INSTRUCTION_STATIC_DATA(0x03, {0x00});
  EMIT_INSTRUCTION_STATIC_DATA(0xF0, {0xF6, 0x0D, 0x00, 0x00, 0x00});
  EMIT_INSTRUCTION_STATIC_DATA(EPD_CMD_BOOSTER_SOFT_START, {0xCF, 0xDF, 0x0F});
  EMIT_INSTRUCTION_STATIC_DATA(0x41, {0x00});
  EMIT_INSTRUCTION_STATIC_DATA(EPD_CMD_VCOM_INTERVAL, {0x1F});
  EMIT_INSTRUCTION_STATIC_DATA(0x60, {0x0C, 0x05});
  EMIT_INSTRUCTION_VAR_DATA(EPD_CMD_RESOLUTION_SETTING, {params->x_res_effective & 0xFF, params->y_res_effective >> 8, params->y_res_effective & 0xff});
  EMIT_INSTRUCTION_STATIC_DATA(0x84, {0x01});
  EMIT_INSTRUCTION_STATIC_DATA(0x68, {0x01});
  EMIT_INSTRUCTION_NO_DATA(EPD_CMD_POWER_ON);

  oepl_display_driver_wait(1000);
}
