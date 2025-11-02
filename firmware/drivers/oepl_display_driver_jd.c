// -----------------------------------------------------------------------------
//                                   Includes
// -----------------------------------------------------------------------------
#include "oepl_display_driver_jd.h"
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
#ifndef JD_DEBUG_PRINT
#define JD_DEBUG_PRINT 1
#endif

// -----------------------------------------------------------------------------
//                              Macros and Typedefs
// -----------------------------------------------------------------------------
#if JD_DEBUG_PRINT
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
const oepl_display_driver_desc_t oepl_display_driver_jd =
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
  DPRINTF("Initialising JD BWRY driver\n");
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
  DPRINTF("enter JD draw\n");
  display_reinit();

  // JD BWRY displays need to be fed a single frame with 2 bits per pixel, instead
  // of BWR displays which are fed two 1bpp frames. This means we need to render
  // the colors for each line and then merge them into a 2bpp encoded line.

  uint8_t* drawline_b = malloc(params->x_res_effective / 8);
  uint8_t* drawline_r = malloc(params->x_res_effective / 8);
  uint8_t* drawline_y = malloc(params->x_res_effective / 8);
  uint8_t* outbuf = malloc(params->x_res_effective / 4);

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
          uint8_t* temp = &(outbuf[x / 4]);
          for (uint8_t shift = 0; shift < 4; shift++) {
              *temp <<= 2;
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
      // start transfer of the 2bpp data line
      oepl_display_driver_common_data(outbuf, (params->x_res_effective / 4), true);
  }

  DPRINTF("Rendering complete");

  free(drawline_b);
  free(drawline_r);
  free(drawline_y);
  free(outbuf);

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
  sl_udelay_wait(500);
  oepl_display_driver_wait_busy(2000, true);
  EMIT_INSTRUCTION_STATIC_DATA(EPD_CMD_DEEP_SLEEP, {0xA5});
  oepl_display_driver_wait(100);

  oepl_display_driver_common_deactivate();
}

static void display_refresh_and_wait(void)
{
  if((params->x_res_effective == 168 && params->y_res_effective == 384) ||
     (params->x_res_effective == 200 && params->y_res_effective == 200) ||
     (params->x_res_effective == 160 && params->y_res_effective == 296)) {
    oepl_display_driver_wait(10);
    DPRINTF("Turn on EPD power rails\n");
    EMIT_INSTRUCTION_STATIC_DATA(0x04, {0x00});
    sl_udelay_wait(500);
    oepl_display_driver_wait_busy(1000, true);
    oepl_display_driver_wait(10);
  }

  DPRINTF("Sending refresh\n");
  EMIT_INSTRUCTION_STATIC_DATA(EPD_CMD_DISPLAY_REFRESH, {0x00});
  sl_udelay_wait(2000);
  oepl_display_driver_wait_busy(50000, true);
}

static void display_reinit(void)
{
  // Reset the display
  display_reset();

  if(params->x_res_effective == 200 && params->y_res_effective == 200) {
    // From Waveshare 200x200 sample
    //  https://github.com/waveshareteam/e-Paper/blob/master/E-paper_Separate_Program/1in54_e-Paper_G/ESP32/EPD_1in54g.cpp
    EMIT_INSTRUCTION_STATIC_DATA(0x4D, {0x78});
    EMIT_INSTRUCTION_STATIC_DATA(0x00, {0x0F, 0x09});
    EMIT_INSTRUCTION_STATIC_DATA(0x06, {0x0F, 0x12, 0x30, 0x20, 0x19, 0x2A, 0x22});
    EMIT_INSTRUCTION_STATIC_DATA(0x50, {0x37});

    EMIT_INSTRUCTION_STATIC_DATA(EPD_CMD_RESOLUTION_SETTING, {0x00, 0xC8, 0x00, 0xC8});

    EMIT_INSTRUCTION_STATIC_DATA(0xE9, {0x01});
    EMIT_INSTRUCTION_STATIC_DATA(0x30, {0x08});
  } else if(params->x_res_effective == 168 && params->y_res_effective == 384) {
    // From captured waveform
    EMIT_INSTRUCTION_STATIC_DATA(0x4D, {0x78});
    EMIT_INSTRUCTION_STATIC_DATA(0x00, {0x87, 0x09});
    EMIT_INSTRUCTION_STATIC_DATA(0x01, {0x07});
    EMIT_INSTRUCTION_STATIC_DATA(0x03, {0x10, 0x54, 0x44});
    EMIT_INSTRUCTION_STATIC_DATA(0x06, {0x0F, 0x0A, 0x2F, 0x25, 0x22, 0x2E, 0x21});
    EMIT_INSTRUCTION_STATIC_DATA(0x50, {0x37});
    EMIT_INSTRUCTION_STATIC_DATA(0x60, {0x02, 0x02});
    EMIT_INSTRUCTION_VAR_DATA(EPD_CMD_RESOLUTION_SETTING, {params->x_res_effective >> 8, params->x_res_effective & 0xFF, params->y_res_effective >> 8, params->y_res_effective & 0xFF});
    EMIT_INSTRUCTION_STATIC_DATA(0xE7, {0x1C});
    EMIT_INSTRUCTION_STATIC_DATA(0xE3, {0x22});
    EMIT_INSTRUCTION_STATIC_DATA(0xB4, {0xD0});
    EMIT_INSTRUCTION_STATIC_DATA(0xB5, {0x03});
    EMIT_INSTRUCTION_STATIC_DATA(0xE9, {0x01});
    EMIT_INSTRUCTION_STATIC_DATA(0x30, {0x08});
  } else if(params->x_res_effective == 160 && params->y_res_effective == 296) {
    // From captured waveform
    DPRINTF("Pulsing reset twice\n");
    oepl_display_driver_common_pulse_reset(200, 40, 200);
    oepl_display_driver_wait(10);
    EMIT_INSTRUCTION_STATIC_DATA(0x4D, {0x78});
    EMIT_INSTRUCTION_STATIC_DATA(0x00, {0x07, 0x09});
    EMIT_INSTRUCTION_STATIC_DATA(0x01, {0x03});
    EMIT_INSTRUCTION_STATIC_DATA(0x03, {0x10, 0x54, 0x44});
    EMIT_INSTRUCTION_STATIC_DATA(0x06, {0x0F, 0x0A, 0x2F, 0x25, 0x22, 0x2E, 0x21});
    EMIT_INSTRUCTION_STATIC_DATA(0x50, {0x37});
    EMIT_INSTRUCTION_STATIC_DATA(0x60, {0x02, 0x02});
    EMIT_INSTRUCTION_VAR_DATA(EPD_CMD_RESOLUTION_SETTING, {params->x_res_effective >> 8, params->x_res_effective & 0xFF, params->y_res_effective >> 8, params->y_res_effective & 0xFF});
    EMIT_INSTRUCTION_STATIC_DATA(0xE7, {0x1C});
    EMIT_INSTRUCTION_STATIC_DATA(0xE3, {0x22});
    EMIT_INSTRUCTION_STATIC_DATA(0xB4, {0xD0});
    EMIT_INSTRUCTION_STATIC_DATA(0xB5, {0x03});
    EMIT_INSTRUCTION_STATIC_DATA(0xE9, {0x01});
    EMIT_INSTRUCTION_STATIC_DATA(0x30, {0x08});
    oepl_display_driver_wait(300);
  } else if(params->x_res_effective == 800 && params->y_res_effective == 480) {
    // From Waveshare 800x480 sample
    //   https://github.com/waveshareteam/e-Paper/blob/master/E-paper_Separate_Program/7in5_e-Paper_H/ESP32/EPD_7in5h.cpp
    EMIT_INSTRUCTION_STATIC_DATA(0x00, {0x0F, 0x29});
    EMIT_INSTRUCTION_STATIC_DATA(0x06, {0x0F, 0x8B, 0x93, 0xA1});
    EMIT_INSTRUCTION_STATIC_DATA(0x41, {0x00});
    EMIT_INSTRUCTION_STATIC_DATA(0x50, {0x37});
    EMIT_INSTRUCTION_STATIC_DATA(0x60, {0x02, 0x02});
    EMIT_INSTRUCTION_VAR_DATA(EPD_CMD_RESOLUTION_SETTING, {params->x_res_effective >> 8, params->x_res_effective & 0xFF, params->y_res_effective >> 8, params->y_res_effective & 0xFF});
    EMIT_INSTRUCTION_STATIC_DATA(0x62, {0x98, 0x98, 0x98, 0x75, 0xCA, 0xB2, 0x98, 0x7E});
    EMIT_INSTRUCTION_STATIC_DATA(0x65, {0x00, 0x00, 0x00, 0x00});
    EMIT_INSTRUCTION_STATIC_DATA(0xE7, {0x1C});
    EMIT_INSTRUCTION_STATIC_DATA(0xE3, {0x00});
    EMIT_INSTRUCTION_STATIC_DATA(0xE9, {0x01});
    EMIT_INSTRUCTION_STATIC_DATA(0x30, {0x08});

    EMIT_INSTRUCTION_NO_DATA(0x04);
    sl_udelay_wait(500);
    oepl_display_driver_wait_busy(2000, true);
  } else if(params->x_res_effective == 960 && params->y_res_effective == 640) {
    // From GDEY116F91 example
    //   https://www.good-display.com/product/543.html
    EMIT_INSTRUCTION_STATIC_DATA(0x00, {0x2F, 0x29});
    EMIT_INSTRUCTION_STATIC_DATA(0x01, {0x07, 0x00, 0x19, 0x78, 0x28, 0x19});
    EMIT_INSTRUCTION_STATIC_DATA(0x03, {0x00, 0x00, 0x00});
    EMIT_INSTRUCTION_STATIC_DATA(0x06, {0x0F, 0x98, 0xA5, 0xA0});
    EMIT_INSTRUCTION_STATIC_DATA(0x30, {0x08});
    EMIT_INSTRUCTION_STATIC_DATA(0x40, {0x00});
    EMIT_INSTRUCTION_STATIC_DATA(0x50, {0x37});
    EMIT_INSTRUCTION_STATIC_DATA(0x60, {0x04, 0x02});
    EMIT_INSTRUCTION_VAR_DATA(EPD_CMD_RESOLUTION_SETTING, {params->x_res_effective >> 8, params->x_res_effective & 0xFF, params->y_res_effective >> 8, params->y_res_effective & 0xFF});
    EMIT_INSTRUCTION_STATIC_DATA(0x65, {0x00, 0x00, 0x00, 0x00});
    EMIT_INSTRUCTION_STATIC_DATA(0xE7, {0x16});
    EMIT_INSTRUCTION_STATIC_DATA(0xE3, {0x65});
    EMIT_INSTRUCTION_STATIC_DATA(0xE0, {0x00});
    EMIT_INSTRUCTION_STATIC_DATA(0xE9, {0x01});
    EMIT_INSTRUCTION_STATIC_DATA(0x62, {0x77, 0x77, 0x77, 0x5c, 0x9f, 0x8c, 0x77, 0x63});

    EMIT_INSTRUCTION_NO_DATA(0x04);
    sl_udelay_wait(500);
    oepl_display_driver_wait_busy(2000, true);
  } else {
    oepl_hw_crash(DBG_DISPLAY, false, "Unknown display resolution for JD driver\n");
  }
}
