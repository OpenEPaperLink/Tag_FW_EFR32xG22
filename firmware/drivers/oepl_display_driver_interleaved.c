// -----------------------------------------------------------------------------
//                                   Includes
// -----------------------------------------------------------------------------
#include "oepl_display_driver_interleaved.h"
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
#ifndef INTERLEAVED_DEBUG_PRINT
#define INTERLEAVED_DEBUG_PRINT 1
#endif

// -----------------------------------------------------------------------------
//                              Macros and Typedefs
// -----------------------------------------------------------------------------
#if INTERLEAVED_DEBUG_PRINT
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
const oepl_display_driver_desc_t oepl_display_driver_interleaved =
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
  DPRINTF("Initialising interleaved BWRY driver\n");
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
  DPRINTF("enter interleaved draw\n");
  display_reinit();

  // These weird AF controllers are fed display lines two at a time, reverse
  // interleaved in 2bpp BWRY format.
  // 01 = white
  // 00 = black
  // 11 = red
  // 10 = yellow
  // Example:
  // line x   =    W  W  W  W  Y  Y  W  W
  // line x+1 =    R  Y  B  W  Y  Y  W  W
  // output bits = RW YW BW WW YY YY WW WW
  // i.e. x1y0 x0y0 x1y1 x0y1 etc

  uint8_t* drawline_b = malloc(params->x_res_effective / 4);
  uint8_t* drawline_r = malloc(params->x_res_effective / 4);
  uint8_t* drawline_y = malloc(params->x_res_effective / 4);
  uint8_t* outbuf = malloc(params->x_res_effective / 2);

  if(drawline_b == NULL || drawline_r == NULL || drawline_y == NULL || outbuf == NULL) {
    oepl_hw_crash(DBG_DISPLAY, false, "Out of memory for rendering drawlines");
  }

  oepl_display_driver_common_instruction(EPD_CMD_DISPLAY_START_TRANSMISSION_DTM1, true);

  for (uint16_t curY = 0; curY < params->y_res_effective; curY += 2) {

      memset(drawline_b, 0, params->x_res_effective / 4);
      memset(drawline_r, 0, params->x_res_effective / 4);
      memset(drawline_y, 0, params->x_res_effective / 4);

      if (params->mirrorV) {
          C_renderDrawLine(drawline_b, params->y_res_effective - curY - 1, COLOR_BLACK);
          C_renderDrawLine(drawline_r, params->y_res_effective - curY - 1, COLOR_RED);
          C_renderDrawLine(drawline_y, params->y_res_effective - curY - 1, COLOR_YELLOW);
          C_renderDrawLine(drawline_b[params->x_res_effective / 8], params->y_res_effective - curY, COLOR_BLACK);
          C_renderDrawLine(drawline_r[params->x_res_effective / 8], params->y_res_effective - curY, COLOR_RED);
          C_renderDrawLine(drawline_y[params->x_res_effective / 8], params->y_res_effective - curY, COLOR_YELLOW);
      } else {
          C_renderDrawLine(&drawline_b[params->x_res_effective / 8], curY, COLOR_BLACK);
          C_renderDrawLine(&drawline_r[params->x_res_effective / 8], curY, COLOR_RED);
          C_renderDrawLine(&drawline_y[params->x_res_effective / 8], curY, COLOR_YELLOW);
          C_renderDrawLine(drawline_b, curY + 1, COLOR_BLACK);
          C_renderDrawLine(drawline_r, curY + 1, COLOR_RED);
          C_renderDrawLine(drawline_y, curY + 1, COLOR_YELLOW);
      }

      for (uint16_t x = 0; x < params->x_res_effective;) {
          // merge color buffers into one
          uint8_t* temp = &(outbuf[x / 2]);
          for (uint8_t shift = 0; shift < 2; shift++) {
              uint8_t curByte = x / 8;
              uint8_t curMask = (1 << (7 - (x % 8)));

              *temp <<= 2;
              if ((drawline_r[curByte] & curMask)) {
                  *temp |= 0x03;
              } else if (drawline_y[curByte] & curMask) {
                  *temp |= 0x02;
              } else if (drawline_b[curByte] & curMask) {
              } else {
                  *temp |= 0x01;
              }

              *temp <<= 2;
              if ((drawline_r[curByte + params->x_res_effective / 8] & curMask)) {
                  *temp |= 0x03;
              } else if (drawline_y[curByte + params->x_res_effective / 8] & curMask) {
                  *temp |= 0x02;
              } else if (drawline_b[curByte + params->x_res_effective / 8] & curMask) {
              } else {
                  *temp |= 0x01;
              }
              x++;
          }
      }
      // start transfer of the 2bpp 2-line data line
      oepl_display_driver_common_data(outbuf, (params->x_res_effective / 2), true);
  }

  DPRINTF("Rendering complete");

  free(drawline_b);
  free(drawline_r);
  free(drawline_y);
  free(outbuf);

  EMIT_INSTRUCTION_NO_DATA(0x04);
  oepl_display_driver_wait_busy(5000, true);
  oepl_display_driver_wait(5);

  display_refresh_and_wait();
  display_sleep();

  DPRINTF("Display sleeping");
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
  oepl_display_driver_common_pulse_reset(200, 40, 200);
}

static void display_sleep(void)
{
  sl_udelay_wait(500);
  EMIT_INSTRUCTION_STATIC_DATA(EPD_CMD_POWER_OFF, {0x00});
  oepl_display_driver_wait_busy(200, true);
  EMIT_INSTRUCTION_STATIC_DATA(EPD_CMD_DEEP_SLEEP, {0xA5});
  oepl_display_driver_wait(2000);

  oepl_display_driver_common_deactivate();
}

static void display_refresh_and_wait(void)
{
  DPRINTF("Sending refresh\n");
  EMIT_INSTRUCTION_STATIC_DATA(EPD_CMD_DISPLAY_REFRESH, {0x00});
  sl_udelay_wait(200);
  oepl_display_driver_wait_busy(50000, true);
}

static void display_reinit(void)
{
  // Reset the display
  display_reset();

  EMIT_INSTRUCTION_STATIC_DATA(0x00, {0x07, 0x29});
  EMIT_INSTRUCTION_STATIC_DATA(0x50, {0x37});

  // Unknown how much time the display actually needs, delay in the captured trace
  // is probably due to waking up the SPI flash and start processing data
  oepl_display_driver_wait(50);
}
