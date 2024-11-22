// -----------------------------------------------------------------------------
//                                   Includes
// -----------------------------------------------------------------------------
#include "oepl_display.h"
#include "oepl_nvm.h"
#include "oepl_radio.h"
#include "oepl-definitions.h"
#include "oepl_drawing_capi.h"
#include "oepl_display_driver_memlcd.h"
#include "oepl_display_driver_IL91874.h"
#include "oepl_display_driver_unissd.h"
#include "oepl_display_driver_dualssd.h"
#include "oepl_display_driver_uc8179.h"
#include "oepl_display_driver_uc8159.h"
#include "oepl_display_driver_ucvar029.h"
#include "oepl_display_driver_ucvar043.h"
#include "oepl_display_driver_GDEW0583Z83.h"
#include "fonts/fonts.h"
#include "common/bitmaps.h"

#include <stdio.h>
#include <string.h>
#include "oepl_efr32_hwtypes.h"
#include "oepl_hw_abstraction.h"
#include "sl_power_manager.h"

// -----------------------------------------------------------------------------
//                              Configuration values
// -----------------------------------------------------------------------------
#ifndef DISPLAY_DEBUG_PRINT
#define DISPLAY_DEBUG_PRINT 1
#endif

// -----------------------------------------------------------------------------
//                              Macros and Typedefs
// -----------------------------------------------------------------------------
#if DISPLAY_DEBUG_PRINT
#define DPRINTF(fmt_, ...) oepl_hw_debugprint(DBG_DISPLAY, (fmt_), ##__VA_ARGS__)
#else
#define DPRINTF(...)
#endif

#define OVERLAY_FLAG_NOT_CONNECTED  (1 << 0)
#define OVERLAY_FLAG_LOW_BATTERY    (1 << 1)

typedef struct {
  union {
    struct {
      uint64_t image_hash;
      uint32_t image_size;
      int image_idx;
    } image;
    oepl_display_infoscreen_t infoscreen;
  };
  bool is_infoscreen;
  uint32_t overlay_flags;
} display_state_t;

// -----------------------------------------------------------------------------
//                          Static Function Declarations
// -----------------------------------------------------------------------------
static void add_overlays(uint32_t overlay_mask);
static void add_rendered_content_splash(void);
static void add_rendered_content_ap_found(void);
static void add_rendered_content_ap_not_found(void);
static void add_rendered_content_deepsleep(void);
static void add_rendered_content_fwu(void);

// -----------------------------------------------------------------------------
//                                Global Variables
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
//                                Static Variables
// -----------------------------------------------------------------------------
static display_state_t current_state;
static uint32_t requested_overlay_flags;
static size_t xres, yres, num_colors;
static bool is_drawing = false;

// Todo: Log the next to-be-displayed content when async display is implemented

static const oepl_display_driver_desc_t* driver = NULL;

// -----------------------------------------------------------------------------
//                          Public Function Definitions
// -----------------------------------------------------------------------------
void oepl_display_init(oepl_efr32xg22_displayparams_t* driverconfig)
{
  switch(driverconfig->ctrl) {
    case CTRL_MEMLCD:
      driver = &oepl_display_driver_memlcd;
      break;
    case CTRL_IL91874:
      driver = &oepl_display_driver_IL91874;
      break;
    case CTRL_SSD:
      driver = &oepl_display_driver_unissd;
      break;
    case CTRL_UC8179:
      driver = &oepl_display_driver_uc8179;
      break;
    case CTRL_UC8159:
      driver = &oepl_display_driver_uc8159;
      break;
    case CTRL_DUALSSD:
      driver = &oepl_display_driver_dualssd;
      break;
    case CTRL_EPDVAR29:
      driver = &oepl_display_driver_ucvar029;
      break;
    case CTRL_EPDVAR43:
      driver = &oepl_display_driver_ucvar043;
      break;
    case CTRL_GDEW0583Z83:
      driver = &oepl_display_driver_gdew0583z83;
      break;
    default:
      oepl_hw_crash(DBG_DISPLAY, false, "Error: Lacking display driver implementation\n");
      return;
  }

  oepl_display_parameters_t displayparams = {
    .x_res = driverconfig->xres,
    .y_res = driverconfig->yres,
    .x_offset = driverconfig->xoffset,
    .y_offset = driverconfig->yoffset,
    .x_res_effective = driverconfig->xres_working,
    .y_res_effective = driverconfig->yres_working,
    .num_colors = driverconfig->have_fourthcolor ? 4 : driverconfig->have_thirdcolor ? 3 : 2,
    .swapXY = driverconfig->swapXY,
    .mirrorH = driverconfig->mirrorX,
    .mirrorV = driverconfig->mirrorY
  };

  xres = displayparams.x_res;
  yres = displayparams.y_res;
  num_colors = displayparams.num_colors;
  requested_overlay_flags = 0;

  driver->init(&displayparams);
  C_setDisplayParameters(driverconfig->swapXY, driverconfig->xres_working, driverconfig->yres_working);

  current_state.is_infoscreen = false;
  current_state.image.image_hash = 0;
  current_state.image.image_size = 0;
  current_state.image.image_idx = -1;
  current_state.overlay_flags = 0;
}

void oepl_display_show_image(size_t img_idx)
{
  oepl_stored_image_hdr_t img_meta;
  uint32_t img_addr;
  oepl_nvm_status_t nvm_status = oepl_nvm_read_image_metadata(img_idx, &img_meta);
  if(nvm_status != NVM_SUCCESS) {
    DPRINTF("NVM metadata error during display update\n");
    return;
  }

  if(!current_state.is_infoscreen &&
     img_meta.md5 == current_state.image.image_hash &&
     img_meta.size == current_state.image.image_size &&
     requested_overlay_flags == current_state.overlay_flags) {
    DPRINTF("Requested image and overlays are already on screen\n");
    return;
  }

  if(!img_meta.is_valid) {
    DPRINTF("Image was not marked valid\n");
    return;
  }

  nvm_status = oepl_nvm_get_image_raw_address(img_idx, &img_addr);
  if(nvm_status != NVM_SUCCESS) {
    DPRINTF("Couldn't get image address\n");
    return;
  }

  C_flushDrawItems();
  C_drawFlashFullscreenImageWithType(img_addr, img_meta.image_format, img_meta.size);
  add_overlays(requested_overlay_flags);

  DPRINTF("Showing image in slot %d\n", img_idx);

  is_drawing = true;
  driver->draw();
  is_drawing = false;
  current_state.image.image_hash = img_meta.md5;
  current_state.image.image_size = img_meta.size;
  current_state.image.image_idx = img_idx;
  current_state.overlay_flags = requested_overlay_flags;
  current_state.is_infoscreen = false;
}

void oepl_display_show_infoscreen(oepl_display_infoscreen_t screen)
{
  if(current_state.is_infoscreen &&
     current_state.infoscreen == screen &&
     requested_overlay_flags == current_state.overlay_flags) {
    // Already displaying this content
    DPRINTF("Already displaying this info screen\n");
    return;
  }

  // Find out if we have a stored image for this info screen
  uint8_t imgtype;
  switch(screen) {
    case INFOSCREEN_DEEPSLEEP:
      imgtype = CUSTOM_IMAGE_LONGTERMSLEEP;
      break;
    case INFOSCREEN_BOOT:
      imgtype = CUSTOM_IMAGE_SPLASHSCREEN;
      break;
    case INFOSCREEN_BOOT_FOUND_AP:
      imgtype = CUSTOM_IMAGE_APFOUND;
      break;
    case INFOSCREEN_LONG_SCAN:
      imgtype = CUSTOM_IMAGE_NOAPFOUND;
      break;
    case INFOSCREEN_LOST_CONNECTION:
      imgtype = CUSTOM_IMAGE_LOST_CONNECTION;
      break;
    case INFOSCREEN_FWU:
      imgtype = 0xFF;
      break;
    case INFOSCREEN_WAKEUP_BUTTON1:
      imgtype = CUSTOM_IMAGE_BUTTON1;
      break;
    case INFOSCREEN_WAKEUP_BUTTON2:
      imgtype = CUSTOM_IMAGE_BUTTON2;
      break;
    case INFOSCREEN_WAKEUP_GPIO:
      imgtype = CUSTOM_IMAGE_GPIO;
      break;
    case INFOSCREEN_WAKEUP_NFC:
      imgtype = CUSTOM_IMAGE_NFC_WAKE;
      break;
    case INFOSCREEN_WAKEUP_RFWAKE:
      imgtype = CUSTOM_IMAGE_RF_WAKE;
      break;
  }

  size_t override_imgidx, img_size;
  if(oepl_nvm_get_image_by_type(imgtype, &override_imgidx, &img_size) == NVM_SUCCESS) {
    oepl_display_show_image(override_imgidx);
    return;
  }

  // Fell through, create our own screen
  C_flushDrawItems();

  switch(screen) {
    case INFOSCREEN_DEEPSLEEP:
      add_rendered_content_deepsleep();
      break;
    case INFOSCREEN_BOOT:
      add_rendered_content_splash();
      break;
    case INFOSCREEN_BOOT_FOUND_AP:
      add_rendered_content_ap_found();
      break;
    case INFOSCREEN_LONG_SCAN:
      add_rendered_content_ap_not_found();
      break;
    case INFOSCREEN_LOST_CONNECTION:
      add_rendered_content_ap_not_found();
      break;
    case INFOSCREEN_FWU:
      add_rendered_content_fwu();
      break;
    case INFOSCREEN_WAKEUP_BUTTON1:
      // Fallthrough
    case INFOSCREEN_WAKEUP_BUTTON2:
      // Fallthrough
    case INFOSCREEN_WAKEUP_GPIO:
      // Fallthrough
    case INFOSCREEN_WAKEUP_NFC:
      // Fallthrough
    case INFOSCREEN_WAKEUP_RFWAKE:
      // Wakeup events aren't rendered - they're only shown if a custom image is present
      return;
  }

  add_overlays(requested_overlay_flags);
  is_drawing = true;
  driver->draw();
  is_drawing = false;
  current_state.overlay_flags = requested_overlay_flags;
  current_state.is_infoscreen = true;
  current_state.infoscreen = screen;
}

void oepl_display_set_overlay(oepl_display_overlay_t overlay, bool show)
{
  uint32_t mask = 0;
  uint8_t overlay_enabled = 1;
  switch(overlay) {
    case ICON_NOT_CONNECTED:
      mask = OVERLAY_FLAG_NOT_CONNECTED;
      if(oepl_nvm_setting_get(OEPL_ENABLE_NORF_ICON, &overlay_enabled, sizeof(overlay_enabled)) != NVM_SUCCESS) {
        // Default to showing overlays
        overlay_enabled = 1;
      }
      break;
    case ICON_LOW_BATTERY:
      mask = OVERLAY_FLAG_LOW_BATTERY;
      if(oepl_nvm_setting_get(OEPL_ENABLE_LOWBAT_ICON, &overlay_enabled, sizeof(overlay_enabled)) != NVM_SUCCESS) {
        // Default to showing overlays
        overlay_enabled = 1;
      }
      break;
  }

  DPRINTF("%s overlay mask 0x%08x\n", overlay_enabled > 0 ? show ? "Set" : "Clear" : "Disabled", mask);

  if(show && (overlay_enabled > 0)) {
    requested_overlay_flags |= mask;
  } else {
    requested_overlay_flags &= ~mask;
  }
}

bool oepl_display_is_drawing(void)
{
  return is_drawing;
}

void oepl_display_draw(oepl_display_draw_done_cb_t cb)
{
  // Todo: async display updates
  (void)cb;
  if(current_state.is_infoscreen) {
    oepl_display_show_infoscreen(current_state.infoscreen);
  } else {
    oepl_display_show_image(current_state.image.image_idx);
  }
}

// -----------------------------------------------------------------------------
//                          Static Function Definitions
// -----------------------------------------------------------------------------
static void add_overlays(uint32_t overlay_mask)
{
  if(overlay_mask & OVERLAY_FLAG_LOW_BATTERY) {
    C_drawMask(xres - 27, yres - 26, 22, 22, COLOR_BLACK);
    if (num_colors >= 3) {
      C_drawMask(xres - 27, yres - 26, 22, 22, COLOR_RED);
      C_drawRoundedRectangle(xres - 28, yres - 26, 24, 24, COLOR_RED);
    } else {
      C_drawMask(xres - 27, yres - 26, 22, 22, COLOR_BLACK);
      C_drawRoundedRectangle(xres - 28, yres - 26, 24, 24, COLOR_BLACK);
    }
    C_addBufferedImage(xres - 24, yres - 19, COLOR_BLACK, ROTATE_0, battery, DRAW_NORMAL);
  }

  if(overlay_mask & OVERLAY_FLAG_NOT_CONNECTED) {
    C_drawMask(xres - 28, 4, 24, 24, COLOR_BLACK);
    if (num_colors >= 3) {
      C_drawMask(xres - 28, 4, 24, 24, COLOR_RED);
      C_drawRoundedRectangle(xres - 28, 4, 24, 24, COLOR_RED);
      C_addBufferedImage(xres - 24, 8, COLOR_BLACK, ROTATE_0, ant, DRAW_NORMAL);
      C_addBufferedImage(xres - 16, 15, COLOR_RED, ROTATE_0, cross, DRAW_NORMAL);
    } else {
      C_drawRoundedRectangle(xres - 28, 4, 24, 24, COLOR_BLACK);
      C_addBufferedImage(xres - 24, 8, COLOR_BLACK, ROTATE_0, ant, DRAW_NORMAL);
      C_addBufferedImage(xres - 16, 15, COLOR_BLACK, ROTATE_0, cross, DRAW_NORMAL);
    }
  }
}

static void add_rendered_content_splash(void)
{
  uint8_t hwid = oepl_hw_get_hwid();
  uint8_t mac[8];
  oepl_radio_get_mac(mac);
  uint16_t fw = oepl_hw_get_swversion();
  const char* suffix = oepl_hw_get_swsuffix();
  switch (hwid) {
    case SOLUM_M3_PEGHOOK_BWR_13:
      // Fallthrough
    case SOLUM_M3_BWR_16:
      C_epdSetFont(&FreeSans9pt7b);
      C_epdPrintf(2, 2, COLOR_BLACK, ROTATE_0, "OpenEPaperLink");
      if (hwid == SOLUM_M3_PEGHOOK_BWR_13) {
        C_epdPrintf(2, 38, COLOR_RED, ROTATE_0, "Newton M3 1.3 Peghook\"");
      } else {
        C_epdPrintf(10, 38, COLOR_RED, ROTATE_0, "Newton M3 1.6\"");
      }
      C_epdPrintf(5, yres - 40, COLOR_BLACK, ROTATE_0, "FW: %04X-%s", fw, suffix);
      C_epdPrintf(2, yres - 20, COLOR_BLACK, ROTATE_0, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[5], mac[6], mac[7]);
      break;
    case SOLUM_M3_BWR_22:
      C_epdSetFont(&FreeSansBold18pt7b);
      C_epdPrintf(2, 2, COLOR_BLACK, ROTATE_0, "OpenEPaperLink");
      C_epdSetFont(&FreeSans9pt7b);
      C_epdPrintf(10, 38, COLOR_RED, ROTATE_0, "Newton M3 2.2\"");
      C_epdPrintf(5, yres - 40, COLOR_BLACK, ROTATE_0, "FW: %04X-%s", fw, suffix);
      C_epdPrintf(5, yres - 20, COLOR_BLACK, ROTATE_0, "MAC: %02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], mac[6], mac[7]);
      C_addQR(xres - 120, 42, 3, 3, "https://openepaperlink.eu/tag/0/%02X/%02X%02X%02X%02X%02X%02X%02X%02X/", hwid, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], mac[6], mac[7]);
      break;
    case SOLUM_M3_BWR_22_LITE:
      C_epdSetFont(&FreeSans9pt7b);
      C_epdPrintf(2, 2, COLOR_BLACK, ROTATE_0, "OpenEPaperLink");
      C_epdSetFont(&FreeSans9pt7b);
      C_epdPrintf(10, 38, COLOR_RED, ROTATE_0, "Newton M3 2.2\" LITE");
      C_epdPrintf(5, yres - 40, COLOR_BLACK, ROTATE_0, "FW: %04X-%s", fw, suffix);
      C_epdPrintf(5, yres - 20, COLOR_BLACK, ROTATE_0, "MAC: %02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], mac[6], mac[7]);
      break;
    case SOLUM_M3_BWR_26:
      C_epdSetFont(&FreeSansBold18pt7b);
      C_epdPrintf(2, 2, COLOR_BLACK, ROTATE_0, "OpenEPaperLink");
      C_epdSetFont(&FreeSans9pt7b);
      C_epdPrintf(10, 38, COLOR_RED, ROTATE_0, "Newton M3 2.6\"");
      C_epdPrintf(5, yres - 40, COLOR_BLACK, ROTATE_0, "FW: %04X-%s", fw, suffix);
      C_epdPrintf(5, yres - 20, COLOR_BLACK, ROTATE_0, "MAC: %02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], mac[6], mac[7]);
      C_addQR(xres - 120, 42, 3, 3, "https://openepaperlink.eu/tag/0/%02X/%02X%02X%02X%02X%02X%02X%02X%02X/", hwid, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], mac[6], mac[7]);
      break;
    case SOLUM_M3_BWR_29:
      // Fallthrough
    case SOLUM_M3_BW_29:
      C_epdSetFont(&FreeSansBold18pt7b);
      C_epdPrintf(2, 2, COLOR_BLACK, ROTATE_0, "OpenEPaperLink");
      C_epdSetFont(&FreeSans9pt7b);
      if(hwid == SOLUM_M3_BWR_29) {
        C_epdPrintf(10, 38, COLOR_RED, ROTATE_0, "Newton M3 2.9\"");
      } else {
        C_epdPrintf(10, 38, COLOR_BLACK, ROTATE_0, "Newton M3 2.9 Freezer\"");
      }
      C_epdPrintf(xres - 17, 0, COLOR_BLACK, ROTATE_270, "FW: %04X-%s", fw, suffix);
      C_epdPrintf(5, yres - 20, COLOR_BLACK, ROTATE_0, "MAC: %02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], mac[6], mac[7]);
      C_addQR(xres - 120, 42, 3, 3, "https://openepaperlink.eu/tag/0/%02X/%02X%02X%02X%02X%02X%02X%02X%02X/", hwid, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], mac[6], mac[7]);
      break;
    case SOLUM_M3_BWR_42:
      C_epdSetFont(&FreeSansBold18pt7b);
      C_epdPrintf(2, 2, COLOR_BLACK, ROTATE_0, "OpenEPaperLink");
      C_epdSetFont(&FreeSans9pt7b);
      C_epdPrintf(10, 38, 1, ROTATE_0, "Newton M3 4.2\"");
      C_epdPrintf(xres - 17, 0, COLOR_BLACK, ROTATE_270, "FW: %04X-%s", fw, suffix);
      C_epdPrintf(5, yres - 20, COLOR_BLACK, ROTATE_0, "MAC: %02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], mac[6], mac[7]);
      C_addQR(xres - 120, 120, 3, 3, "https://openepaperlink.eu/tag/0/%02X/%02X%02X%02X%02X%02X%02X%02X%02X/", hwid, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], mac[6], mac[7]);
      break;
    case SOLUM_M3_BWR_43:
      C_epdSetFont(&FreeSansBold24pt7b);
      C_epdPrintf(7, 7, COLOR_BLACK, ROTATE_0, "OpenEPaperLink");
      C_epdSetFont(&FreeSansBold18pt7b);
      C_epdPrintf(15, 60, COLOR_RED, ROTATE_0, "Newton M3 4.3\"");
      C_epdSetFont(&FreeSans9pt7b);
      C_epdPrintf(xres - 17, 0, COLOR_BLACK, ROTATE_270, "FW: %04X-%s", fw, suffix);
      C_epdPrintf(10, yres - 25, COLOR_BLACK, ROTATE_0, "MAC: %02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], mac[6], mac[7]);
      C_addQR(xres - 120, 32, 3, 3, "https://openepaperlink.eu/tag/0/%02X/%02X%02X%02X%02X%02X%02X%02X%02X/", hwid, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], mac[6], mac[7]);
      break;
    case SOLUM_M3_BW_58:
      // Fallthrough
    case SOLUM_M3_BWR_58:
      C_epdSetFont(&FreeSansBold18pt7b);
      C_epdPrintf(2, 2, COLOR_BLACK, ROTATE_0, "OpenEPaperLink");
      C_epdSetFont(&FreeSans9pt7b);
      if(hwid == SOLUM_M3_BWR_58) {
        C_epdPrintf(10, 38, COLOR_RED, ROTATE_0, "Newton M3 5.85\"");
      } else {
        C_epdPrintf(10, 38, COLOR_BLACK, ROTATE_0, "Newton M3 5.85\" Freezer");
      }
      C_epdSetFont(&FreeSans9pt7b);
      C_epdPrintf(xres - 17, 0, COLOR_BLACK, ROTATE_270, "FW: %04X-%s", fw, suffix);
      C_epdPrintf(5, yres - 20, COLOR_BLACK, ROTATE_0, "MAC: %02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], mac[6], mac[7]);
      C_addQR(xres - 120, 42, 3, 3, "https://openepaperlink.eu/tag/0/%02X/%02X%02X%02X%02X%02X%02X%02X%02X/", hwid, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], mac[6], mac[7]);
      break;
    case SOLUM_M3_BWR_60:
      C_epdSetFont(&FreeSansBold24pt7b);
      C_epdPrintf(10, 10, COLOR_BLACK, ROTATE_0, "OpenEPaperLink");
      C_epdSetFont(&FreeSansBold18pt7b);
      C_epdPrintf(15, 60, 1, ROTATE_0, "Newton M3 6.0\"");
      C_epdSetFont(&FreeSans9pt7b);
      C_epdPrintf(xres - 17, 310, COLOR_BLACK, ROTATE_270, "FW: %04X-%s", fw, suffix);
      C_epdPrintf(10, yres - 25, COLOR_BLACK, ROTATE_0, "MAC: %02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], mac[6], mac[7]);
      C_addFlashImage(293, 61, COLOR_BLACK, ROTATE_0, newton);
      C_addQR(40, 120, 3, 7, "https://openepaperlink.eu/tag/0/%02X/%02X%02X%02X%02X%02X%02X%02X%02X/", hwid, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], mac[6], mac[7]);
      break;
    case SOLUM_M3_BWR_75:
      C_epdSetFont(&FreeSansBold24pt7b);
      C_epdPrintf(10, 10, COLOR_BLACK, ROTATE_0, "OpenEPaperLink");
      C_epdSetFont(&FreeSansBold18pt7b);
      C_epdPrintf(15, 60, COLOR_RED, ROTATE_0, "Newton M3 7.5\"");
      C_epdSetFont(&FreeSans9pt7b);
      C_epdPrintf(xres - 17, 310, COLOR_BLACK, ROTATE_270, "FW: %04X-%s", fw, suffix);
      C_epdPrintf(10, yres - 25, COLOR_BLACK, ROTATE_0, "MAC: %02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], mac[6], mac[7]);
      C_addFlashImage(420, 81, COLOR_BLACK, ROTATE_0, newton);
      C_addQR(100, 160, 3, 7, "https://openepaperlink.eu/tag/0/%02X/%02X%02X%02X%02X%02X%02X%02X%02X/", hwid, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], mac[6], mac[7]);
      break;
    case SOLUM_M3_BWR_97:
      C_epdSetFont(&FreeSansBold24pt7b);
      C_epdPrintf(10, 10, COLOR_BLACK, ROTATE_0, "OpenEPaperLink");
      C_epdSetFont(&FreeSansBold18pt7b);
      C_epdPrintf(15, 60, COLOR_RED, ROTATE_0, "Newton M3 9.7\"");
      C_epdSetFont(&FreeSans9pt7b);
      C_epdPrintf(xres - 37, 310, COLOR_BLACK, ROTATE_270, "FW: %04X-%s", fw, suffix);
      C_epdPrintf(10, yres - 25, COLOR_BLACK, ROTATE_0, "MAC: %02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], mac[6], mac[7]);
      C_addFlashImage(220, 420, COLOR_BLACK, ROTATE_0, newton);
      C_addQR(260, 160, 3, 7, "https://openepaperlink.eu/tag/0/%02X/%02X%02X%02X%02X%02X%02X%02X%02X/", hwid, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], mac[6], mac[7]);
      break;
    default:
      C_epdSetFont(&FreeSans9pt7b);
      C_epdPrintf(2, 2, COLOR_BLACK, ROTATE_0, "OpenEPaperLink");
      if (num_colors >= 3) {
        C_epdPrintf(2, 38, COLOR_RED, ROTATE_0, "EFR32xG22 ID 0x%02X", hwid);
      } else {
        C_epdPrintf(2, 38, COLOR_BLACK, ROTATE_0, "EFR32xG22 ID 0x%02X", hwid);
      }
      C_epdPrintf(5, yres - 40, COLOR_BLACK, ROTATE_0, "FW: %04X-%s", fw, suffix);
      C_epdPrintf(2, yres - 20, COLOR_BLACK, ROTATE_0, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[5], mac[6], mac[7]);
      break;
  }
}

static void add_rendered_content_ap_found(void)
{
  int8_t rssi, temperature = -127;
  uint8_t lqi;
  uint8_t APmac[8], mac[8];
  uint8_t currentChannel;
  uint8_t hwid = oepl_hw_get_hwid();
  uint16_t mv = 0;
  bool accentcolor = num_colors >= 3 ? COLOR_RED : COLOR_BLACK;

  if(!oepl_radio_get_ap_link(&currentChannel, APmac, &lqi, &rssi)) {
    rssi = -127;
    lqi = 0;
    currentChannel = 0;
    memset(APmac, 0, sizeof(APmac));
  }

  oepl_hw_get_temperature(&temperature);
  oepl_hw_get_voltage(&mv, false);

  oepl_radio_get_mac(mac);

  // Start with largest screen layout and work our way down to smallest
  if(xres >= 600 && yres >= 480) {
    // Layout for 6" and above is the same
    C_epdSetFont(&FreeSansBold18pt7b);
    C_epdPrintf(7, 7, COLOR_BLACK, ROTATE_0, "AP Found - Waiting for data");
    C_epdSetFont(&FreeSans9pt7b);
    C_epdPrintf(15, 55, accentcolor, ROTATE_0, "AP: %02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X", APmac[7], APmac[6], APmac[5], APmac[4], APmac[3], APmac[2], APmac[1], APmac[0]);
    C_epdPrintf(15, 73, accentcolor, ROTATE_0, "RSSI: %ddBm    LQI: %d", rssi, lqi);
    C_epdSetFont(&FreeSansBold18pt7b);
    C_epdPrintf(270, 55, accentcolor, ROTATE_0, "Ch %d", currentChannel);
    C_epdSetFont(&FreeSans9pt7b);
    C_epdPrintf(10, yres - 43, COLOR_BLACK, ROTATE_0, "Battery: %d.%02dV Temp: %d'C", mv / 1000, (mv % 1000)/10, temperature);
    C_epdPrintf(10, yres - 25, COLOR_BLACK, ROTATE_0, "MAC: %02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], mac[6], mac[7]);
    C_addQR(xres - 66, 47, 3, 2, "https://openepaperlink.eu/tag/1/%02X/%02X%02X%02X%02X%02X%02X%02X%02X/", hwid, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], mac[6], mac[7]);
  } else if(xres >= 792 && yres >= 272) {
    // 5.8" (weird aspect ratio)
    C_epdSetFont(&FreeSansBold18pt7b);
    C_epdPrintf(7, 7, COLOR_BLACK, ROTATE_0, "AP Found");
    C_epdSetFont(&FreeSans9pt7b);
    C_epdPrintf(10, 53, accentcolor, ROTATE_0, "%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X", APmac[7], APmac[6], APmac[5], APmac[4], APmac[3], APmac[2], APmac[1], APmac[0]);
    C_epdPrintf(10, 71, accentcolor, ROTATE_0, "RSSI: %ddBm    LQI: %d", rssi, lqi);
    C_epdPrintf(10, 89, accentcolor, ROTATE_0, "Ch %d", currentChannel);
    C_epdSetFont(&FreeSans9pt7b);
    C_epdPrintf(10, yres - 43, COLOR_BLACK, ROTATE_0, "Battery: %d.%02dV Temp: %d'C", mv / 1000, (mv % 1000)/10, temperature);
    C_epdPrintf(10, yres - 25, COLOR_BLACK, ROTATE_0, "MAC: %02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], mac[6], mac[7]);
    C_addQR(xres - 120, 42, 3, 3, "https://openepaperlink.eu/tag/0/%02X/%02X%02X%02X%02X%02X%02X%02X%02X/", hwid, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], mac[6], mac[7]);
  } else if(xres >= 522 && yres >= 122) {
    // 4.3" (weird aspect ratio)
    C_epdSetFont(&FreeSansBold18pt7b);
    C_epdPrintf(7, 7, COLOR_BLACK, ROTATE_0, "AP Found - Waiting for data");
    C_epdSetFont(&FreeSans9pt7b);
    C_epdPrintf(15, 55, accentcolor, ROTATE_0, "AP: %02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X", APmac[7], APmac[6], APmac[5], APmac[4], APmac[3], APmac[2], APmac[1], APmac[0]);
    C_epdPrintf(15, 73, accentcolor, ROTATE_0, "RSSI: %ddBm    LQI: %d", rssi, lqi);
    C_epdSetFont(&FreeSansBold18pt7b);
    C_epdPrintf(270, 55, accentcolor, ROTATE_0, "Ch %d", currentChannel);
    C_epdSetFont(&FreeSans9pt7b);
    C_epdPrintf(10, yres - 43, COLOR_BLACK, ROTATE_0, "Battery: %d.%02dV Temp: %d'C", mv / 1000, (mv % 1000)/10, temperature);
    C_epdPrintf(10, yres - 25, COLOR_BLACK, ROTATE_0, "MAC: %02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], mac[6], mac[7]);
    C_addQR(xres - 66, 47, 3, 2, "https://openepaperlink.eu/tag/1/%02X/%02X%02X%02X%02X%02X%02X%02X%02X/", hwid, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], mac[6], mac[7]);
  } else if(xres >= 400 && yres >= 300) {
    // 4.2"
    C_epdSetFont(&FreeSansBold18pt7b);
    C_epdPrintf(7, 7, COLOR_BLACK, ROTATE_0, "AP Found");
    C_epdSetFont(&FreeSans9pt7b);
    C_epdPrintf(10, 53, accentcolor, ROTATE_0, "%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X", APmac[7], APmac[6], APmac[5], APmac[4], APmac[3], APmac[2], APmac[1], APmac[0]);
    C_epdPrintf(10, 71, accentcolor, ROTATE_0, "RSSI: %ddBm    LQI: %d", rssi, lqi);
    C_epdPrintf(10, 89, accentcolor, ROTATE_0, "Ch %d", currentChannel);
    C_epdSetFont(&FreeSans9pt7b);
    C_epdPrintf(10, yres - 43, COLOR_BLACK, ROTATE_0, "Battery: %d.%02dV Temp: %d'C", mv / 1000, (mv % 1000)/10, temperature);
    C_epdPrintf(10, yres - 25, COLOR_BLACK, ROTATE_0, "MAC: %02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], mac[6], mac[7]);
    C_addQR(xres - 66, 47, 3, 2, "https://openepaperlink.eu/tag/1/%02X/%02X%02X%02X%02X%02X%02X%02X%02X/", hwid, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], mac[6], mac[7]);
  } else if(xres >= 360 && yres >= 184) {
    // 2.6"
    C_epdSetFont(&FreeSansBold18pt7b);
    C_epdPrintf(7, 7, COLOR_BLACK, ROTATE_0, "AP Found");
    C_epdSetFont(&FreeSans9pt7b);
    C_epdPrintf(10, 53, accentcolor, ROTATE_0, "%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X", APmac[7], APmac[6], APmac[5], APmac[4], APmac[3], APmac[2], APmac[1], APmac[0]);
    C_epdPrintf(10, 71, accentcolor, ROTATE_0, "RSSI: %ddBm    LQI: %d", rssi, lqi);
    C_epdPrintf(10, 89, accentcolor, ROTATE_0, "Ch %d", currentChannel);
    C_epdSetFont(&FreeSans9pt7b);
    C_epdPrintf(10, yres - 43, COLOR_BLACK, ROTATE_0, "Battery: %d.%02dV Temp: %d'C", mv / 1000, (mv % 1000)/10, temperature);
    C_epdPrintf(10, yres - 25, COLOR_BLACK, ROTATE_0, "MAC: %02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], mac[6], mac[7]);
    C_addQR(xres - 66, 47, 3, 2, "https://openepaperlink.eu/tag/1/%02X/%02X%02X%02X%02X%02X%02X%02X%02X/", hwid, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], mac[6], mac[7]);
  } else if(xres >= 284 && yres >= 168) {
    // 2.9"
    C_epdSetFont(&FreeSansBold18pt7b);
    C_epdPrintf(7, 7, COLOR_BLACK, ROTATE_0, "AP Found");
    C_epdSetFont(&FreeSans9pt7b);
    C_epdPrintf(10, 53, accentcolor, ROTATE_0, "%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X", APmac[7], APmac[6], APmac[5], APmac[4], APmac[3], APmac[2], APmac[1], APmac[0]);
    C_epdPrintf(10, 71, accentcolor, ROTATE_0, "RSSI: %ddBm    LQI: %d", rssi, lqi);
    C_epdPrintf(10, 89, accentcolor, ROTATE_0, "Ch %d", currentChannel);
    C_epdSetFont(&FreeSans9pt7b);
    C_epdPrintf(10, yres - 43, COLOR_BLACK, ROTATE_0, "Battery: %d.%02dV Temp: %d'C", mv / 1000, (mv % 1000)/10, temperature);
    C_epdPrintf(10, yres - 25, COLOR_BLACK, ROTATE_0, "MAC: %02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], mac[6], mac[7]);
    C_addQR(xres - 66, 47, 3, 2, "https://openepaperlink.eu/tag/1/%02X/%02X%02X%02X%02X%02X%02X%02X%02X/", hwid, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], mac[6], mac[7]);
  } else if(xres >= 296 && yres >= 160) {
    // 2.2"
    C_epdSetFont(&FreeSansBold18pt7b);
    C_epdPrintf(7, 7, COLOR_BLACK, ROTATE_0, "AP Found");
    C_epdSetFont(&FreeSans9pt7b);
    C_epdPrintf(10, 53, accentcolor, ROTATE_0, "%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X", APmac[7], APmac[6], APmac[5], APmac[4], APmac[3], APmac[2], APmac[1], APmac[0]);
    C_epdPrintf(10, 71, accentcolor, ROTATE_0, "RSSI: %ddBm    LQI: %d", rssi, lqi);
    C_epdPrintf(10, 89, accentcolor, ROTATE_0, "Ch %d", currentChannel);
    C_epdSetFont(&FreeSans9pt7b);
    C_epdPrintf(10, yres - 43, COLOR_BLACK, ROTATE_0, "Battery: %d.%02dV Temp: %d'C", mv / 1000, (mv % 1000)/10, temperature);
    C_epdPrintf(10, yres - 25, COLOR_BLACK, ROTATE_0, "MAC: %02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], mac[6], mac[7]);
    C_addQR(xres - 66, 47, 3, 2, "https://openepaperlink.eu/tag/1/%02X/%02X%02X%02X%02X%02X%02X%02X%02X/", hwid, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], mac[6], mac[7]);
  } else {
    // Anything smaller
    C_epdSetFont(&FreeSans9pt7b);
    C_epdPrintf(7, 6, COLOR_BLACK, ROTATE_0, "AP Found");
    C_epdPrintf(0, 24, accentcolor, ROTATE_0, "%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X", APmac[7], APmac[6], APmac[5], APmac[4], APmac[3], APmac[2], APmac[1], APmac[0]);
    C_epdPrintf(5, 42, accentcolor, ROTATE_0, "RSSI: %ddBm    LQI: %d", rssi, lqi);
    C_epdPrintf(5, 60, accentcolor, ROTATE_0, "Ch %d", currentChannel);
    C_epdSetFont(&FreeSans9pt7b);
    C_epdPrintf(5, yres - 43, COLOR_BLACK, ROTATE_0, "Battery: %d.%02dV Temp: %d'C", mv / 1000, (mv % 1000)/10, temperature);
    C_epdPrintf(0, yres - 25, COLOR_BLACK, ROTATE_0, "%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], mac[6], mac[7]);
  }
}

static void add_rendered_content_ap_not_found(void)
{
  uint8_t hwid = oepl_hw_get_hwid();
  uint8_t mac[8];
  oepl_radio_get_mac(mac);

  // Start with largest screen layout and work our way down to smallest
  if(xres >= 960 && yres >= 672) {
    // 9.7"
    C_epdSetFont(&FreeSansBold18pt7b);
    C_epdPrintf(7, 7, COLOR_BLACK, ROTATE_0, "No AP Found          U_U");
    C_epdSetFont(&FreeSans9pt7b);
    C_addQR(xres - 66, 47, 3, 2, "https://openepaperlink.eu/tag/1/%02X/%02X%02X%02X%02X%02X%02X%02X%02X/", hwid, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], mac[6], mac[7]);
    C_epdPrintf(10, 39, COLOR_BLACK, ROTATE_0, "Couldn't find an AP :(");
    C_epdPrintf(10, 58, COLOR_BLACK, ROTATE_0, "I'll try again in a little while, but you");
    C_epdPrintf(10, 77, COLOR_BLACK, ROTATE_0, "can force a retry now by scanning");
    C_epdPrintf(10, 98, COLOR_BLACK, ROTATE_0, "the NFC-wake area with your phone");
    C_addFlashImage(200, 128, COLOR_BLACK, ROTATE_0, pandablack);
    C_addFlashImage(312, 274, COLOR_RED, ROTATE_0, pandared);
  } else if(xres >= 880 && yres >= 528) {
    // 7.5"
    C_epdSetFont(&FreeSansBold18pt7b);
    C_epdPrintf(7, 7, COLOR_BLACK, ROTATE_0, "No AP Found          U_U");
    C_epdSetFont(&FreeSans9pt7b);
    C_addQR(xres - 66, 47, 3, 2, "https://openepaperlink.eu/tag/1/%02X/%02X%02X%02X%02X%02X%02X%02X%02X/", hwid, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], mac[6], mac[7]);
    C_epdPrintf(10, 39, COLOR_BLACK, ROTATE_0, "Couldn't find an AP :(");
    C_epdPrintf(10, 58, COLOR_BLACK, ROTATE_0, "I'll try again in a little while, but you");
    C_epdPrintf(10, 77, COLOR_BLACK, ROTATE_0, "can force a retry now by pressing a button");
    C_addFlashImage(200, 128, COLOR_BLACK, ROTATE_0, pandablack);
    C_addFlashImage(312, 274, COLOR_RED, ROTATE_0, pandared);
  } else if(xres >= 600 && yres >= 480) {
    // 6"
    C_epdSetFont(&FreeSansBold18pt7b);
    C_epdPrintf(7, 7, COLOR_BLACK, ROTATE_0, "No AP Found          U_U");
    C_epdSetFont(&FreeSans9pt7b);
    C_addQR(xres - 66, 47, 3, 2, "https://openepaperlink.eu/tag/1/%02X/%02X%02X%02X%02X%02X%02X%02X%02X/", hwid, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], mac[6], mac[7]);
    C_epdPrintf(10, 39, COLOR_BLACK, ROTATE_0, "Couldn't find an AP :(");
    C_epdPrintf(10, 58, COLOR_BLACK, ROTATE_0, "I'll try again in a little while, but you");
    C_epdPrintf(10, 77, COLOR_BLACK, ROTATE_0, "can force a retry now by pressing a button");
    C_addFlashImage(0, 96, COLOR_BLACK, ROTATE_0, pandablack);
    C_addFlashImage(112, 242, COLOR_RED, ROTATE_0, pandared);
  } else if(xres >= 792 && yres >= 272) {
    // 5.8" (weird aspect ratio)
    C_epdSetFont(&FreeSansBold18pt7b);
    C_epdPrintf(7, 7, COLOR_BLACK, ROTATE_0, "No AP Found");
    C_epdSetFont(&FreeSans9pt7b);
    C_addQR(xres - 120, 42, 3, 3, "https://openepaperlink.eu/tag/0/%02X/%02X%02X%02X%02X%02X%02X%02X%02X/", hwid, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], mac[6], mac[7]);
    C_epdPrintf(10, 69, COLOR_BLACK, ROTATE_0, "Couldn't find an AP :(");
    C_epdPrintf(10, 89, COLOR_BLACK, ROTATE_0, "I'll try again in a little while, but you");
    C_epdPrintf(152, 109, COLOR_BLACK, ROTATE_0, "can force a retry now by scanning");
    C_epdPrintf(152, 129, COLOR_BLACK, ROTATE_0, "the NFC-wake area with your phone");
  } else if(xres >= 522 && yres >= 122) {
    // 4.3" (weird aspect ratio)
    C_epdSetFont(&FreeSansBold18pt7b);
    C_epdPrintf(7, 7, COLOR_BLACK, ROTATE_0, "No AP Found          UwU");
    C_epdSetFont(&FreeSans9pt7b);
    C_addQR(xres - 66, 47, 3, 2, "https://openepaperlink.eu/tag/1/%02X/%02X%02X%02X%02X%02X%02X%02X%02X/", hwid, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], mac[6], mac[7]);
    C_drawRoundedRectangle(36, 55, 112, 42, COLOR_RED);
    C_epdPrintf(44, 61, COLOR_BLACK, ROTATE_0, "NFC WAKE");
    C_epdPrintf(41, 77, COLOR_BLACK, ROTATE_0, "SCAN HERE");

    C_epdPrintf(152, 49, COLOR_BLACK, ROTATE_0, "Couldn't find an AP :(");
    C_epdPrintf(152, 69, COLOR_BLACK, ROTATE_0, "I'll try again in a little while, but you");
    C_epdPrintf(152, 89, COLOR_BLACK, ROTATE_0, "can force a retry now by scanning");
    C_epdPrintf(152, 109, COLOR_BLACK, ROTATE_0, "the NFC-wake area with your phone");
  } else if(xres >= 400 && yres >= 300) {
    // 4.2"
    C_epdSetFont(&FreeSansBold18pt7b);
    C_epdPrintf(7, 7, COLOR_BLACK, ROTATE_0, "No AP Found");
    C_epdSetFont(&FreeSans9pt7b);
    C_addQR(xres - 66, 47, 3, 2, "https://openepaperlink.eu/tag/1/%02X/%02X%02X%02X%02X%02X%02X%02X%02X/", hwid, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], mac[6], mac[7]);
    C_epdPrintf(10, 69, COLOR_BLACK, ROTATE_0, "Couldn't find an AP :(");
    C_epdPrintf(10, 89, COLOR_BLACK, ROTATE_0, "I'll try again in a little while, but you");
    C_epdPrintf(10, 109, COLOR_BLACK, ROTATE_0, "can force a retry now by pressing a button");
  } else if(xres >= 360 && yres >= 184) {
    // 2.6"
    C_epdSetFont(&FreeSansBold18pt7b);
    C_epdPrintf(7, 7, COLOR_BLACK, ROTATE_0, "No AP Found");
    C_epdSetFont(&FreeSans9pt7b);
    C_addQR(xres - 66, 47, 3, 2, "https://openepaperlink.eu/tag/1/%02X/%02X%02X%02X%02X%02X%02X%02X%02X/", hwid, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], mac[6], mac[7]);
    C_epdPrintf(10, 69, COLOR_BLACK, ROTATE_0, "Couldn't find an AP :(");
    C_epdPrintf(10, 89, COLOR_BLACK, ROTATE_0, "I'll try again in a little while, but you");
    C_epdPrintf(10, 109, COLOR_BLACK, ROTATE_0, "can force a retry now by scanning");
    C_epdPrintf(10, 129, COLOR_BLACK, ROTATE_0, "the NFC-wake area with your phone");
  } else if(xres >= 284 && yres >= 168) {
    // 2.9"
    C_epdSetFont(&FreeSansBold18pt7b);
    C_epdPrintf(7, 7, COLOR_BLACK, ROTATE_0, "No AP Found");
    C_epdSetFont(&FreeSans9pt7b);
    C_addQR(xres - 66, 47, 3, 2, "https://openepaperlink.eu/tag/1/%02X/%02X%02X%02X%02X%02X%02X%02X%02X/", hwid, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], mac[6], mac[7]);
    C_epdPrintf(10, 69, COLOR_BLACK, ROTATE_0, "Couldn't find an AP :(");
    C_epdPrintf(10, 89, COLOR_BLACK, ROTATE_0, "I'll try again in a little while, but you");
    C_epdPrintf(10, 109, COLOR_BLACK, ROTATE_0, "can force a retry now by pressing a button");
  } else if(xres >= 296 && yres >= 160) {
    // 2.2"
    C_epdSetFont(&FreeSansBold18pt7b);
    C_epdPrintf(7, 7, COLOR_BLACK, ROTATE_0, "No AP Found");
    C_epdSetFont(&FreeSans9pt7b);
    C_addQR(xres - 66, 47, 3, 2, "https://openepaperlink.eu/tag/1/%02X/%02X%02X%02X%02X%02X%02X%02X%02X/", hwid, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], mac[6], mac[7]);
    C_epdPrintf(10, 69, COLOR_BLACK, ROTATE_0, "Couldn't find an AP :(");
    C_epdPrintf(10, 89, COLOR_BLACK, ROTATE_0, "I'll try again in a little while, but you");
    C_epdPrintf(10, 109, COLOR_BLACK, ROTATE_0, "can force a retry now by pressing a button");
  } else {
    // Anything smaller
    C_epdSetFont(&FreeSans9pt7b);
    C_epdPrintf(7, 7, COLOR_BLACK, ROTATE_0, "NO AP Found");
    C_epdPrintf(2, 25, COLOR_BLACK, ROTATE_0, "Couldn't find an AP :(");
  }
}

static void add_rendered_content_deepsleep(void)
{
  C_epdSetFont(&FreeSansBold24pt7b);
  C_epdPrintf((xres / 2) - 12, (yres / 2) - 12, COLOR_BLACK, ROTATE_0, "zZz");
}

static void add_rendered_content_fwu(void)
{
  C_epdSetFont(&FreeSansBold24pt7b);
  C_epdPrintf((xres / 2) - 12, (yres / 2) - 12, COLOR_BLACK, ROTATE_0, "FWU");
}
