// -----------------------------------------------------------------------------
//                                   Includes
// -----------------------------------------------------------------------------
#include "oepl_app.h"
#include "oepl_radio.h"
#include "oepl_hw_abstraction.h"
#include "oepl_nvm.h"
#include "oepl_led.h"
#include "oepl_display.h"
#include "md5.h"

#include <stdio.h>
#include <stddef.h>
#include <string.h>

#include "em_device.h"

// -----------------------------------------------------------------------------
//                              Configuration values
// -----------------------------------------------------------------------------
#ifndef APP_DEBUG_PRINT
#define APP_DEBUG_PRINT 1
#endif

#define EVENT_FLAG_BUTTON_1     (1 << 0)
#define EVENT_FLAG_BUTTON_2     (1 << 1)
#define EVENT_FLAG_GPIO         (1 << 2)
#define EVENT_FLAG_NFC_WAKE     (1 << 3)
#define EVENT_FLAG_CONNECTED    (1 << 4)
#define EVENT_FLAG_DISCONNECTED (1 << 5)

// -----------------------------------------------------------------------------
//                              Macros and Typedefs
// -----------------------------------------------------------------------------
#if APP_DEBUG_PRINT
#define DPRINTF(...) oepl_hw_debugprint(DBG_APP, __VA_ARGS__)
#else
#define DPRINTF(...)
#endif

// Todo: propagate these into shared OEPL definitions
#define IMG_EXTTYPE_PRELOAD_FLAG 0x04
#define IMG_EXTTYPE_LUT_MASK 0x03
#define IMG_EXTTYPE_IMGID_MASK 0xF8
#define IMG_EXTTYPE_IMGID_SHIFT 0x03
#define IMG_EXTTYPE_IMGID_FROM_EXTTYPE( exttype ) ((uint8_t)((uint8_t)(exttype & IMG_EXTTYPE_IMGID_MASK) >> IMG_EXTTYPE_IMGID_SHIFT))

typedef enum {
  BOOT_FACTORY_FRESH,
  BOOT_POWERCYCLE,
  BOOT_UPGRADE
} boot_type_t;

typedef enum {
  MODE_NORMAL = 0,
  MODE_SLIDESHOW_FAST = 0x06,
  MODE_SLIDESHOW_MEDIUM = 0x07,
  MODE_SLIDESHOW_SLOW = 0x08,
  MODE_SLIDESHOW_GLACIAL = 0x09,
  MODE_WAIT_RFWAKE = 0x20
} application_mode_t;

typedef enum {
  BOOT,
  CONNECTED,
  DISCONNECTED,
  DATA_AVAILABLE,
  DOWNLOAD,
  AWAITING_CONFIRMATION,
  CONFIRMATION_RECEIVED
} application_state_t;

// -----------------------------------------------------------------------------
//                          Static Function Declarations
// -----------------------------------------------------------------------------
static oepl_radio_action_t radio_event_handler(oepl_radio_event_t event, const void* event_data);
static void application_state_transition(application_state_t new_state);
static void application_mode_transition(application_mode_t new_mode);
static void application_process_data(oepl_radio_data_indication_t* data);
static void application_process_datablock(oepl_radio_blockrecv_t* block);
static bool application_check_md5(const uint8_t* data, size_t bytes, const uint8_t* reference);
static void oepl_app_button_handler(oepl_hw_gpio_channel_t button, oepl_hw_gpio_event_t event);

// data processing handlers, could be moved into separate files for clarity
static bool application_process_image_block(size_t index, const uint8_t* data, size_t length, bool is_last);
static bool application_process_fwu_block(size_t index, const uint8_t* data, size_t length, bool is_last);
static bool application_process_config_block(size_t index, const uint8_t* data, size_t length, bool is_last);
static bool application_process_nfcu_block(size_t index, const uint8_t* data, size_t length, bool is_last);
static bool application_process_nfcr_block(size_t index, const uint8_t* data, size_t length, bool is_last);

// -----------------------------------------------------------------------------
//                                Global Variables
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
//                                Static Variables
// -----------------------------------------------------------------------------
static oepl_radio_data_indication_t data_to_process;
static oepl_datablock_descriptor_t datablock_in_progress;
static const uint8_t* datablock = NULL;

static application_state_t current_state = BOOT;
static bool stay_awake = false;
static bool have_seen_ap = false;

static volatile uint32_t event_flags = 0;

// -----------------------------------------------------------------------------
//                          Public Function Definitions
// -----------------------------------------------------------------------------
bool oepl_app_is_event_pending(void)
{
  return (event_flags != 0) || stay_awake;
}

void oepl_app_init(void)
{
  // Called once out of reset. Do all hardware setup, load all configuration
  // values.

  // Initialize the common hardware resources
  oepl_hw_init();

  // Check if an upgrade was applied, and if so, tell the FWU system to not
  // indicate this on next boot.
  bool was_upgraded = oepl_fwu_is_upgraded();
  if(was_upgraded) {
    DPRINTF("We were upgraded!\n");
    oepl_fwu_confirm_upgrade();
  }

  // If not bypassed, show splashscreen
  uint8_t fastboot;
  if(oepl_nvm_setting_get(OEPL_ENABLE_FASTBOOT, &fastboot, sizeof(fastboot)) == NVM_SUCCESS && fastboot != 0) {
    // Skip the boot screen
  } else {
    oepl_display_show_infoscreen(INFOSCREEN_BOOT);
  }

  // Check whether there is a stored channel
  uint32_t last_channel;
  oepl_nvm_status_t status = oepl_nvm_setting_get(OEPL_LAST_CONNECTED_CHANNEL, &last_channel, sizeof(last_channel));
  if(status != NVM_SUCCESS) {
    last_channel = 0;
  } else {
    DPRINTF("Have previously been connected on ch%ld\n", last_channel);
  }
  // Setup and start the radio
  oepl_radio_init(radio_event_handler, WAKEUP_REASON_FIRSTBOOT, last_channel);
  // Setup the external interrupts and callback
  oepl_hw_init_gpio(oepl_app_button_handler);
  return;
}

/**************************************************************************//**
 * This is the event loop function for the OEPL app implementation. Call it for
 * each iteration of the main event loop such that it can process events.
 *****************************************************************************/
void oepl_app_process(void)
{
  // Service the radio first, since it may trigger one or more callbacks.
  if(oepl_radio_is_event_pending()) {
    oepl_radio_process();
  }

  // Application event loop
  switch(current_state) {
    case BOOT:
      // Nothing to see here, waiting for the radio process to tell us we're
      // either connected or orphaned
      break;
    case CONNECTED:
      // Nothing to see here, waiting for the radio process to tell us we've
      // got a pending action, or we've been disconnected due to the AP going
      // AWOL.
      break;
    case DATA_AVAILABLE:
      // Process the received data indication from the AP. This state will 
      // always trigger an immediate state transition to either a
      // downloading or (back to) connected state.
      switch(data_to_process.AP_data.dataType) {
        case DATATYPE_NOUPDATE:
          DPRINTF("NOP indication shouldn't bubble up to app\n");
          application_state_transition(CONNECTED);
          break;
        case DATATYPE_IMG_RAW_1BPP:
          // Fallthrough
        case DATATYPE_IMG_RAW_2BPP:
          // Fallthrough
        case DATATYPE_IMG_ZLIB:
        {
          DPRINTF("Image indication received:\n");
          DPRINTF("  - Type %02x\n", data_to_process.AP_data.dataType);
          DPRINTF("  - ExtType %02x\n", data_to_process.AP_data.dataTypeArgument);
          DPRINTF("  - Size %ldB\n", data_to_process.AP_data.dataSize);
          DPRINTF("  - Checksum %08lx%08lx\n", (uint32_t)(data_to_process.AP_data.dataVer >> 32), (uint32_t)data_to_process.AP_data.dataVer);

          if(data_to_process.AP_data.dataTypeArgument & IMG_EXTTYPE_LUT_MASK) {
            DPRINTF("Custom LUT support not implemented\n");
            // Don't download, skip to confirmation
            application_state_transition(AWAITING_CONFIRMATION);
            break;
          }

          oepl_stored_image_hdr_t img_meta;
          size_t img_idx;
          oepl_nvm_status_t nvm_status = oepl_nvm_get_image_by_hash(
            data_to_process.AP_data.dataVer, data_to_process.AP_data.dataSize,
            &img_idx, &img_meta);
          if(nvm_status == NVM_SUCCESS) {
            // if regular image, display from cache if it is complete
            if(img_meta.is_valid) {
              DPRINTF("We have this one in cache...\n");
              application_state_transition(AWAITING_CONFIRMATION);
            } else {
              // Something happened to the download of this image... erase and retry in the same slot
              oepl_nvm_erase_image(img_idx);
              img_meta.is_valid = false;
              img_meta.md5 = data_to_process.AP_data.dataVer;
              img_meta.size = data_to_process.AP_data.dataSize;
              img_meta.image_format = data_to_process.AP_data.dataType;
              img_meta.image_type = IMG_EXTTYPE_IMGID_FROM_EXTTYPE( data_to_process.AP_data.dataTypeArgument );
              oepl_nvm_write_image_metadata(img_idx, &img_meta);
              application_state_transition(DOWNLOAD);
            }
          } else {
            // Allocate a slot
            nvm_status = oepl_nvm_get_free_image_slot(&img_idx, IMG_EXTTYPE_IMGID_FROM_EXTTYPE( data_to_process.AP_data.dataTypeArgument ));
            if(nvm_status == NVM_SUCCESS) {
              oepl_stored_image_hdr_t img_meta;
              img_meta.is_valid = false;
              img_meta.md5 = data_to_process.AP_data.dataVer;
              img_meta.size = data_to_process.AP_data.dataSize;
              img_meta.image_format = data_to_process.AP_data.dataType;
              img_meta.image_type = IMG_EXTTYPE_IMGID_FROM_EXTTYPE( data_to_process.AP_data.dataTypeArgument );
              oepl_nvm_write_image_metadata(img_idx, &img_meta);
              application_state_transition(DOWNLOAD);
            } else {
              // We can't do this right now... Confirm and deal with failure afterwards
              DPRINTF("We don't have space for this...\n");
              application_state_transition(AWAITING_CONFIRMATION);
            }
          }
          break;
        }
        case DATATYPE_FW_UPDATE:
        {
          DPRINTF("FWU indicated\n");
          DPRINTF("  - Size %ldB\n", data_to_process.AP_data.dataSize);
          DPRINTF("  - Checksum %08lx%08lx\n", (uint32_t)(data_to_process.AP_data.dataVer >> 32), (uint32_t)data_to_process.AP_data.dataVer);
          // Dup-check before downloading
          if(oepl_fwu_should_download(data_to_process.AP_data.dataVer, (size_t)data_to_process.AP_data.dataSize)) {
            application_state_transition(DOWNLOAD);
          } else {
            DPRINTF("Rejecting upgrade since we're already running this file\n");
            application_state_transition(AWAITING_CONFIRMATION);
          }
          break;
        }
        case DATATYPE_NFC_URL_DIRECT:
          // Fallthrough
        case DATATYPE_NFC_RAW_CONTENT:
        {
          DPRINTF("NFC %s indicated\n", data_to_process.AP_data.dataType == DATATYPE_NFC_URL_DIRECT ? "URL" : "raw");
          oepl_stored_content_version_t stored_ver;
          oepl_nvm_status_t status = oepl_nvm_setting_get(OEPL_NFC_CONTENT_VERSION, &stored_ver, sizeof(stored_ver));
          if(status == NVM_SUCCESS) {
            if(data_to_process.AP_data.dataVer == stored_ver.md5 && data_to_process.AP_data.dataSize == stored_ver.size) {
              DPRINTF("We already have this content in the NFC\n");
              application_state_transition(AWAITING_CONFIRMATION);
            }
          } else {
            application_state_transition(DOWNLOAD);
          }
          break;
        }
        case DATATYPE_TAG_CONFIG_DATA:
        {
          DPRINTF("Tag settings update indicated\n");
          oepl_stored_content_version_t stored_ver;
          oepl_nvm_status_t status = oepl_nvm_setting_get(OEPL_SETTINGS_CONTENT_VERSION, &stored_ver, sizeof(stored_ver));
          if(status == NVM_SUCCESS) {
            if(data_to_process.AP_data.dataVer == stored_ver.md5 && data_to_process.AP_data.dataSize == stored_ver.size) {
              DPRINTF("We already have these settings\n");
              application_state_transition(AWAITING_CONFIRMATION);
            }
          } else {
            application_state_transition(DOWNLOAD);
          }
          break;
        }
        case DATATYPE_COMMAND_DATA:
        {
          DPRINTF("Command 0x%02X received\n", data_to_process.AP_data.dataTypeArgument);
          // ACK it first thing we do, as we might be resetting the tag
          application_state_transition(AWAITING_CONFIRMATION);
          break;
        }
        default:
          DPRINTF("Unsupported datatype indicated\n");
          application_state_transition(CONNECTED);
          break;
      }
      break;
    case AWAITING_CONFIRMATION:
      // Nothing to see here, awaiting a radio state transition which can
      // make us act.
      break;
    case CONFIRMATION_RECEIVED:
      // If this was a command which got postponed, act on it now
      if(data_to_process.AP_data.dataType == DATATYPE_COMMAND_DATA) {
        switch(data_to_process.AP_data.dataTypeArgument) {
          case CMD_DO_REBOOT:
            DPRINTF("Rebooting\n");
            oepl_hw_reboot();
            break;
          case CMD_DO_SCAN:
            DPRINTF("Forced scan (roam) triggered\n");
            oepl_radio_try_roam();
            break;
          case CMD_DO_RESET_SETTINGS:
            oepl_nvm_setting_set_default(OEPL_RAW_TAGSETTINGS);
            DPRINTF("Reset settings, rebooting...\n");
            oepl_hw_reboot();
            break;
          case CMD_DO_DEEPSLEEP:
            DPRINTF("Enter deepsleep\n");
            DPRINTF("To wake, press a button or power cycle\n");
            oepl_display_show_infoscreen(INFOSCREEN_DEEPSLEEP);
            oepl_hw_enter_deepsleep();
            break;
          case CMD_DO_LEDFLASH:
          {
            uint8_t led_data[12];
            led_data[0] = (data_to_process.AP_data.dataVer >>  0) & 0xFF;
            led_data[1] = (data_to_process.AP_data.dataVer >>  8) & 0xFF;
            led_data[2] = (data_to_process.AP_data.dataVer >> 16) & 0xFF;
            led_data[3] = (data_to_process.AP_data.dataVer >> 24) & 0xFF;
            led_data[4] = (data_to_process.AP_data.dataVer >> 32) & 0xFF;
            led_data[5] = (data_to_process.AP_data.dataVer >> 40) & 0xFF;
            led_data[6] = (data_to_process.AP_data.dataVer >> 48) & 0xFF;
            led_data[7] = (data_to_process.AP_data.dataVer >> 56) & 0xFF;
            led_data[8] = (data_to_process.AP_data.dataSize >>  0) & 0xFF;
            led_data[9] = (data_to_process.AP_data.dataSize >>  8) & 0xFF;
            led_data[10] = (data_to_process.AP_data.dataSize >> 16) & 0xFF;
            led_data[11] = (data_to_process.AP_data.dataSize >> 24) & 0xFF;
            if(oepl_led_flash_sequence(led_data)) {
              DPRINTF("LED sequence executing\n");
            } else {
              DPRINTF("LED sequence canceled since previous still in progress\n");
            }
            break;
          }
          case CMD_ERASE_EEPROM_IMAGES:
          {
            DPRINTF("Erase all stored images (and LUTs?)\n");
            size_t num_img, img_size;
            oepl_nvm_status_t nvm_status = oepl_nvm_get_num_img_slots(&num_img, &img_size);
            if(nvm_status == NVM_SUCCESS) {
              for(size_t i = 0; i < num_img; i++) {
                oepl_nvm_erase_image(i);
              }
            }
            break;
          }
          case CMD_ENTER_SLIDESHOW_FAST:
            DPRINTF("Enter fast slideshow mode\n");
            application_mode_transition(MODE_SLIDESHOW_FAST);
            break;
          case CMD_ENTER_SLIDESHOW_MEDIUM:
            DPRINTF("Enter medium slideshow mode\n");
            application_mode_transition(MODE_SLIDESHOW_MEDIUM);
            break;
          case CMD_ENTER_SLIDESHOW_SLOW:
            DPRINTF("Enter slow slideshow mode\n");
            application_mode_transition(MODE_SLIDESHOW_SLOW);
            break;
          case CMD_ENTER_SLIDESHOW_GLACIAL:
            DPRINTF("Enter glacial slideshow mode\n");
            application_mode_transition(MODE_SLIDESHOW_GLACIAL);
            break;
          case CMD_ENTER_NORMAL_MODE:
            DPRINTF("Enter normal (AP-directed) mode\n");
            application_mode_transition(MODE_NORMAL);
            break;
          case CMD_ENTER_WAIT_RFWAKE:
            DPRINTF("Enter deepsleep and wait for RF wake signal\n");
            application_mode_transition(MODE_WAIT_RFWAKE);
            break;
          case CMD_GET_BATTERY_VOLTAGE:
          {
            DPRINTF("Forced battery voltage measurement and trigger a poll\n");
            uint16_t voltage;
            oepl_hw_get_voltage(&voltage, true);
            oepl_radio_send_poll_with_reason(WAKEUP_REASON_TIMED);
            break;
          }
          default:
            DPRINTF("Unknown CMD 0x%02x\n", data_to_process.AP_data.dataTypeArgument);
            break;
        }
      } else if(data_to_process.AP_data.dataType == DATATYPE_FW_UPDATE) {
        size_t highest_block;
        oepl_nvm_status_t status = oepl_fwu_get_highest_block_written(&highest_block);
        if(status == NVM_SUCCESS) {
          size_t file_blocks = data_to_process.AP_data.dataSize / 4096;
          if(file_blocks % 4096) {
            file_blocks++;
          }

          if(highest_block >= file_blocks - 1) {
            status = oepl_fwu_check();
            if(status == NVM_ERROR) {
              DPRINTF("Couldn't validate firmware, signaling failure\n");
              oepl_radio_send_poll_with_reason(WAKEUP_REASON_FAILED_OTA_FW);
              oepl_fwu_erase();
            } else {
              DPRINTF("Applying upgrade...\n");
              oepl_display_show_infoscreen(INFOSCREEN_FWU);
              oepl_fwu_apply();
              oepl_hw_crash(DBG_APP, true, "Failed to apply FWU\n");
            }
          } else {
            DPRINTF("Don't have all FWU blocks, signaling failure\n");
            oepl_radio_send_poll_with_reason(WAKEUP_REASON_FAILED_OTA_FW);
          }
        } else {
          DPRINTF("HW failure, signaling failure\n");
          oepl_radio_send_poll_with_reason(WAKEUP_REASON_FAILED_OTA_FW);
        }
      } else if(data_to_process.AP_data.dataType == DATATYPE_IMG_RAW_1BPP ||
                data_to_process.AP_data.dataType == DATATYPE_IMG_RAW_2BPP ||
                data_to_process.AP_data.dataType == DATATYPE_IMG_ZLIB) {
        // If the AP requested us to show this image on screen, do it now.
        if((data_to_process.AP_data.dataTypeArgument & IMG_EXTTYPE_PRELOAD_FLAG) == 0) {
          oepl_stored_image_hdr_t img_meta;
          size_t img_idx;
          oepl_nvm_status_t nvm_status = oepl_nvm_get_image_by_hash(
            data_to_process.AP_data.dataVer, data_to_process.AP_data.dataSize,
            &img_idx, &img_meta);
          
          if(nvm_status == NVM_SUCCESS && img_meta.is_valid) {
            DPRINTF("Showing image\n");
            oepl_display_show_image(img_idx);
          } else {
            DPRINTF("Confirmed image but couldn't find it in storage. MD5 mismatch or out of space?\n");
          }
        }
        // If the new image we received is not a multi-instance image, remove
        // all previous versions from storage.
        switch(IMG_EXTTYPE_IMGID_FROM_EXTTYPE( data_to_process.AP_data.dataTypeArgument )) {
          case CUSTOM_IMAGE_NOCUSTOM:
            break;
          case CUSTOM_IMAGE_SLIDESHOW:
            break;
          default:
            oepl_nvm_erase_image_cache(IMG_EXTTYPE_IMGID_FROM_EXTTYPE( data_to_process.AP_data.dataTypeArgument ));
            break;
        }
      } else if(data_to_process.AP_data.dataType == DATATYPE_TAG_CONFIG_DATA) {
        // We might have changed modes
        uint8_t new_mode;
        oepl_nvm_setting_get(OEPL_CUSTOM_MODE, &new_mode, sizeof(new_mode));
        application_mode_transition(new_mode);
      }
      application_state_transition(CONNECTED);
      break;
    case DOWNLOAD:
      if(datablock != NULL)
      {
        size_t blocks_in_file = data_to_process.AP_data.dataSize / 4096;
        if(data_to_process.AP_data.dataSize % 4096 != 0) {
          blocks_in_file += 1;
        }
        bool is_last_block = datablock_in_progress.idx == blocks_in_file - 1;
        size_t block_size = is_last_block ? data_to_process.AP_data.dataSize % 4096 : 4096;

        // Which process was this data intended for?
        bool proceed = true;
        switch(data_to_process.AP_data.dataType) {
          case DATATYPE_IMG_RAW_1BPP:
          case DATATYPE_IMG_RAW_2BPP:
          case DATATYPE_IMG_ZLIB:
            DPRINTF("Received %simage block %d\n", is_last_block? "last ": "", datablock_in_progress.idx);
            proceed = application_process_image_block(datablock_in_progress.idx, datablock, block_size, is_last_block);
            break;
          case DATATYPE_FW_UPDATE:
            DPRINTF("Received %sFWU block %d\n", is_last_block? "last ": "", datablock_in_progress.idx);
            proceed = application_process_fwu_block(datablock_in_progress.idx, datablock, block_size, is_last_block);
            break;
          case DATATYPE_TAG_CONFIG_DATA:
            DPRINTF("Received %stag config block %d\n", is_last_block? "last ": "", datablock_in_progress.idx);
            proceed = application_process_config_block(datablock_in_progress.idx, datablock, block_size, is_last_block);
            break;
          case DATATYPE_NFC_URL_DIRECT:
            DPRINTF("Received %sNFC URL block %d\n", is_last_block? "last ": "", datablock_in_progress.idx);
            proceed = application_process_nfcu_block(datablock_in_progress.idx, datablock, block_size, is_last_block);
            break;
          case DATATYPE_NFC_RAW_CONTENT:
            DPRINTF("Received %sNFC raw block %d\n", is_last_block? "last ": "", datablock_in_progress.idx);
            proceed = application_process_nfcr_block(datablock_in_progress.idx, datablock, block_size, is_last_block);
            break;
          default:
            DPRINTF("\n\nERR: received block for unknown datatype\n\n");
            oepl_hw_reboot();
        }

        oepl_radio_release_datablock();

        if(!is_last_block && proceed) {
          datablock_in_progress.idx += 1;
          datablock = NULL;
          oepl_radio_request_datablock(datablock_in_progress);
        } else {
          application_state_transition(AWAITING_CONFIRMATION);
        }
        break;
      }
    case DISCONNECTED:
      // Nothing to see here, waiting for the radio process to tell us we're
      // reconnected.
      break;
  }

  // Handle events which may have happened asynchronously
  if(event_flags != 0) {
    if(event_flags & EVENT_FLAG_BUTTON_1) {
      DPRINTF("Button handler for btn 1\n");
      oepl_radio_send_poll_with_reason(WAKEUP_REASON_BUTTON1);
      if(current_state == CONNECTED) {
        oepl_display_show_infoscreen(INFOSCREEN_WAKEUP_BUTTON1);
      }
      event_flags &= ~EVENT_FLAG_BUTTON_1;
    }
    if(event_flags & EVENT_FLAG_BUTTON_2) {
      DPRINTF("Button handler for btn 2\n");
      oepl_radio_send_poll_with_reason(WAKEUP_REASON_BUTTON2);
      if(current_state == CONNECTED) {
        oepl_display_show_infoscreen(INFOSCREEN_WAKEUP_BUTTON2);
      }
      event_flags &= ~EVENT_FLAG_BUTTON_2;
    }
    if(event_flags & EVENT_FLAG_GPIO) {
      DPRINTF("Button handler for generic GPIO\n");
      oepl_radio_send_poll_with_reason(WAKEUP_REASON_GPIO);
      if(current_state == CONNECTED) {
        oepl_display_show_infoscreen(INFOSCREEN_WAKEUP_GPIO);
      }
      event_flags &= ~EVENT_FLAG_GPIO;
    }
    if(event_flags & EVENT_FLAG_NFC_WAKE) {
      DPRINTF("Button handler for NFC wake pin\n");
      oepl_radio_send_poll_with_reason(WAKEUP_REASON_NFC);
      if(current_state == CONNECTED) {
        oepl_display_show_infoscreen(INFOSCREEN_WAKEUP_NFC);
      }
      event_flags &= ~EVENT_FLAG_NFC_WAKE;
    }
    if(event_flags & EVENT_FLAG_CONNECTED) {
      DPRINTF("Event handler for association succeeded\n");
      oepl_display_set_overlay(ICON_NOT_CONNECTED, false);
      if(!have_seen_ap) {
        have_seen_ap = true;
        oepl_display_show_infoscreen(INFOSCREEN_BOOT_FOUND_AP);
      }
      oepl_display_draw(NULL);
      event_flags &= ~EVENT_FLAG_CONNECTED;
    }
    if(event_flags & EVENT_FLAG_DISCONNECTED) {
      DPRINTF("Event handler for becoming an orphan\n");
      oepl_display_set_overlay(ICON_NOT_CONNECTED, true);
      if(!have_seen_ap) {
        oepl_display_show_infoscreen(INFOSCREEN_LONG_SCAN);
      }
      oepl_display_draw(NULL);
      event_flags &= ~EVENT_FLAG_DISCONNECTED;
    }
  }
}

// -----------------------------------------------------------------------------
//                          Static Function Definitions
// -----------------------------------------------------------------------------
static oepl_radio_action_t radio_event_handler(oepl_radio_event_t event, const void* event_data)
{
  switch(event) {
    case ASSOCIATED:
    {
      DPRINTF("Associated on channel %ld!\n", (uint32_t)event_data);
      uint32_t prev_ch;
      oepl_nvm_setting_get(OEPL_LAST_CONNECTED_CHANNEL, &prev_ch, sizeof(uint32_t));
      if(prev_ch != (uint32_t)event_data) {
        oepl_nvm_setting_set(OEPL_LAST_CONNECTED_CHANNEL, &event_data, sizeof(uint32_t));
      }
      application_state_transition(CONNECTED);
      break;
    }
    case ORPHANED:
      DPRINTF("Orphaned!\n");
      application_state_transition(DISCONNECTED);
      break;
    case AP_DATA:      
      DPRINTF("Data pending!\n");
      application_process_data((oepl_radio_data_indication_t*) event_data);
      break;
    case BLOCK_COMPLETE:
      DPRINTF("Block received!\n");
      application_process_datablock((oepl_radio_blockrecv_t*) event_data);
      break;
    case BLOCK_TIMEOUT:
      DPRINTF("Block timed out\n");
      // Since retries are already built into the radio logic, we have to
      // assume this download transaction is lost.

      // Go back to connected, we'll orphan after a couple more check-ins
      // have failed.
      application_state_transition(CONNECTED);
      break;
    case BLOCK_CANCELED:
      DPRINTF("AP canceled transfer\n");
      //Todo: How to handle this? Erase the in-flight storage and forget
      // that this data transfer ever existed?
      application_state_transition(CONNECTED);
      break;
    case SCAN_TIMEOUT:
      DPRINTF("Scan timeout!\n");
      if(current_state == BOOT) {
        application_state_transition(DISCONNECTED);
      } else if(current_state != DISCONNECTED) {
        DPRINTF("Scan timeout while connected - we'll get orphaned if this continues\n");
      }
      break;
    case CHANNEL_ROAM:
    {
      DPRINTF("Switched channel to %ld!\n", (uint32_t) event_data);
      // This doesn't impact the application at all, but we update the stored
      // channel number for fast-associate after reboot
      uint32_t prev_ch;
      oepl_nvm_setting_get(OEPL_LAST_CONNECTED_CHANNEL, &prev_ch, sizeof(uint32_t));
      if(prev_ch != (uint32_t) event_data) {
        oepl_nvm_setting_set(OEPL_LAST_CONNECTED_CHANNEL, &event_data, sizeof(uint32_t));
      }
      break;
    }
    case POLL_TIMEOUT:
      DPRINTF("Timed out on a poll - radio process should take care of it\n");
      break;
    case CONFIRMATION_COMPLETE:
      DPRINTF("Received confirmation ACK\n");
      application_state_transition(CONFIRMATION_RECEIVED);
      break;
    case CONFIRMATION_TIMEOUT:
      // Treat a timeout on ACK'ing the confirmation the same as receiving
      // the confirmation. We'll possibly get the same data notification if
      // the confirmation really didn't come through, but dup checking should
      // avoid us from getting stuck in a loop of downloading and not being
      // able to confirm.
      DPRINTF("Did not receive confirmation ACK\n");
      application_state_transition(CONFIRMATION_RECEIVED);
      break;
  }
  return NO_ACTION;
}

static void application_state_transition(application_state_t new_state)
{
  // same-state transition should be invalid...
  if(new_state == current_state) {
    DPRINTF("\n\nERR: Same state transition not allowed\n\n");
    oepl_hw_reboot();
    return;
  }

  // Perform housekeeping on state transition
  switch(new_state) {
    case CONNECTED:
      if(current_state == DISCONNECTED ||
         current_state == BOOT) {
        // Tell the event loop to redraw the EPD when changing from disconnected to connected
        event_flags |= EVENT_FLAG_CONNECTED;
      }
      break;
    case DISCONNECTED:
      if(current_state == CONNECTED ||
         current_state == BOOT) {
        // Tell the event loop to redraw the EPD when changing from connected to disconnected
        event_flags |= EVENT_FLAG_DISCONNECTED;
      }
      break;
    case AWAITING_CONFIRMATION:
      // Progressing to awaiting a confirmation always starts with sending a confirmation
      oepl_radio_acknowledge_action(data_to_process.AP_MAC, data_to_process.AP_PAN);
      break;
    case DOWNLOAD:
      if(current_state != DATA_AVAILABLE) {
        DPRINTF("\n\nERR: Invalid state transition: download without being told to\n\n");
        oepl_hw_reboot();
      }
      // Progressing to download state means resetting the download logic
      DPRINTF("Starting download of %ld bytes\n", data_to_process.AP_data.dataSize);
      datablock_in_progress.file.type = data_to_process.AP_data.dataType;
      memcpy(&datablock_in_progress.file.id, &data_to_process.AP_data.dataVer, sizeof(uint64_t));
      memcpy(datablock_in_progress.file.ap, data_to_process.AP_MAC, sizeof(datablock_in_progress.file.ap));
      datablock_in_progress.file.ap_pan = data_to_process.AP_PAN;
      datablock_in_progress.file.filesize = data_to_process.AP_data.dataSize;

      // Check for partial resume support
      switch(data_to_process.AP_data.dataType) {
        case DATATYPE_FW_UPDATE:
        {
          uint64_t staged_md5;
          size_t staged_size;
          uint16_t staged_version;
          oepl_nvm_status_t status = oepl_fwu_get_metadata(&staged_version, &staged_md5, &staged_size);
          if(status == NVM_NOT_FOUND) {
            status = oepl_fwu_set_metadata(0xFFFF, data_to_process.AP_data.dataVer, data_to_process.AP_data.dataSize);
            if(status == NVM_SUCCESS) {
              DPRINTF("Starting FWU from scratch\n");
              datablock_in_progress.idx = 0;
            } else {
              // we can't do this right now.
              DPRINTF("Failed to set upgrade meta\n");
              oepl_radio_acknowledge_action(data_to_process.AP_MAC, data_to_process.AP_PAN);
              new_state = AWAITING_CONFIRMATION;
            }
          } else if(status == NVM_ERROR) {
            // we can't do this right now.
            DPRINTF("FWU internal error, can't start\n");
            oepl_radio_acknowledge_action(data_to_process.AP_MAC, data_to_process.AP_PAN);
            new_state = AWAITING_CONFIRMATION;
          } else {
            if(staged_size == data_to_process.AP_data.dataSize && staged_md5 == data_to_process.AP_data.dataVer) {
              status = oepl_fwu_get_highest_block_written(&datablock_in_progress.idx);
              if(status != NVM_SUCCESS) {
                DPRINTF("Starting FWU, unsure about the blocks written\n");
                datablock_in_progress.idx = 0;
              } else if(datablock_in_progress.idx == 0) {
                // Didn't get further than the first block or didn't start writing at all.
                DPRINTF("Restarting FWU\n");
              } else {
                size_t blocks_in_file = staged_size / 4096;
                if(staged_size % 4096) {
                  blocks_in_file++;
                }
                if(datablock_in_progress.idx >= blocks_in_file - 1) {
                  // Our previous process succeeded, go to confirmation and retrigger upgrade
                  DPRINTF("Already received this file fully\n");
                  oepl_radio_acknowledge_action(data_to_process.AP_MAC, data_to_process.AP_PAN);
                  new_state = AWAITING_CONFIRMATION;
                } else {
                  // Continue with the next block
                  datablock_in_progress.idx++;
                  DPRINTF("Resuming FWU at block %d\n", datablock_in_progress.idx);
                }
              }
            } else {
              // Receiving a different file, need to start anew
              status = oepl_fwu_set_metadata(0xFFFF, data_to_process.AP_data.dataVer, data_to_process.AP_data.dataSize);
              if(status == NVM_SUCCESS) {
                DPRINTF("Reset FWU metadata to receive new upgrade file\n");
                datablock_in_progress.idx = 0;
              } else {
                // we can't do this right now.
                DPRINTF("Couldn't reset metadata\n");
                oepl_radio_acknowledge_action(data_to_process.AP_MAC, data_to_process.AP_PAN);
                new_state = AWAITING_CONFIRMATION;
              }
            }
          }
          break;
        }
        default:
          datablock_in_progress.idx = 0;
          break;
      }

      // Start the download if we still want to proceed
      if(new_state == DOWNLOAD) {
        datablock = NULL;
        oepl_radio_request_datablock(datablock_in_progress);
      }
      break;
    default:
      break;
  }
  current_state = new_state;
}

static void application_process_data(oepl_radio_data_indication_t* data)
{
  // Process it in the main loop to not make our stack depth go bonkers
  if(current_state == CONNECTED || current_state == BOOT || current_state == DISCONNECTED) {
    memcpy(&data_to_process, data, sizeof(*data));
    application_state_transition(DATA_AVAILABLE);
  } else {
    // We received a poll response while still processing the previous 
    // one or while not in a connected state. If we really have been awake
    // for at least 40 seconds, something must be wrong.
    // Treat this as an invalid transition and reset the system.
    DPRINTF("\n\nERR: Data indication received while still handling previous data indication\n\n");
    oepl_hw_reboot();
  }
}

static void application_process_datablock(oepl_radio_blockrecv_t* block)
{
  // Called from the radio callback, so we'll have at least one round in the application
  // event loop after this.
  if(current_state != DOWNLOAD) {
    DPRINTF("\n\nERR: received datablock but not in download state\n\n");
    oepl_hw_reboot();
  }

  if(block->block_index != datablock_in_progress.idx) {
    DPRINTF("\n\nERR: received out-of-order datablock\n\n");
    oepl_hw_reboot();
  }

  if(datablock != NULL) {
    DPRINTF("\n\nERR: not finished with the previous datablock\n\n");
    oepl_hw_reboot();
  }

  datablock = block->block_data;
}

static bool application_process_image_block(size_t index, const uint8_t* data, size_t length, bool is_last)
{
  oepl_stored_image_hdr_t img_meta;
  size_t img_idx;
  oepl_nvm_status_t nvm_status = oepl_nvm_get_image_by_hash(
    data_to_process.AP_data.dataVer, data_to_process.AP_data.dataSize,
    &img_idx, &img_meta);
  if(nvm_status == NVM_SUCCESS) {
    nvm_status = oepl_nvm_write_image_bytes(img_idx, index * 4096, data, length);
  } else {
    DPRINTF("App error: couldn't get image metadata for download in progress\n");
    return false;
  }

  if(nvm_status != NVM_SUCCESS) {
    DPRINTF("Couldn't write image bytes into slot, erasing full slot\n");
    oepl_nvm_erase_image(img_idx);
    return false;
  }

  if(is_last) {
    MD5Context md5;
    md5Init(&md5);

    size_t img_blocks = img_meta.size / 4096;
    if(img_meta.size % 4096) {
      img_blocks++;
    }

    for(size_t i = 0; i < img_blocks; i++) {
      size_t this_block_size = i == img_blocks - 1 ? img_meta.size % 4096 : 4096;
      //Todo: do something about this cast?
      // -> We know we're using the radio buffer here which is mutable...
      memset(data, 0x12, this_block_size);
      nvm_status = oepl_nvm_read_image_bytes(img_idx, i*4096, (uint8_t*)data, this_block_size);
      if(nvm_status != NVM_SUCCESS) {
        DPRINTF("Error reading image data from NVM\n");
        return false;
      }
      DPRINTF("Feeding MD5 %d bytes\n", this_block_size);
      md5Update(&md5, data, this_block_size);
    }
    md5Finalize(&md5);
    if(memcmp(&md5.digest[0], &img_meta.md5, sizeof(img_meta.md5)) == 0) {
      // Mark image download as valid
      DPRINTF("Image MD5 checks out\n");
      img_meta.is_valid = true;
      oepl_nvm_write_image_metadata(img_idx, &img_meta);
      return false;
    } else {
      // Erase image download
      DPRINTF("MD5 mismatch on image download, erasing\n");
      oepl_nvm_erase_image(img_idx);
      return false;
    }
  } else {
    return true;
  }
}

static bool application_process_fwu_block(size_t index, const uint8_t* data, size_t length, bool is_last)
{
  (void) is_last;
  oepl_nvm_status_t status = oepl_fwu_write(index, data, length);
  if(status != NVM_SUCCESS) {
    // We can't proceed right now...
    return false;
  }

  return true;
}

static bool application_check_md5(const uint8_t* data, size_t bytes, const uint8_t* reference)
{
  MD5Context md5;
  md5Init(&md5);
  md5Update(&md5, data, bytes);
  md5Finalize(&md5);
  if(memcmp(&md5.digest[0], reference, 8) == 0) {
    return true;
  } else {
    DPRINTF("MD5 mismatch on %ld bytes:\n", bytes);
    DPRINTF("- Expected: %02X%02X%02X%02X%02X%02X%02X%02X\n", reference[0], reference[1], reference[2], reference[3],
                                                              reference[4], reference[5], reference[6], reference[7]);
    DPRINTF("- Computed: %02X%02X%02X%02X%02X%02X%02X%02X\n", md5.digest[0], md5.digest[1], md5.digest[2], md5.digest[3],
                                                              md5.digest[4], md5.digest[5], md5.digest[6], md5.digest[7]);
    DPRINTF("- Data:\n\t");
    for(size_t i = 0; i < bytes; i++) {
      DPRINTF("%02X", data[i]);
      if(i % 16 == 0 && i != 0) {
        DPRINTF("\n\t");
      }
    }
    DPRINTF("\n");
    return false;
  }
}

static bool application_process_config_block(size_t index, const uint8_t* data, size_t length, bool is_last)
{
  if(index == 0 && is_last) {
    if(length != sizeof(struct tagsettings)) {
      DPRINTF("Don't know how to parse these settings, size mismatch\n");
    }

    // Check MD5 before storing
    if(!application_check_md5(data, length, (const uint8_t*)&data_to_process.AP_data.dataVer)) {
      DPRINTF("MD5 mismatch\n");
      return false;
    }

    oepl_nvm_setting_set(OEPL_RAW_TAGSETTINGS, data, sizeof(struct tagsettings));
    oepl_stored_content_version_t stored_ver = {
      .md5 = data_to_process.AP_data.dataVer,
      .size = data_to_process.AP_data.dataSize
    };
    oepl_nvm_setting_set(OEPL_SETTINGS_CONTENT_VERSION, &stored_ver, sizeof(stored_ver));
    return false;
  } else {
    DPRINTF("Usage error: can't support tag settings larger than one block\n");
    return false;
  }
}

static bool application_process_nfcu_block(size_t index, const uint8_t* data, size_t length, bool is_last)
{
  if(index == 0 && is_last) {
    // Check MD5 before storing
    if(!application_check_md5(data, length, (const uint8_t*)&data_to_process.AP_data.dataVer)) {
      DPRINTF("MD5 mismatch\n");
      return false;
    }

    oepl_hw_nfc_write_url(data, length);
    oepl_stored_content_version_t stored_ver = {
      .md5 = data_to_process.AP_data.dataVer,
      .size = data_to_process.AP_data.dataSize
    };
    oepl_nvm_setting_set(OEPL_NFC_CONTENT_VERSION, &stored_ver, sizeof(stored_ver));
    return false;
  } else {
    DPRINTF("Usage error: can't support NFC URLs larger than one block\n");
    return false;
  }
}

static bool application_process_nfcr_block(size_t index, const uint8_t* data, size_t length, bool is_last)
{
  if(index == 0 && is_last) {
    // Check MD5 before storing
    if(!application_check_md5(data, length, (const uint8_t*)&data_to_process.AP_data.dataVer)) {
      DPRINTF("MD5 mismatch\n");
      return false;
    }

    oepl_hw_nfc_write_raw(data, length);
    oepl_stored_content_version_t stored_ver = {
      .md5 = data_to_process.AP_data.dataVer,
      .size = data_to_process.AP_data.dataSize
    };
    oepl_nvm_setting_set(OEPL_NFC_CONTENT_VERSION, &stored_ver, sizeof(stored_ver));
    return false;
  } else {
    DPRINTF("Usage error: can't support NFC data content larger than one block\n");
    return false;
  }
}

static void oepl_app_button_handler(oepl_hw_gpio_channel_t button, oepl_hw_gpio_event_t event)
{
  (void) event;
  switch(button) {
    case BUTTON_1:
      event_flags |= EVENT_FLAG_BUTTON_1;
      break;
    case BUTTON_2:
      event_flags |= EVENT_FLAG_BUTTON_2;
      break;
    case GENERIC_GPIO:
      event_flags |= EVENT_FLAG_GPIO;
      break;
    case NFC_WAKE:
      event_flags |= EVENT_FLAG_NFC_WAKE;
      break;
  }
}

static void application_mode_transition(application_mode_t new_mode)
{
  // Todo: implement support for running modes / slideshow
  (void) new_mode;
  DPRINTF("Custom modes are not supported yet\n");
}
