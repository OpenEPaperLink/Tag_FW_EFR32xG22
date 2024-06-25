// -----------------------------------------------------------------------------
//                                   Includes
// -----------------------------------------------------------------------------
#include "em_device.h"
#include "oepl_nvm.h"
#include "oepl_hw_abstraction.h"
#include "oepl_flash_driver.h"

#include "oepl-proto.h"
#include "nvm3.h"
#include "nvm3_default.h"
#include "btl_interface.h"
#include "btl_interface_storage.h"
#include "md5.h"
#include <stdio.h>
#include "oepl-definitions.h"

// -----------------------------------------------------------------------------
//                              Configuration values
// -----------------------------------------------------------------------------
#ifndef NVM_DEBUG_PRINT
#define NVM_DEBUG_PRINT 1
#endif

#define NVM3_OBJECT_ID_CONFIG               0x0000
#define NVM3_OBJECT_ID_SETTINGS_START       0x0001
#define NVM3_OBJECT_ID_SETTINGS_MAX         0x0100

#define NVM3_OBJECT_ID_FWU_METADATA         0x1000
#define NVM3_OBJECT_ID_FWU_BLOCK_COUNTER    0x1001
#define NVM3_OBJECT_ID_IMAGE_METADATA_BASE  0x2000
#define NVM3_OBJECT_ID_IMAGE_METADATA_MAX   0x2010

#define NVM3_MARKER_VALUE                   0xCAFEFACEUL

// -----------------------------------------------------------------------------
//                              Macros and Typedefs
// -----------------------------------------------------------------------------
#if NVM_DEBUG_PRINT
#define DPRINTF(fmt_, ...) oepl_hw_debugprint(DBG_NVM, (fmt_), ##__VA_ARGS__)
#else
#define DPRINTF(...)
#endif

typedef struct {
  uint32_t marker;
  uint8_t hwid;
  size_t fwu_slot_size;
  uint32_t bulk_storage_base_address;
  size_t bulk_storage_size;
  size_t bulk_storage_pagesize;
} device_hw_config_t;

typedef struct {
  uint64_t current_md5;
  uint64_t staged_md5;
  uint16_t current_version;
  uint16_t staged_version;
  size_t current_size;
  size_t staged_size;
} device_fwu_meta_t;

// -----------------------------------------------------------------------------
//                          Static Function Declarations
// -----------------------------------------------------------------------------
static oepl_nvm_status_t check_fwu_md5(void);

// -----------------------------------------------------------------------------
//                                Global Variables
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
//                                Static Variables
// -----------------------------------------------------------------------------
static device_hw_config_t devconfig;
const oepl_stored_tagsettings_t oepl_default_tagsettings = {
  .settingsVer = 1,                  // the version of the struct as written to the infopage
  .enableFastBoot = 0,               // default 0; if set, it will skip splashscreen
  .enableRFWake = 0,                 // default 0; if set, it will enable RF wake. This will add about ~0.9µA idle power consumption
  .enableTagRoaming = 0,             // default 0; if set, the tag will scan for an accesspoint every few check-ins. This will increase power consumption quite a bit
  .enableScanForAPAfterTimeout = 1,  // default 1; if a the tag failed to check in, after a few attempts it will try to find a an AP on other channels
  .enableLowBatSymbol = 1,           // default 1; tag will show 'low battery' icon on screen if the battery is depleted
  .enableNoRFSymbol = 1,             // default 1; tag will show 'no signal' icon on screen if it failed to check in for a longer period of time
  .fastBootCapabilities = 0,         // holds the byte with 'capabilities' as detected during a normal tag boot; allows the tag to skip detecting buttons and NFC chip
  .customMode = 0,                   // default 0; if anything else, tag will bootup in a different 'mode'
  .batLowVoltage = 2600,               // Low battery threshold voltage (2450 for 2.45v). defaults to BATTERY_VOLTAGE_MINIMUM from powermgt.h
  .minimumCheckInTime = 40,          // defaults to BASE_INTERVAL from powermgt.h
  .fixedChannel = 0,                 // default 0; if set to a valid channel number, the tag will stick to that channel
};

static oepl_stored_tagsettings_t tag_settings;

// -----------------------------------------------------------------------------
//                          Public Function Definitions
// -----------------------------------------------------------------------------

// ------------------------ Base NVM Layer functionality -----------------------
oepl_nvm_status_t oepl_nvm_init_default(void)
{
  // Bugfix: deinit the bootloader explicitly since it might not have realized
  bootloader_deinit();
  Ecode_t nvm_status = nvm3_readData(nvm3_defaultHandle, NVM3_OBJECT_ID_CONFIG, &devconfig, sizeof(devconfig));
  if(nvm_status != ECODE_OK) {
    DPRINTF("Unable to read/find device initial settings, %08lX\n", nvm_status);
    return NVM_ERROR;
  }
  if(devconfig.marker != NVM3_MARKER_VALUE) {
    DPRINTF("Wrong version of device initial settings\n");
    return NVM_ERROR;
  }
  if(devconfig.bulk_storage_size < 4096) {
    DPRINTF("No flash registered, rerunning autodetect\n");
    nvm3_deleteObject(nvm3_defaultHandle, NVM3_OBJECT_ID_CONFIG);
    return NVM_ERROR;
  }

  return NVM_SUCCESS;
}

oepl_nvm_status_t oepl_nvm_factory_reset(uint8_t hwid)
{
  oepl_nvm_status_t function_status = NVM_ERROR;
  Ecode_t nvm_status = nvm3_eraseAll(nvm3_defaultHandle);
  if(nvm_status != ECODE_OK) {
    DPRINTF("Failed resetting to factory\n");
    return NVM_ERROR;
  }

  devconfig.marker = NVM3_MARKER_VALUE;
  devconfig.hwid = hwid;
  
  // Autodetect FWU and storage size through the bootloader flash driver
  int32_t status;
  BootloaderStorageSlot_t slotInfo;
  BootloaderStorageInformation_t flashInfo;

  oepl_hw_flash_wake();
  if((status = bootloader_init()) != BOOTLOADER_OK)
  {
    DPRINTF("Failed BTL init with %08lx\n", status);
    goto exit;
  }

  bootloader_getStorageInfo(&flashInfo);
  if(flashInfo.numStorageSlots > 1) {
    DPRINTF("Bootloader with multiple slots is not supported (yet)\n");
    status = BOOTLOADER_ERROR_INIT_STORAGE;
    goto done;
  }

  if((status = bootloader_getStorageSlotInfo(0, &slotInfo)) != BOOTLOADER_OK)
  {
    DPRINTF("Failed BTL slot info with %08lx\n", status);
    goto done;
  }

  DPRINTF("Detected bootloader slot size %ldB\n", slotInfo.length);
  DPRINTF("Detected raw flash with size %ldB, page size %ldB\n", flashInfo.flashInfo.partSize, flashInfo.flashInfo.pageSize);
  DPRINTF("Wordsize %d, page erase %ldms, part erase %ldms\n", flashInfo.flashInfo.wordSizeBytes, flashInfo.flashInfo.pageEraseMs, flashInfo.flashInfo.partEraseMs);

  devconfig.fwu_slot_size = slotInfo.length;
  devconfig.bulk_storage_size = flashInfo.flashInfo.partSize - slotInfo.length - slotInfo.address;
  devconfig.bulk_storage_pagesize = flashInfo.flashInfo.pageSize;
  devconfig.bulk_storage_base_address = slotInfo.address + slotInfo.length;

  DPRINTF("Raw flash content before erase:\n");
  for(size_t i = 0; i < flashInfo.flashInfo.partSize / 32; i+=32) {
    uint8_t buffer[32];
    bootloader_readRawStorage(i, buffer, 32);
    for(size_t j = 0; j < 32; j++) {
      DPRINTF("%02x", buffer[j]);
      if(j > 0 && j % 8 == 0) {
        DPRINTF(" ");
      }
    }
    DPRINTF("\n");
  }

  // Erase the storage
  status = bootloader_eraseRawStorage(slotInfo.address, flashInfo.flashInfo.partSize);
  if(status != BOOTLOADER_OK) {
    DPRINTF("Failed flash device erase with %08lx\n", status);
  }

  nvm3_writeData(nvm3_defaultHandle, NVM3_OBJECT_ID_CONFIG, &devconfig, sizeof(devconfig));
  DPRINTF("Stored new devconfig\n");

  nvm3_writeData(nvm3_defaultHandle, NVM3_OBJECT_ID_SETTINGS_START + OEPL_RAW_TAGSETTINGS, &oepl_default_tagsettings, sizeof(oepl_default_tagsettings));
  DPRINTF("Stored default tagconfig\n");

done:
  if(status == BOOTLOADER_OK) {
    function_status = NVM_SUCCESS;
  }

  if((status = bootloader_deinit()) != BOOTLOADER_OK)
  {
    DPRINTF("Failed BTL deinit with %08lx\n", status);
    function_status = NVM_ERROR;
  }
exit:
  oepl_hw_flash_deepsleep();
  return function_status;
}

oepl_nvm_status_t oepl_nvm_setting_set(oepl_setting_entry_t entry, const void* data, size_t length)
{
  Ecode_t status;
  switch(entry) {
    case OEPL_HWID:
      if(length != sizeof(devconfig.hwid)) {
        return NVM_ERROR;
      }
      devconfig.hwid = *(uint8_t*)data;
      status = nvm3_writeData(nvm3_defaultHandle, NVM3_OBJECT_ID_CONFIG, &devconfig, sizeof(devconfig));
      return status == ECODE_NVM3_OK ? NVM_SUCCESS : NVM_ERROR;
    case OEPL_RAW_TAGSETTINGS:
      if(length != sizeof(tag_settings)) {
        return NVM_ERROR;
      }
      memcpy(&tag_settings, data, length);
      break;
    case OEPL_ENABLE_FASTBOOT:
      if(length != sizeof(tag_settings.enableFastBoot)) {
        return NVM_ERROR;
      }
      tag_settings.enableFastBoot = *((uint8_t*)data);
      entry = OEPL_RAW_TAGSETTINGS;
      data = &tag_settings;
      length = sizeof(tag_settings);
      break;
    case OEPL_ENABLE_RFWAKE:
      if(length != sizeof(tag_settings.enableRFWake)) {
        return NVM_ERROR;
      }
      tag_settings.enableRFWake = *((uint8_t*)data);
      entry = OEPL_RAW_TAGSETTINGS;
      data = &tag_settings;
      length = sizeof(tag_settings);
      break;
    case OEPL_ENABLE_TAGROAMING:
      if(length != sizeof(tag_settings.enableTagRoaming)) {
        return NVM_ERROR;
      }
      tag_settings.enableTagRoaming = *((uint8_t*)data);
      entry = OEPL_RAW_TAGSETTINGS;
      data = &tag_settings;
      length = sizeof(tag_settings);
      break;
    case OEPL_ENABLE_AUTOSCAN_ON_ORPHAN:
      if(length != sizeof(tag_settings.enableScanForAPAfterTimeout)) {
        return NVM_ERROR;
      }
      tag_settings.enableScanForAPAfterTimeout = *((uint8_t*)data);
      entry = OEPL_RAW_TAGSETTINGS;
      data = &tag_settings;
      length = sizeof(tag_settings);
      break;
    case OEPL_ENABLE_LOWBAT_ICON:
      if(length != sizeof(tag_settings.enableLowBatSymbol)) {
        return NVM_ERROR;
      }
      tag_settings.enableLowBatSymbol = *((uint8_t*)data);
      entry = OEPL_RAW_TAGSETTINGS;
      data = &tag_settings;
      length = sizeof(tag_settings);
      break;
    case OEPL_ENABLE_NORF_ICON:
      if(length != sizeof(tag_settings.enableNoRFSymbol)) {
        return NVM_ERROR;
      }
      tag_settings.enableNoRFSymbol = *((uint8_t*)data);
      entry = OEPL_RAW_TAGSETTINGS;
      data = &tag_settings;
      length = sizeof(tag_settings);
      break;
    case OEPL_STORED_CAPABILITIES:
      if(length != sizeof(tag_settings.fastBootCapabilities)) {
        return NVM_ERROR;
      }
      tag_settings.fastBootCapabilities = *((uint8_t*)data);
      entry = OEPL_RAW_TAGSETTINGS;
      data = &tag_settings;
      length = sizeof(tag_settings);
      break;
    case OEPL_CUSTOM_MODE:
      if(length != sizeof(tag_settings.customMode)) {
        return NVM_ERROR;
      }
      tag_settings.customMode = *((uint8_t*)data);
      entry = OEPL_RAW_TAGSETTINGS;
      data = &tag_settings;
      length = sizeof(tag_settings);
      break;
    case OEPL_LOWBAT_VOLTAGE_MV:
      if(length != sizeof(tag_settings.batLowVoltage)) {
        return NVM_ERROR;
      }
      memcpy(&tag_settings.batLowVoltage, data, sizeof(tag_settings.batLowVoltage));
      entry = OEPL_RAW_TAGSETTINGS;
      data = &tag_settings;
      length = sizeof(tag_settings);
      break;
    case OEPL_MIN_CHECKIN_INTERVAL_S:
      if(length != sizeof(tag_settings.minimumCheckInTime)) {
        return NVM_ERROR;
      }
      memcpy(&tag_settings.minimumCheckInTime, data, sizeof(tag_settings.minimumCheckInTime));
      entry = OEPL_RAW_TAGSETTINGS;
      data = &tag_settings;
      length = sizeof(tag_settings);
      break;
    case OEPL_FIXED_CHANNEL:
      if(length != sizeof(tag_settings.fixedChannel)) {
        return NVM_ERROR;
      }
      tag_settings.fixedChannel = *((uint8_t*)data);
      entry = OEPL_RAW_TAGSETTINGS;
      data = &tag_settings;
      length = sizeof(tag_settings);
      break;
    default:
     // No special handling needed
      break;
  }

  status = nvm3_writeData(nvm3_defaultHandle, NVM3_OBJECT_ID_SETTINGS_START + (size_t)entry, data, length);
  if(status == ECODE_NVM3_OK) {
    return NVM_SUCCESS;
  } else {
    return NVM_ERROR;
  }
}

oepl_nvm_status_t oepl_nvm_setting_get(oepl_setting_entry_t entry, void* data, size_t length)
{
  switch(entry) {
    case OEPL_HWID:
      if(length < sizeof(devconfig.hwid)) {
        return NVM_ERROR;
      }
      *(uint8_t*)data = devconfig.hwid;
      return NVM_SUCCESS;
    case OEPL_ENABLE_FASTBOOT:
      if(length < sizeof(tag_settings.enableFastBoot)) {
        return NVM_ERROR;
      }
      *(uint8_t*)data = tag_settings.enableFastBoot;
      return NVM_SUCCESS;
    case OEPL_ENABLE_RFWAKE:
      if(length < sizeof(tag_settings.enableRFWake)) {
        return NVM_ERROR;
      }
      *(uint8_t*)data = tag_settings.enableRFWake;
      return NVM_SUCCESS;
    case OEPL_ENABLE_TAGROAMING:
      if(length < sizeof(tag_settings.enableTagRoaming)) {
        return NVM_ERROR;
      }
      *(uint8_t*)data = tag_settings.enableTagRoaming;
      return NVM_SUCCESS;
    case OEPL_ENABLE_AUTOSCAN_ON_ORPHAN:
      if(length < sizeof(tag_settings.enableScanForAPAfterTimeout)) {
        return NVM_ERROR;
      }
      *(uint8_t*)data = tag_settings.enableScanForAPAfterTimeout;
      return NVM_SUCCESS;
    case OEPL_ENABLE_LOWBAT_ICON:
      if(length < sizeof(tag_settings.enableLowBatSymbol)) {
        return NVM_ERROR;
      }
      *(uint8_t*)data = tag_settings.enableLowBatSymbol;
      return NVM_SUCCESS;
    case OEPL_ENABLE_NORF_ICON:
      if(length < sizeof(tag_settings.enableNoRFSymbol)) {
        return NVM_ERROR;
      }
      *(uint8_t*)data = tag_settings.enableNoRFSymbol;
      return NVM_SUCCESS;
    case OEPL_STORED_CAPABILITIES:
      if(length < sizeof(tag_settings.fastBootCapabilities)) {
        return NVM_ERROR;
      }
      *(uint8_t*)data = tag_settings.fastBootCapabilities;
      return NVM_SUCCESS;
    case OEPL_CUSTOM_MODE:
      if(length < sizeof(tag_settings.customMode)) {
        return NVM_ERROR;
      }
      *(uint8_t*)data = tag_settings.customMode;
      return NVM_SUCCESS;
    case OEPL_LOWBAT_VOLTAGE_MV:
      if(length < sizeof(tag_settings.batLowVoltage)) {
        return NVM_ERROR;
      }
      memcpy(data, &tag_settings.batLowVoltage, sizeof(tag_settings.batLowVoltage));
      return NVM_SUCCESS;
    case OEPL_MIN_CHECKIN_INTERVAL_S:
      if(length < sizeof(tag_settings.minimumCheckInTime)) {
        return NVM_ERROR;
      }
      memcpy(data, &tag_settings.minimumCheckInTime, sizeof(tag_settings.minimumCheckInTime));
      return NVM_SUCCESS;
    case OEPL_FIXED_CHANNEL:
      if(length < sizeof(tag_settings.fixedChannel)) {
        return NVM_ERROR;
      }
      *(uint8_t*)data = tag_settings.fixedChannel;
      return NVM_SUCCESS;
    default:
      break;
  }

  Ecode_t status = nvm3_readData(nvm3_defaultHandle, NVM3_OBJECT_ID_SETTINGS_START + (size_t)entry, data, length);
  if(status == ECODE_NVM3_OK) {
    return NVM_SUCCESS;
  } else if(status == ECODE_NVM3_ERR_KEY_NOT_FOUND) {
    return NVM_NOT_FOUND;
  } else {
    return NVM_ERROR;
  }
}

oepl_nvm_status_t oepl_nvm_setting_delete(oepl_setting_entry_t entry)
{
  switch(entry) {
    case OEPL_ENABLE_FASTBOOT:
      // Fallthrough
    case OEPL_ENABLE_RFWAKE:
      // Fallthrough
    case OEPL_ENABLE_TAGROAMING:
      // Fallthrough
    case OEPL_ENABLE_AUTOSCAN_ON_ORPHAN:
      // Fallthrough
    case OEPL_ENABLE_LOWBAT_ICON:
      // Fallthrough
    case OEPL_ENABLE_NORF_ICON:
      // Fallthrough
    case OEPL_STORED_CAPABILITIES:
      // Fallthrough
    case OEPL_CUSTOM_MODE:
      // Fallthrough
    case OEPL_LOWBAT_VOLTAGE_MV:
      // Fallthrough
    case OEPL_MIN_CHECKIN_INTERVAL_S:
      // Fallthrough
    case OEPL_FIXED_CHANNEL:
      // Cannot remove members of the tag settings struct
      return NVM_NOT_SUPPORTED;
    default:
      break;
  }

  Ecode_t status = nvm3_deleteObject(nvm3_defaultHandle, NVM3_OBJECT_ID_SETTINGS_START + (size_t)entry);
  if(status == ECODE_NVM3_OK || status == ECODE_NVM3_ERR_KEY_NOT_FOUND) {
    return NVM_SUCCESS;
  } else {
    return NVM_ERROR;
  }
}

oepl_nvm_status_t oepl_nvm_setting_set_default(oepl_setting_entry_t entry)
{
  if(entry != OEPL_RAW_TAGSETTINGS) {
    return NVM_NOT_SUPPORTED;
  }

  return oepl_nvm_setting_set(OEPL_RAW_TAGSETTINGS, &oepl_default_tagsettings, sizeof(oepl_default_tagsettings));
}

// ------------------------ OTA upgrade functionality --------------------------
oepl_nvm_status_t oepl_fwu_erase(void)
{
  int32_t status;
  oepl_hw_flash_wake();
  if((status = bootloader_init()) != BOOTLOADER_OK)
  {
    DPRINTF("Failed BTL init with %08lx\n", status);
    goto exit;
  }

  if((status = bootloader_eraseStorageSlot(0)) != BOOTLOADER_OK)
  {
    DPRINTF("Failed BTL erase with %08lx\n", status);
    goto done;
  }

  oepl_nvm_status_t retval = oepl_fwu_set_metadata(0, 0, 0);

  done:
  if((status = bootloader_deinit()) != BOOTLOADER_OK)
  {
    DPRINTF("Failed BTL deinit with %08lx\n", status);
  }
  exit:
  oepl_hw_flash_deepsleep();
  return retval;
}

oepl_nvm_status_t oepl_fwu_set_metadata(uint16_t new_version, uint64_t file_md5, size_t file_size)
{
  device_fwu_meta_t meta;
  nvm3_deleteObject(nvm3_defaultHandle, NVM3_OBJECT_ID_FWU_BLOCK_COUNTER);
  Ecode_t status = nvm3_readData(nvm3_defaultHandle, NVM3_OBJECT_ID_FWU_METADATA, &meta, sizeof(meta));
  if(status == ECODE_NVM3_ERR_KEY_NOT_FOUND) {
    meta.current_md5 = 0;
    meta.current_version = oepl_hw_get_swversion();
    meta.current_size = 0;
    meta.staged_version = new_version;
    meta.staged_md5 = file_md5;
    meta.staged_size = file_size;
    nvm3_writeData(nvm3_defaultHandle, NVM3_OBJECT_ID_FWU_METADATA, &meta, sizeof(meta));
    return NVM_SUCCESS;
  } else if(status == ECODE_NVM3_OK) {
    meta.staged_version = new_version;
    meta.staged_md5 = file_md5;
    meta.staged_size = file_size;
    nvm3_writeData(nvm3_defaultHandle, NVM3_OBJECT_ID_FWU_METADATA, &meta, sizeof(meta));
    return NVM_SUCCESS;
  } else {
    return NVM_ERROR;
  }
}

oepl_nvm_status_t oepl_fwu_get_metadata(uint16_t* new_version, uint64_t* file_md5, size_t* file_size)
{
  device_fwu_meta_t meta;
  Ecode_t status = nvm3_readData(nvm3_defaultHandle, NVM3_OBJECT_ID_FWU_METADATA, &meta, sizeof(meta));
  if(status == ECODE_NVM3_OK) {
    *new_version = meta.staged_version;
    *file_md5 = meta.staged_md5;
    *file_size = meta.staged_size;
    return NVM_SUCCESS;
  } else if (status == ECODE_NVM3_ERR_KEY_NOT_FOUND) {
    return NVM_NOT_FOUND;
  } else {
    return NVM_ERROR;
  }
}

oepl_nvm_status_t oepl_fwu_get_highest_block_written(size_t* block_idx)
{
  uint32_t counter_value;
  Ecode_t status = nvm3_readCounter(nvm3_defaultHandle, NVM3_OBJECT_ID_FWU_BLOCK_COUNTER, &counter_value);
  if(status == ECODE_NVM3_OK) {
    *block_idx = counter_value;
  } else if(status == ECODE_NVM3_ERR_KEY_NOT_FOUND) {
    *block_idx = 0;
    nvm3_writeCounter(nvm3_defaultHandle, NVM3_OBJECT_ID_FWU_BLOCK_COUNTER, 0);
  } else {
    return NVM_ERROR;
  }
  return NVM_SUCCESS;
}

oepl_nvm_status_t oepl_fwu_write(size_t block_idx, const uint8_t bytes[4096], size_t actual_length)
{
  size_t highest_written;
  if(oepl_fwu_get_highest_block_written(&highest_written) != NVM_SUCCESS) {
    return NVM_ERROR;
  }

  if(actual_length > 4096) {
    return NVM_ERROR;
  }

  if(block_idx != highest_written + 1
     && block_idx != 0) {
    DPRINTF("ERR: FWU writing out of sequence block %d (written %d)\n", block_idx, highest_written);
    return NVM_ERROR;
  }
  int32_t btl_status;
  oepl_hw_flash_wake();
  if((btl_status = bootloader_init()) != BOOTLOADER_OK) {
    DPRINTF("Failed BTL init with %08lx\n", btl_status);
    oepl_hw_flash_deepsleep();
    return NVM_ERROR;
  }

  // Todo: the bootloader API somehow declares its input a mutable buffer. Does it actually modify it?
  btl_status = bootloader_writeStorage(0, block_idx * 4096, (uint8_t*)bytes, actual_length);
  bootloader_deinit();
  oepl_hw_flash_deepsleep();
  if(btl_status == BOOTLOADER_OK) {
    if(block_idx == 0) {
      nvm3_writeCounter(nvm3_defaultHandle, NVM3_OBJECT_ID_FWU_BLOCK_COUNTER, 0);
      highest_written = 0;
    } else {
      nvm3_incrementCounter(nvm3_defaultHandle, NVM3_OBJECT_ID_FWU_BLOCK_COUNTER, (uint32_t*)&highest_written);
    }

    if(block_idx != highest_written) {
      DPRINTF("Couldn't match FWU block write counter\n");
      return NVM_ERROR;
    }
    return NVM_SUCCESS;
  } else {
    DPRINTF("Failed storage write with %ld\n", btl_status);
    return NVM_ERROR;
  }
}

static uint8_t metadata[8];

void oepl_fwu_metadata_cb(uint32_t address,
                          uint8_t  *data,
                          size_t   length,
                          void     *context)
{
  (void)context;
  if(address < sizeof(metadata) &&
     (address + length <= sizeof(metadata))) {
    memcpy(&metadata[address], data, length);
  }
}

oepl_nvm_status_t oepl_fwu_check(void)
{
  // To check an FWU, we first check the staged MD5 against the content of the bootload slot
  oepl_nvm_status_t status = check_fwu_md5();
  if(status != NVM_SUCCESS) {
    return status;
  }

  // Then we let the bootloader do a file consistency check
  oepl_hw_flash_wake();
  int32_t btl_status = bootloader_init();
  if(btl_status != BOOTLOADER_OK) {
    goto exit;
  }

  memset(metadata, 0, sizeof(metadata));

  btl_status = bootloader_verifyImage(0, oepl_fwu_metadata_cb);
  if(btl_status != BOOTLOADER_OK) {
    DPRINTF("Failed image verification with %08lx\n", btl_status);
    goto done;
  }
  if(memcmp(metadata, "OEPL_UNI", 8) != 0) {
    DPRINTF("Incorrect metadata, this OTA file is not meant for this product\n");
    btl_status = BOOTLOADER_ERROR_SECURITY_REJECTED;
    goto done;
  }
  DPRINTF("Succesfully verified image in slot 0\n");
done:
  if(bootloader_deinit() != BOOTLOADER_OK) {
    DPRINTF("Failed BTL deinit with %08lx\n", btl_status);
  }
exit:
  oepl_hw_flash_deepsleep();
  if(btl_status != BOOTLOADER_OK) {
    return NVM_ERROR;
  } else {
    return NVM_SUCCESS;
  }
}

oepl_nvm_status_t oepl_fwu_apply(void)
{
  // Trigger the bootloader
  int32_t status;
  oepl_hw_flash_wake();
  if((status = bootloader_init()) != BOOTLOADER_OK) {
    DPRINTF("Failed BTL init with %08lx\n", status);
    goto exit;
  }

  // Note: this will trigger a reboot if all went well
  bootloader_rebootAndInstall();

  // If we end up here, it's an error
  DPRINTF("Fell through bootload application\n");
exit:
  if((status = bootloader_deinit()) != BOOTLOADER_OK) {
    DPRINTF("Failed BTL deinit with %08lx\n", status);
  }
  oepl_hw_flash_deepsleep();

  return NVM_ERROR;
}

bool oepl_fwu_is_upgraded(void)
{
  device_fwu_meta_t meta;
  Ecode_t status = nvm3_readData(nvm3_defaultHandle, NVM3_OBJECT_ID_FWU_METADATA, &meta, sizeof(meta));
  if(status == ECODE_NVM3_ERR_KEY_NOT_FOUND) {
    DPRINTF("No firmware meta data found, creating\n");
    meta.current_md5 = 0;
    meta.current_version = oepl_hw_get_swversion();
    meta.current_size = 0;
    meta.staged_version = 0;
    meta.staged_md5 = 0;
    meta.staged_size = 0;
    nvm3_writeData(nvm3_defaultHandle, NVM3_OBJECT_ID_FWU_METADATA, &meta, sizeof(meta));
    return false;
  } else if(status == ECODE_NVM3_OK) {
    DPRINTF("Checking current rev %d against previously stored %d\n", oepl_hw_get_swversion(), meta.current_version);
    if(oepl_hw_get_swversion() > meta.current_version) {
      // Todo: upgrade logic?
      DPRINTF("Upgrade happened\n");
      return true;
    } else if(oepl_hw_get_swversion() < meta.current_version) {
      // Todo: downgrade logic?
      DPRINTF("Downgrade happened\n");
      return true;
    } else {
      return false;
    }
  } else {
    return false;
  }
}

oepl_nvm_status_t oepl_fwu_confirm_upgrade(void)
{
  device_fwu_meta_t meta;
  Ecode_t status = nvm3_readData(nvm3_defaultHandle, NVM3_OBJECT_ID_FWU_METADATA, &meta, sizeof(meta));
  if(status == ECODE_NVM3_OK) {
    meta.current_md5 = meta.staged_md5;
    meta.current_version = oepl_hw_get_swversion();
    meta.current_size = meta.staged_size;
    meta.staged_version = 0;
    meta.staged_md5 = 0;
    meta.staged_size = 0;
    nvm3_writeData(nvm3_defaultHandle, NVM3_OBJECT_ID_FWU_METADATA, &meta, sizeof(meta));
    DPRINTF("Upgrade confirmed in NVM\n");
    oepl_fwu_erase();
    return NVM_SUCCESS;
  } else {
    return NVM_ERROR;
  }
}

bool oepl_fwu_should_download(uint64_t update_md5, size_t update_filesize)
{
  device_fwu_meta_t meta;
  Ecode_t status = nvm3_readData(nvm3_defaultHandle, NVM3_OBJECT_ID_FWU_METADATA, &meta, sizeof(meta));
  if(status == ECODE_NVM3_OK) {
    if(meta.current_md5 == update_md5 && meta.current_size == update_filesize) {
      // We're already running this file
      return false;
    } else {
      return true;
    }
  } else {
    // We don't know, let the download proceed.
    return true;
  }
}

// ------------------------ Image storage functionality ------------------------
oepl_nvm_status_t oepl_nvm_get_num_img_slots(size_t* num_slots, size_t* slot_size)
{
  static size_t num_slots_cache = 0;
  static size_t slot_size_cache = 0;

  if(num_slots_cache > 0) {
    *num_slots = num_slots_cache;
    *slot_size = slot_size_cache;
    return NVM_SUCCESS;
  }

  size_t xres, yres, bpp;
  bool status = oepl_hw_get_screen_properties(&xres, &yres, &bpp);
  if(!status) {
    return NVM_ERROR;
  }

  if(devconfig.bulk_storage_pagesize == 0 ||
     devconfig.bulk_storage_size == 0) {
    // No bulk storage configured?
    return NVM_ERROR;
  }

  // Calculate amount of bytes needed for this screen type
  size_t raw_framesize = xres * yres * bpp / 8;

  // Calculate least amount of page-multiple bytes needed to contain a raw image for this screen
  slot_size_cache = raw_framesize / devconfig.bulk_storage_pagesize;
  if(raw_framesize % devconfig.bulk_storage_pagesize) {
    slot_size_cache++;
  }
  slot_size_cache *= devconfig.bulk_storage_pagesize;

  // Calculate amount of times we can fit this page-aligned size in bulk storage
  num_slots_cache = devconfig.bulk_storage_size / slot_size_cache;
  if(num_slots_cache > NVM3_OBJECT_ID_IMAGE_METADATA_MAX - NVM3_OBJECT_ID_IMAGE_METADATA_BASE) {
    num_slots_cache = NVM3_OBJECT_ID_IMAGE_METADATA_MAX - NVM3_OBJECT_ID_IMAGE_METADATA_BASE;
  }

  // Output sizes
  *num_slots = num_slots_cache;
  *slot_size = slot_size_cache;
  return NVM_SUCCESS;
}

oepl_nvm_status_t oepl_nvm_get_image_raw_address(size_t img_idx, uint32_t* address)
{
  size_t num_slots, slot_size;
  if(oepl_nvm_get_num_img_slots(&num_slots, &slot_size) != NVM_SUCCESS) {
    return NVM_ERROR;
  }

  if(devconfig.bulk_storage_pagesize == 0 ||
     devconfig.bulk_storage_size == 0) {
    // No bulk storage configured?
    return NVM_ERROR;
  }

  *address = devconfig.bulk_storage_base_address + img_idx * slot_size;
  return NVM_SUCCESS;
}

oepl_nvm_status_t oepl_nvm_get_image_by_hash(uint64_t md5, uint32_t size, size_t* img_idx, oepl_stored_image_hdr_t* metadata)
{
  size_t num_slots, slot_size;
  oepl_nvm_status_t retval = oepl_nvm_get_num_img_slots(&num_slots, &slot_size);
  if(retval != NVM_SUCCESS) {
    return retval;
  }

  for(size_t i = 0; i < num_slots; i++) {
    Ecode_t nvm_status = nvm3_readData(nvm3_defaultHandle, NVM3_OBJECT_ID_IMAGE_METADATA_BASE + i, metadata, sizeof(oepl_stored_image_hdr_t));
    if(nvm_status != ECODE_NVM3_OK &&
       nvm_status != ECODE_NVM3_ERR_KEY_NOT_FOUND) {
      return NVM_ERROR;
    }
    if(metadata->md5 == md5 && metadata->size == size) {
      *img_idx = i;
      return NVM_SUCCESS;
    }
  }

  // If we exited the for loop, we haven't found a matching image.
  return NVM_NOT_FOUND;
}

oepl_nvm_status_t oepl_nvm_get_image_by_type(uint8_t image_type, size_t* img_idx, size_t* seqno)
{
  size_t num_slots, slot_size, highest_seq = 0;
  bool found = false;
  oepl_nvm_status_t retval = oepl_nvm_get_num_img_slots(&num_slots, &slot_size);
  if(retval != NVM_SUCCESS) {
    return retval;
  }

  image_type &= ~(CUSTOM_IMAGE_PRELOAD_FLAG | CUSTOM_IMAGE_LUT_MASK);

  for(size_t i = 0; i < num_slots; i++) {
    oepl_stored_image_hdr_t imgmeta;
    Ecode_t nvm_status = nvm3_readData(nvm3_defaultHandle, NVM3_OBJECT_ID_IMAGE_METADATA_BASE + i, &imgmeta, sizeof(imgmeta));
    if(nvm_status != ECODE_NVM3_OK &&
       nvm_status != ECODE_NVM3_ERR_KEY_NOT_FOUND) {
      return NVM_ERROR;
    }
    if(imgmeta.image_type == image_type) {
      if(imgmeta.seqno >= highest_seq) {
        *img_idx = i;
        highest_seq = imgmeta.seqno;
        if(seqno != NULL) {
          *seqno = imgmeta.seqno;
        }
      }
      found = true;
    }
  }

  // If we exited the for loop, we haven't found a matching image.
  return found ? NVM_SUCCESS : NVM_NOT_FOUND;
}

oepl_nvm_status_t oepl_nvm_get_free_image_slot(size_t* img_idx, uint8_t image_type)
{
  size_t num_slots, slot_size, candidate_seq = 0, candidate_idx = 0;
  bool found_candidate = false;
  oepl_nvm_status_t retval = oepl_nvm_get_num_img_slots(&num_slots, &slot_size);
  if(retval != NVM_SUCCESS) {
    return retval;
  }

  image_type &= ~(CUSTOM_IMAGE_PRELOAD_FLAG | CUSTOM_IMAGE_LUT_MASK);

  for(size_t i = 0; i < num_slots; i++) {
    oepl_stored_image_hdr_t imgmeta;
    Ecode_t nvm_status = nvm3_readData(nvm3_defaultHandle, NVM3_OBJECT_ID_IMAGE_METADATA_BASE + i, &imgmeta, sizeof(imgmeta));
    if(nvm_status == ECODE_NVM3_ERR_KEY_NOT_FOUND) {
      // Found a free slot, use it
      *img_idx = i;
      return NVM_SUCCESS;
    } else if(nvm_status != ECODE_NVM3_OK) {
      // Hardware / application error?
      return NVM_ERROR;
    } else if(!imgmeta.is_valid) {
      // Clean up and release this slot back for use
      oepl_nvm_erase_image(i);
      *img_idx = i;
      return NVM_SUCCESS;
    } else {
      // If we find an image of the same type, remember the index of the
      // one with the lowest sequence number since it would be a candidate for replacement.
      if(imgmeta.image_type == image_type) {
        if(!found_candidate || imgmeta.seqno < candidate_seq) {
          candidate_seq = imgmeta.seqno;
          candidate_idx = i;
        }
        found_candidate = true;
      }
    }
  }

  if(found_candidate) {
    // Erase candidate to free up space for new one
    *img_idx = candidate_idx;
    return oepl_nvm_erase_image(candidate_idx);
  }

  // If we exited the for loop, we haven't found an open slot
  return NVM_NOT_FOUND;
}

oepl_nvm_status_t oepl_nvm_erase_image(size_t img_idx)
{
  size_t num_slots, slot_size;
  oepl_nvm_status_t retval = oepl_nvm_get_num_img_slots(&num_slots, &slot_size);
  if(retval != NVM_SUCCESS) {
    return retval;
  }

  if(img_idx >= num_slots) {
    return NVM_NOT_SUPPORTED;
  }

  // Erase the slot in bulk storage first
  int32_t btl_status;
  oepl_hw_flash_wake();
  if((btl_status = bootloader_init()) != BOOTLOADER_OK) {
    DPRINTF("Failed BTL init with %08lx\n", btl_status);
    goto exit;
  }

  btl_status = bootloader_eraseRawStorage(devconfig.bulk_storage_base_address + img_idx * slot_size, slot_size);
  if(btl_status != BOOTLOADER_OK) {
    goto done;
  }

  // Then erase the accompanying metadata
  Ecode_t nvm_status = nvm3_deleteObject(nvm3_defaultHandle, NVM3_OBJECT_ID_IMAGE_METADATA_BASE + img_idx);
  if(nvm_status == ECODE_NVM3_OK ||
     nvm_status == ECODE_NVM3_ERR_KEY_NOT_FOUND) {
    retval = NVM_SUCCESS;
  } else {
    retval = NVM_ERROR;
  }

  done:
  bootloader_deinit();
  exit:
  oepl_hw_flash_deepsleep();
  if(btl_status != BOOTLOADER_OK) {
    return NVM_ERROR;
  } else {
    return retval;
  }
}

oepl_nvm_status_t oepl_nvm_erase_image_cache(uint8_t image_type)
{
  size_t highest_idx, highest_seqno;

  image_type &= ~(CUSTOM_IMAGE_LUT_MASK | CUSTOM_IMAGE_PRELOAD_FLAG);
  oepl_nvm_status_t retval = oepl_nvm_get_image_by_type(image_type, &highest_idx, &highest_seqno);
  if(retval == NVM_NOT_FOUND) {
    return NVM_SUCCESS;
  } else if(retval != NVM_SUCCESS) {
    return retval;
  }
  
  size_t num_slots, slot_size;
  retval = oepl_nvm_get_num_img_slots(&num_slots, &slot_size);
  if(retval != NVM_SUCCESS) {
    return retval;
  }

  for(size_t i = 0; i < num_slots; i++) {
    oepl_stored_image_hdr_t imgmeta;
    Ecode_t nvm_status = nvm3_readData(nvm3_defaultHandle, NVM3_OBJECT_ID_IMAGE_METADATA_BASE + i, &imgmeta, sizeof(imgmeta));
    if(nvm_status == ECODE_NVM3_ERR_KEY_NOT_FOUND) {
      continue;
    } else if(nvm_status != ECODE_NVM3_OK) {
      // Hardware / application error?
      return NVM_ERROR;
    } else if (i == highest_idx) {
      continue;
    } else if(imgmeta.image_type == image_type) {
      // Clean up and release this slot back for use
      retval = oepl_nvm_erase_image(i);
      if(retval != NVM_SUCCESS) {
        return retval;
      }
    }
  }

  return NVM_SUCCESS;
}

oepl_nvm_status_t oepl_nvm_write_image_metadata(size_t img_idx, oepl_stored_image_hdr_t* metadata)
{
  size_t num_slots, slot_size;
  oepl_nvm_status_t retval = oepl_nvm_get_num_img_slots(&num_slots, &slot_size);
  if(retval != NVM_SUCCESS) {
    return retval;
  }

  if(img_idx >= num_slots) {
    return NVM_NOT_SUPPORTED;
  }

  metadata->image_type &= ~(CUSTOM_IMAGE_PRELOAD_FLAG | CUSTOM_IMAGE_LUT_MASK);

  uint32_t type;
  size_t len;
  Ecode_t nvm_status = nvm3_getObjectInfo(nvm3_defaultHandle, NVM3_OBJECT_ID_IMAGE_METADATA_BASE + img_idx, &type, &len);

  if(nvm_status == ECODE_NVM3_ERR_KEY_NOT_FOUND) {
    // We're setting a new image. Track sequence number if another one of the same type exists.
    size_t existing_idx, existing_seq;
    if(oepl_nvm_get_image_by_type(metadata->image_type, &existing_idx, &existing_seq) == NVM_SUCCESS) {
      metadata->seqno = existing_seq + 1;
    }
  } else if(nvm_status == ECODE_NVM3_OK) {
    // We're updating or overwriting an existing object. Ensure we keep the sequence number the same.
    oepl_stored_image_hdr_t existing_image;
    retval = oepl_nvm_read_image_metadata(img_idx, &existing_image);
    if(retval != NVM_SUCCESS) {
      return retval;
    }
    if(metadata->md5 == existing_image.md5 && metadata->size == existing_image.size) {
      metadata->seqno = existing_image.seqno;
    }
  } else {
    // Hardware / application error?
    return NVM_ERROR;
  }

  nvm_status = nvm3_writeData(nvm3_defaultHandle, NVM3_OBJECT_ID_IMAGE_METADATA_BASE + img_idx, metadata, sizeof(oepl_stored_image_hdr_t));

  return nvm_status == ECODE_NVM3_OK ? NVM_SUCCESS : NVM_ERROR;
}

oepl_nvm_status_t oepl_nvm_read_image_metadata(size_t img_idx, oepl_stored_image_hdr_t* metadata)
{
  size_t num_slots, slot_size;
  oepl_nvm_status_t retval = oepl_nvm_get_num_img_slots(&num_slots, &slot_size);
  if(retval != NVM_SUCCESS) {
    return retval;
  }

  if(img_idx >= num_slots) {
    return NVM_NOT_SUPPORTED;
  }

  Ecode_t nvm_status = nvm3_readData(nvm3_defaultHandle, NVM3_OBJECT_ID_IMAGE_METADATA_BASE + img_idx, metadata, sizeof(oepl_stored_image_hdr_t));

  return nvm_status == ECODE_NVM3_OK ? NVM_SUCCESS : NVM_ERROR;
}

oepl_nvm_status_t oepl_nvm_write_image_bytes(size_t img_idx, size_t offset, const uint8_t* bytes, size_t length)
{
  size_t num_slots, slot_size;
  oepl_nvm_status_t retval = oepl_nvm_get_num_img_slots(&num_slots, &slot_size);
  if(retval != NVM_SUCCESS) {
    return retval;
  }

  if(img_idx >= num_slots) {
    return NVM_NOT_SUPPORTED;
  }

  int32_t btl_status;
  oepl_hw_flash_wake();
  if((btl_status = bootloader_init()) != BOOTLOADER_OK) {
    DPRINTF("Failed BTL init with %08lx\n", btl_status);
    goto exit;
  }

  DPRINTF("Write %d to addr 0x%08x\n", length, devconfig.bulk_storage_base_address + img_idx * slot_size + offset);

  // Todo: does the bootloader really modify its input byte buffer?
  btl_status = bootloader_writeRawStorage(devconfig.bulk_storage_base_address + img_idx * slot_size + offset, (uint8_t*)bytes, length);

  bootloader_deinit();

  exit:
  oepl_hw_flash_deepsleep();
  if(btl_status != BOOTLOADER_OK) {
    return NVM_ERROR;
  } else {
    return retval;
  }
}

oepl_nvm_status_t oepl_nvm_read_image_bytes(size_t img_idx, size_t offset, uint8_t* bytes, size_t length)
{
  size_t num_slots, slot_size;
  oepl_nvm_status_t retval = oepl_nvm_get_num_img_slots(&num_slots, &slot_size);
  if(retval != NVM_SUCCESS) {
    return retval;
  }

  if(img_idx >= num_slots) {
    return NVM_NOT_SUPPORTED;
  }

  if(length == HAL_flashRead(devconfig.bulk_storage_base_address + img_idx * slot_size + offset, bytes, length)) {
    retval = NVM_SUCCESS;
  } else {
    retval = NVM_ERROR;
  }

  return retval;
}

// -----------------------------------------------------------------------------
//                          Static Function Definitions
// -----------------------------------------------------------------------------
// Split this in a separate subroutine as the call stack blows up otherwise
static oepl_nvm_status_t check_fwu_md5(void)
{
  uint8_t read_buffer[512];
  size_t offset;
  MD5Context md5;
  device_fwu_meta_t meta;
  oepl_nvm_status_t retval = NVM_SUCCESS;
  Ecode_t status = nvm3_readData(nvm3_defaultHandle, NVM3_OBJECT_ID_FWU_METADATA, &meta, sizeof(meta));
  if(status != ECODE_NVM3_OK) {
    DPRINTF("No staged upgrade found in NVM\n");
    return NVM_ERROR;
  }
  if(meta.staged_version == 0 || meta.staged_size == 0) {
    DPRINTF("No staged upgrade info found\n");
    return NVM_NOT_FOUND;
  }

  oepl_hw_flash_wake();

  size_t read_size = meta.staged_size - 0 > sizeof(read_buffer) ? sizeof(read_buffer) : meta.staged_size - offset;
  DPRINTF("Verifying MD5 on %d bytes in chunks of %d\n", meta.staged_size, read_size);
  md5Init(&md5);
  for(size_t offset = 0; offset < meta.staged_size;) {
    read_size = meta.staged_size - offset > sizeof(read_buffer) ? sizeof(read_buffer) : meta.staged_size - offset;
    if(read_size != HAL_flashRead(offset, read_buffer, read_size)) {
      DPRINTF("Failed FWU read\n");
      retval = NVM_ERROR;
      goto exit;
    }
    md5Update(&md5, read_buffer, read_size);
    offset += read_size;
  }
  md5Finalize(&md5);

  if(memcmp(&md5.digest[0], &meta.staged_md5, sizeof(meta.staged_md5) != 0)) {
    DPRINTF("MD5 failed, got [ ");
    for(size_t i = 0; i < 8; i++) {
      DPRINTF("%02X", md5.digest[i]);
    }
    DPRINTF(" ] expected [ ");
    for(size_t i = 0; i < 8; i++) {
      DPRINTF("%02X", ((uint8_t*)&meta.staged_md5)[i]);
    }
    retval = NVM_NOT_FOUND;
    goto exit;
  }
  exit:
  oepl_hw_flash_deepsleep();
  return retval;
}
