#ifndef OEPL_DISPLAY_DRIVER_H
#define OEPL_DISPLAY_DRIVER_H

// -----------------------------------------------------------------------------
//                                   Includes
// -----------------------------------------------------------------------------
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// -----------------------------------------------------------------------------
//                              Macros and Typedefs
// -----------------------------------------------------------------------------
typedef struct {
  size_t x_res;               // Number of pixels in horizontal direction
  size_t y_res;               // Number of pixels in vertical direction
  size_t x_res_effective;     // Number of drawn pixels per scan line
  size_t y_res_effective;     // Number of drawn scan lines per frame
  size_t x_offset;            // X-Offset in controller buffer to start scanning
  size_t y_offset;            // Y-Offset in controller buffer to start scanning
  size_t num_colors;          // BW = 2, BWR or BWY = 3, BWRY = 4
  bool swapXY;                // Whether scan direction is top-bottom or left-right
  bool mirrorH;               // Mirror pixels in the scan line
  bool mirrorV;               // Load lines bottom-to-top instead of top-to-bottom
} oepl_display_parameters_t;

typedef void (*oepl_display_driver_init_t)(const oepl_display_parameters_t* params);
typedef void (*oepl_display_driver_draw_t)(void);

typedef struct {
  oepl_display_driver_init_t init;
  oepl_display_driver_draw_t draw;
} oepl_display_driver_desc_t;

// -----------------------------------------------------------------------------
//                                Global Variables
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
//                          Public Function Declarations
// -----------------------------------------------------------------------------

#endif