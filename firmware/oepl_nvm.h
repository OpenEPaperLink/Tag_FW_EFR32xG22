#ifndef OEPL_NVM_H
#define OEPL_NVM_H

// -----------------------------------------------------------------------------
//                                   Includes
// -----------------------------------------------------------------------------
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// -----------------------------------------------------------------------------
//                              Macros and Typedefs
// -----------------------------------------------------------------------------
#ifndef __packed
#define __packed __attribute__((packed))
#endif

typedef enum {
  OEPL_RAW_TAGSETTINGS,
  OEPL_ENABLE_FASTBOOT,
  OEPL_ENABLE_RFWAKE,
  OEPL_ENABLE_TAGROAMING,
  OEPL_ENABLE_AUTOSCAN_ON_ORPHAN,
  OEPL_ENABLE_LOWBAT_ICON,
  OEPL_ENABLE_NORF_ICON,
  OEPL_STORED_CAPABILITIES,
  OEPL_CUSTOM_MODE,
  OEPL_LOWBAT_VOLTAGE_MV,
  OEPL_MIN_CHECKIN_INTERVAL_S,
  OEPL_FIXED_CHANNEL,
  OEPL_HWID,
  OEPL_LAST_CONNECTED_CHANNEL,
  OEPL_NFC_CONTENT_VERSION,
  OEPL_SETTINGS_CONTENT_VERSION,
  OEPL_CURRENT_MODE
} oepl_setting_entry_t;

typedef enum {
  NVM_SUCCESS,
  NVM_NOT_FOUND,
  NVM_OUT_OF_MEMORY,
  NVM_ERROR,
  NVM_NOT_SUPPORTED
} oepl_nvm_status_t;

typedef struct __packed {				
	uint64_t md5;
	uint32_t size;
  size_t seqno;
	uint8_t image_format;
	uint8_t image_type;
  bool is_valid;
} oepl_stored_image_hdr_t;

typedef struct __packed {
  uint64_t md5;
  uint32_t size;
} oepl_stored_content_version_t;

typedef struct __packed tagsettings oepl_stored_tagsettings_t;
extern const oepl_stored_tagsettings_t oepl_default_tagsettings;

// -----------------------------------------------------------------------------
//                                Global Variables
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
//                          Public Function Declarations
// -----------------------------------------------------------------------------

// ------------------------ Base NVM Layer functionality -----------------------
oepl_nvm_status_t oepl_nvm_init_default(void);
oepl_nvm_status_t oepl_nvm_factory_reset(uint8_t hwid);
oepl_nvm_status_t oepl_nvm_setting_set(oepl_setting_entry_t entry, const void* data, size_t length);
oepl_nvm_status_t oepl_nvm_setting_get(oepl_setting_entry_t entry, void* data, size_t length);
oepl_nvm_status_t oepl_nvm_setting_delete(oepl_setting_entry_t entry);
oepl_nvm_status_t oepl_nvm_setting_set_default(oepl_setting_entry_t entry);

// ------------------------ Image storage functionality ------------------------
oepl_nvm_status_t oepl_nvm_get_num_img_slots(size_t *num_slots, size_t* slot_size);
oepl_nvm_status_t oepl_nvm_get_image_raw_address(size_t img_idx, uint32_t* address);
oepl_nvm_status_t oepl_nvm_get_image_by_hash(uint64_t md5, uint32_t size, size_t* img_idx, oepl_stored_image_hdr_t* metadata);
oepl_nvm_status_t oepl_nvm_get_image_by_type(uint8_t image_type, size_t* img_idx, size_t* seqno);
oepl_nvm_status_t oepl_nvm_get_free_image_slot(size_t* img_idx, uint8_t image_type);
oepl_nvm_status_t oepl_nvm_erase_image(size_t img_idx);
oepl_nvm_status_t oepl_nvm_erase_image_cache(uint8_t image_type);
oepl_nvm_status_t oepl_nvm_write_image_metadata(size_t img_idx, oepl_stored_image_hdr_t* metadata);
oepl_nvm_status_t oepl_nvm_read_image_metadata(size_t img_idx, oepl_stored_image_hdr_t* metadata);
oepl_nvm_status_t oepl_nvm_write_image_bytes(size_t img_idx, size_t offset, const uint8_t* bytes, size_t length);
oepl_nvm_status_t oepl_nvm_read_image_bytes(size_t img_idx, size_t offset, uint8_t* bytes, size_t length);

// ------------------------ OTA upgrade functionality --------------------------
oepl_nvm_status_t oepl_fwu_erase(void);
oepl_nvm_status_t oepl_fwu_set_metadata(uint16_t new_version, uint64_t file_md5, size_t file_size);
oepl_nvm_status_t oepl_fwu_get_metadata(uint16_t* new_version, uint64_t* file_md5, size_t* file_size);
oepl_nvm_status_t oepl_fwu_get_highest_block_written(size_t* block_idx);
oepl_nvm_status_t oepl_fwu_write(size_t block_idx, const uint8_t bytes[4096], size_t actual_length);
oepl_nvm_status_t oepl_fwu_check(void);
oepl_nvm_status_t oepl_fwu_apply(void);
bool oepl_fwu_is_upgraded(void);
bool oepl_fwu_should_download(uint64_t update_md5, size_t update_filesize);
oepl_nvm_status_t oepl_fwu_confirm_upgrade(void);

#endif