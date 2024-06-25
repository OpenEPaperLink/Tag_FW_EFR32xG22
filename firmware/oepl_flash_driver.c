// -----------------------------------------------------------------------------
//                                   Includes
// -----------------------------------------------------------------------------
#include "oepl_flash_driver.h"
#include "oepl_nvm.h"
#include "oepl_hw_abstraction.h"
#include "oepl_efr32_hwtypes.h"
#include <spidrv.h>
#include "string.h"
#include "sl_udelay.h"

// -----------------------------------------------------------------------------
//                              Configuration values
// -----------------------------------------------------------------------------
#ifndef FLASH_DEBUG_PRINT
#define FLASH_DEBUG_PRINT 1
#endif

// -----------------------------------------------------------------------------
//                              Macros and Typedefs
// -----------------------------------------------------------------------------
#if FLASH_DEBUG_PRINT
#define DPRINTF(fmt_, ...) oepl_hw_debugprint(DBG_FLASH, (fmt_), ##__VA_ARGS__)
#else
#define DPRINTF(...)
#endif

// -----------------------------------------------------------------------------
//                          Static Function Declarations
// -----------------------------------------------------------------------------
static void init_flashdriver(void);
static void setup_spi(void);
static void teardown_spi(void);
static void read_bytes(uint32_t address, uint8_t* buffer, size_t bytes);

// -----------------------------------------------------------------------------
//                                Global Variables
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
//                                Static Variables
// -----------------------------------------------------------------------------
static const oepl_efr32xg22_tagconfig_t* cfg = NULL;
static SPIDRV_HandleData_t handledata;
static SPIDRV_Handle_t handle = &handledata;

// -----------------------------------------------------------------------------
//                          Public Function Definitions
// -----------------------------------------------------------------------------

uint32_t HAL_flashRead(uint32_t address, uint8_t *buffer, uint32_t num)
{
  init_flashdriver();

  if(cfg == NULL || cfg->flash == NULL) {
    oepl_hw_crash(DBG_FLASH, false, "Unknown flash configuration\n");
  }

  setup_spi();
  read_bytes(address, buffer, num);
  teardown_spi();

  return num;
}

// -----------------------------------------------------------------------------
//                          Static Function Definitions
// -----------------------------------------------------------------------------
static void init_flashdriver(void)
{
  if(cfg != NULL) {
    return;
  }

  cfg = oepl_efr32xg22_get_config();
}

static void setup_spi(void)
{
  SPIDRV_Init_t spi_init = SPIDRV_MASTER_DEFAULT;
  spi_init.port = cfg->flash->usart;
  spi_init.portTx = cfg->flash->MOSI.port;
  spi_init.pinTx = cfg->flash->MOSI.pin;
  spi_init.portRx = cfg->flash->MISO.port;
  spi_init.pinRx = cfg->flash->MISO.pin;
  spi_init.portClk = cfg->flash->SCK.port;
  spi_init.pinClk = cfg->flash->SCK.pin;
  spi_init.bitRate = 10000000;
  spi_init.csControl = spidrvCsControlApplication;

  if(cfg->flash->EN.port != gpioPortInvalid) {
    GPIO_PinModeSet(cfg->flash->EN.port, cfg->flash->EN.pin, gpioModePushPull, ~cfg->flash->EN.idle_state);
  }

  GPIO_PinModeSet(cfg->flash->nCS.port, cfg->flash->nCS.pin, gpioModePushPull, 1);
  SPIDRV_Init(handle, &spi_init);

  // Wake the flash
  // If it's an MX25 in deep sleep, use CS assert to wake it
  // Wake up flash in case the device is in deep power down mode already.
  GPIO_PinOutClear(cfg->flash->nCS.port, cfg->flash->nCS.pin);
  sl_udelay_wait(20);                  // wait for tCRDP=20us
  GPIO_PinOutSet(cfg->flash->nCS.port, cfg->flash->nCS.pin);
  sl_udelay_wait(35);                  // wait for tRDP=35us

  // If it's another SFDP flash, issue the standard wakeup call
  GPIO_PinOutClear(cfg->flash->nCS.port, cfg->flash->nCS.pin);
  uint8_t powerup[] = {0xAB};
  SPIDRV_MTransmitB(handle, powerup, sizeof(powerup));
  GPIO_PinOutSet(cfg->flash->nCS.port, cfg->flash->nCS.pin);
  sl_udelay_wait(3);

  // Sanity checks before reading from flash:
  // Check the JEDEC ID can be read
  uint8_t jedec_id[4] = {0x9F, 0x00, 0x00, 0x00};
  while(jedec_id[1] == 0) {
    GPIO_PinOutClear(cfg->flash->nCS.port, cfg->flash->nCS.pin);
    SPIDRV_MTransferB(handle, jedec_id, jedec_id, 4);
    GPIO_PinOutSet(cfg->flash->nCS.port, cfg->flash->nCS.pin);
    jedec_id[0] = 0x9F;
  }

  // Check the busy bit is not set
  jedec_id[0] = 0x05;
  jedec_id[1] = 0xFF;
  while((jedec_id[1] & 0x01) != 0) {
    GPIO_PinOutClear(cfg->flash->nCS.port, cfg->flash->nCS.pin);
    SPIDRV_MTransferB(handle, jedec_id, jedec_id, 2);
    GPIO_PinOutSet(cfg->flash->nCS.port, cfg->flash->nCS.pin);
    jedec_id[0] = 0x05;
  }
}

static void teardown_spi(void)
{
  GPIO_PinOutClear(cfg->flash->nCS.port, cfg->flash->nCS.pin);
  uint8_t powerdown[] = {0xB9};
  SPIDRV_MTransmitB(handle, powerdown, sizeof(powerdown));
  GPIO_PinOutSet(cfg->flash->nCS.port, cfg->flash->nCS.pin);
  sl_udelay_wait(30);

  GPIO_PinModeSet(cfg->flash->nCS.port, cfg->flash->nCS.pin, gpioModeInputPull, 1);
  SPIDRV_DeInit(handle);

  if(cfg->flash->EN.port != gpioPortInvalid) {
    GPIO_PinModeSet(cfg->flash->EN.port, cfg->flash->EN.pin, gpioModeInputPull, cfg->flash->EN.idle_state);
  }
}

static void read_bytes(uint32_t address, uint8_t* buffer, size_t bytes)
{
  uint8_t* buffer_ptr = buffer;

  // Chunk the transfer in 2k blocks, as the EFR32 DMA maxes out at 2k
  for(size_t i = 0; i < bytes / 2048; i++) {
    GPIO_PinOutClear(cfg->flash->nCS.port, cfg->flash->nCS.pin);
    uint8_t readcmd[] = {0x03, address >> 16, address >> 8, address};
    SPIDRV_MTransmitB(handle, readcmd, sizeof(readcmd));
    SPIDRV_MReceiveB(handle, buffer_ptr, 2048);
    GPIO_PinOutSet(cfg->flash->nCS.port, cfg->flash->nCS.pin);
    address += 2048;
    buffer_ptr += 2048;
  }
  
  if((size_t)(buffer_ptr - buffer) < bytes) {
    GPIO_PinOutClear(cfg->flash->nCS.port, cfg->flash->nCS.pin);
    uint8_t readcmd[] = {0x03, address >> 16, address >> 8, address};
    SPIDRV_MTransmitB(handle, readcmd, sizeof(readcmd));
    SPIDRV_MReceiveB(handle, buffer_ptr, bytes - (buffer_ptr - buffer));
    GPIO_PinOutSet(cfg->flash->nCS.port, cfg->flash->nCS.pin);
  }
}