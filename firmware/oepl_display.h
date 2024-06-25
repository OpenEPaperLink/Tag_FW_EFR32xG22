#ifndef OEPL_DISPLAY_H
#define OEPL_DISPLAY_H

// -----------------------------------------------------------------------------
//                                   Includes
// -----------------------------------------------------------------------------
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "oepl_efr32_hwtypes.h"

// -----------------------------------------------------------------------------
//                              Macros and Typedefs
// -----------------------------------------------------------------------------
typedef enum {
  ICON_NOT_CONNECTED,
  ICON_LOW_BATTERY
} oepl_display_overlay_t;

typedef enum {
  INFOSCREEN_DEEPSLEEP,
  INFOSCREEN_BOOT,
  INFOSCREEN_BOOT_FOUND_AP,
  INFOSCREEN_LONG_SCAN,
  INFOSCREEN_LOST_CONNECTION,
  INFOSCREEN_FWU,
  INFOSCREEN_WAKEUP_BUTTON1,
  INFOSCREEN_WAKEUP_BUTTON2,
  INFOSCREEN_WAKEUP_GPIO,
  INFOSCREEN_WAKEUP_NFC,
  INFOSCREEN_WAKEUP_RFWAKE
} oepl_display_infoscreen_t;

typedef void (*oepl_display_draw_done_cb_t)(void);

// -----------------------------------------------------------------------------
//                                Global Variables
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
//                          Public Function Declarations
// -----------------------------------------------------------------------------
/**************************************************************************//**
 * Initialize display hardware
 *****************************************************************************/
void oepl_display_init(oepl_efr32xg22_displayparams_t* driverconfig);

/**************************************************************************//**
 * This is the event loop function for the display driver. Call it for
 * each iteration of the main event loop such that it can process events.
 * 
 * Needs to be serviced as long as oepl_display_is_event_pending() returns true
 *****************************************************************************/
void oepl_display_process(void);

/**************************************************************************//**
 * Does the display driver require calling its event handler again?
 *****************************************************************************/
bool oepl_display_is_event_pending(void);

/**************************************************************************//**
 * Show an image from a persistent slot (and add currently enabled overlays).
 * Showing an image will override the previous image or info screen.
 * 
 * Note that this only sets the internal state. Updating the display is done
 * with oepl_display_draw().
 *****************************************************************************/
void oepl_display_show_image(size_t img_idx);

/**************************************************************************//**
 * Show an info screen (and add currently enabled overlays).
 * Info screen is either runtime generated or read from persistent storage
 * (if available).
 * Showing an infoscreen will override the previous image or info screen.
 * 
 * Note that this only sets the internal state. Updating the display is done
 * with oepl_display_draw().
 *****************************************************************************/
void oepl_display_show_infoscreen(oepl_display_infoscreen_t screen);

/**************************************************************************//**
 * Add/remove an overlay
 *****************************************************************************/
void oepl_display_set_overlay(oepl_display_overlay_t overlay, bool show);

/**************************************************************************//**
 * Check whether the display driver is currently drawing the screen
 *****************************************************************************/
bool oepl_display_is_drawing(void);

/**************************************************************************//**
 * Start a display refresh
 * 
 * The registered callback will be called when the refresh cycle is done.
 *****************************************************************************/
void oepl_display_draw(oepl_display_draw_done_cb_t cb);

#endif