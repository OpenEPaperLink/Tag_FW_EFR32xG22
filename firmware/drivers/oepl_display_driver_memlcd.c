// -----------------------------------------------------------------------------
//                                   Includes
// -----------------------------------------------------------------------------
#include "oepl_display_driver_memlcd.h"
#include "oepl_drawing_capi.h"

#include "sl_board_control.h"
#include "sl_memlcd.h"

// -----------------------------------------------------------------------------
//                              Configuration values
// -----------------------------------------------------------------------------
#ifndef MEMLCD_DEBUG_PRINT
#define MEMLCD_DEBUG_PRINT 1
#endif

// -----------------------------------------------------------------------------
//                              Macros and Typedefs
// -----------------------------------------------------------------------------
#if MEMLCD_DEBUG_PRINT
#define DPRINTF(fmt_, ...) oepl_hw_debugprint(DBG_DISPLAY, (fmt_), ##__VA_ARGS__)
#else
#define DPRINTF(...)
#endif

// -----------------------------------------------------------------------------
//                          Static Function Declarations
// -----------------------------------------------------------------------------
static void display_init(const oepl_display_parameters_t* params);
static void display_draw(void);

// -----------------------------------------------------------------------------
//                                Global Variables
// -----------------------------------------------------------------------------
const oepl_display_driver_desc_t oepl_display_driver_memlcd =
{
  .init = &display_init,
  .draw = &display_draw
};

// -----------------------------------------------------------------------------
//                                Static Variables
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
//                          Public Function Definitions
// -----------------------------------------------------------------------------
static void display_init(const oepl_display_parameters_t* params)
{
  sl_board_enable_display();

  sl_memlcd_t memlcd = {
    .width = params->x_res,
    .height = params->y_res,
    .bpp = params->num_colors == 2 ? 1 : 2,
    .color_mode = SL_MEMLCD_COLOR_MODE_MONOCHROME,
    .spi_freq = 1100000,
    .extcomin_freq = 60,
    .setup_us = 6,
    .hold_us = 2,
    .custom_data = NULL,
  };

  sl_memlcd_configure(&memlcd);
  sl_memlcd_clear(sl_memlcd_get());
  sl_memlcd_power_on(sl_memlcd_get(), false);
}

static void display_draw(void)
{
  sl_memlcd_refresh(sl_memlcd_get());
  sl_memlcd_power_on(sl_memlcd_get(), true);

  for(size_t row = 0; row < 128; row++) {
    uint8_t buf[128/8];
    C_renderDrawLine(buf, row, 0);
    for(size_t i = 0; i < sizeof(buf); i++) {
      buf[i] = SL_RBIT(buf[i]) >> 24;
    }
    sl_memlcd_refresh(sl_memlcd_get());
    sl_memlcd_draw(sl_memlcd_get(), buf, row, 128);
  }

  sl_memlcd_power_on(sl_memlcd_get(), false);
}

// -----------------------------------------------------------------------------
//                          Static Function Definitions
// -----------------------------------------------------------------------------
