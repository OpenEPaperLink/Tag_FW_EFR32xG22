// -----------------------------------------------------------------------------
// Credit goes to Jonas Niesner for figuring out how to drive the TNB132M, ref.
// https://github.com/OpenDisplay/Firmware_Silabs/commit/b25829df7193c570e21ce1a2f741bc1ee08f5f0f
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
//                                   Includes
// -----------------------------------------------------------------------------
#include "oepl_nfc_driver_tnb132m.h"
#include "oepl_hw_abstraction.h"
#include "oepl_efr32_hwtypes.h"

#include "em_cmu.h"
#include "em_gpio.h"
#include "em_i2c.h"
#include "sl_udelay.h"
#include "em_device.h"

#include <string.h>

// -----------------------------------------------------------------------------
//                              Configuration values
// -----------------------------------------------------------------------------
#ifndef TNB132M_COMMON_DEBUG_PRINT
#define TNB132M_COMMON_DEBUG_PRINT 1
#endif

// -----------------------------------------------------------------------------
//                              Macros and Typedefs
// -----------------------------------------------------------------------------
#if TNB132M_COMMON_DEBUG_PRINT
#define DPRINTF(fmt_, ...) oepl_hw_debugprint(DBG_NFC, (fmt_), ##__VA_ARGS__)
#else
#define DPRINTF(...)
#endif

// -----------------------------------------------------------------------------
//                          Static Function Declarations
// -----------------------------------------------------------------------------
static void open_io(const oepl_efr32xg22_nfcconfig_t* cfg);
static void close_io(const oepl_efr32xg22_nfcconfig_t* cfg);


// -----------------------------------------------------------------------------
//                                Global Variables
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
//                                Static Variables
// -----------------------------------------------------------------------------
const oepl_nfc_driver_desc_t oepl_nfc_driver_tnb132m = {
  .init = oepl_nfc_init_tnb132m,
  .write = oepl_nfc_write_tnb132m
};

// -----------------------------------------------------------------------------
//                          Static Function Definitions
// -----------------------------------------------------------------------------
static void open_io(const oepl_efr32xg22_nfcconfig_t* cfg)
{
  GPIO_PinModeSet(cfg->SCL.port, cfg->SCL.pin, gpioModeWiredAndFilter, 0);
  GPIO_PinModeSet(cfg->SDA.port, cfg->SDA.pin, gpioModeWiredAndFilter, 0);
  if (cfg->power.port != gpioPortInvalid) {
    GPIO_PinModeSet(cfg->power.port, cfg->power.pin, gpioModePushPull, 1);
    sl_udelay_wait(40000);
  } else {
    sl_udelay_wait(10000);
  }

  // Use default settings
  I2C_Init_TypeDef i2cInit = I2C_INIT_DEFAULT;

  size_t i2cnum;
  switch((uint32_t)cfg->i2c) {
    #if defined(I2C0)
    case (uint32_t) I2C0:
      i2cnum = 0;
      CMU_ClockEnable(cmuClock_I2C0, true);
      break;
    #endif
    #if defined(I2C1)
    case (uint32_t) I2C1:
      i2cnum = 1;
      CMU_ClockEnable(cmuClock_I2C1, true);
      break;
    #endif
    #if defined(I2C2)
    case (uint32_t) I2C2:
      i2cnum = 2;
      CMU_ClockEnable(cmuClock_I2C2, true);
      break;
    #endif
    default:
      oepl_hw_crash(DBG_HW, false, "Unknown I2C peripheral %08x\n", cfg->i2c);
      while(1);
  }

  // Route I2C pins to GPIO
  GPIO->I2CROUTE[i2cnum].SDAROUTE = (GPIO->I2CROUTE[0].SDAROUTE & ~_GPIO_I2C_SDAROUTE_MASK)
                        | (cfg->SDA.port << _GPIO_I2C_SDAROUTE_PORT_SHIFT
                        | (cfg->SDA.pin << _GPIO_I2C_SDAROUTE_PIN_SHIFT));
  GPIO->I2CROUTE[i2cnum].SCLROUTE = (GPIO->I2CROUTE[0].SCLROUTE & ~_GPIO_I2C_SCLROUTE_MASK)
                        | (cfg->SCL.port << _GPIO_I2C_SCLROUTE_PORT_SHIFT
                        | (cfg->SCL.pin << _GPIO_I2C_SCLROUTE_PIN_SHIFT));
  GPIO->I2CROUTE[i2cnum].ROUTEEN = GPIO_I2C_ROUTEEN_SDAPEN | GPIO_I2C_ROUTEEN_SCLPEN;

  // Initialize the I2C
  I2C_Init(cfg->i2c, &i2cInit);

  // Enable automatic STOP on NACK
  cfg->i2c->CTRL = I2C_CTRL_AUTOSN;
}

static void close_io(const oepl_efr32xg22_nfcconfig_t* cfg)
{
  I2C_Reset(cfg->i2c);

  size_t i2cnum;
  switch((uint32_t)cfg->i2c) {
    #if defined(I2C0)
    case (uint32_t) I2C0:
      i2cnum = 0;
      CMU_ClockEnable(cmuClock_I2C0, false);
      break;
    #endif
    #if defined(I2C1)
    case (uint32_t) I2C1:
      i2cnum = 1;
      CMU_ClockEnable(cmuClock_I2C1, false);
      break;
    #endif
    #if defined(I2C2)
    case (uint32_t) I2C2:
      i2cnum = 2;
      CMU_ClockEnable(cmuClock_I2C2, false);
      break;
    #endif
    default:
      oepl_hw_crash(DBG_HW, false, "Unknown I2C peripheral\n");
      while(1);
  }

  GPIO->I2CROUTE[i2cnum].ROUTEEN = 0;

  if (cfg->power.port != gpioPortInvalid) {
    GPIO_PinOutClear(cfg->power.port, cfg->power.pin);
    GPIO_PinModeSet(cfg->power.port, cfg->power.pin, gpioModeInput, 1);
  }
  GPIO_PinModeSet(cfg->SCL.port, cfg->SCL.pin, gpioModeInput, 1);
  GPIO_PinModeSet(cfg->SDA.port, cfg->SDA.pin, gpioModeInput, 1);
}

static bool tnb132m_type3_paged_block_read16(const oepl_efr32xg22_nfcconfig_t* cfg,
                                             uint8_t dev7, uint8_t sub, uint8_t *out16)
{
  // Transfer structure
  I2C_TransferSeq_TypeDef i2cTransfer;
  I2C_TransferReturn_TypeDef result;

  // Initialize I2C transfer
  i2cTransfer.addr          = dev7 << 1;
  i2cTransfer.flags         = I2C_FLAG_WRITE_READ;
  i2cTransfer.buf[0].data   = &sub;
  i2cTransfer.buf[0].len    = 1;
  i2cTransfer.buf[1].data   = out16;
  i2cTransfer.buf[1].len    = 16;

  result = I2C_TransferInit(cfg->i2c, &i2cTransfer);

  // Send data
  while (result == i2cTransferInProgress) {
    result = I2C_Transfer(cfg->i2c);
  }

  if (result != i2cTransferDone) {
      DPRINTF("I2C fail %08x\n", result);
      return false;
  } else {
    DPRINTF("I2C Response: ");
    for(size_t i = 0; i < 16; i++) {
      DPRINTF("%02x ", out16[i]);
    }
    DPRINTF("\n");
    return true;
  }
}

/* Type-3 16-byte block write: START, dev+W, sub, 16 data bytes, STOP. Mirrors the
 * paged read on the write side — AI goes to dev=0x48 sub=0, NDEF data blocks go to
 * dev=0x40 sub=0x10/0x20/... (same byte-offset mapping as the read path). */
static bool tnb132m_type3_paged_block_write16(const oepl_efr32xg22_nfcconfig_t* cfg,
                                              uint8_t dev7, uint8_t sub, const uint8_t *in16)
{
  bool a;
  if (in16 == NULL) {
    return false;
  }

  // Transfer structure
  I2C_TransferSeq_TypeDef i2cTransfer;
  I2C_TransferReturn_TypeDef result;

  // Initialize I2C transfer
  i2cTransfer.addr          = dev7 << 1;
  i2cTransfer.flags         = I2C_FLAG_WRITE_WRITE;
  i2cTransfer.buf[0].data   = &sub;
  i2cTransfer.buf[0].len    = 1;
  i2cTransfer.buf[1].data   = in16;
  i2cTransfer.buf[1].len    = 16;

  result = I2C_TransferInit(cfg->i2c, &i2cTransfer);

  // Send data
  while (result == i2cTransferInProgress) {
    result = I2C_Transfer(cfg->i2c);
  }

  if (result != i2cTransferDone) {
      return false;
  } else {
    return true;
  }
}

static bool tnb132m_prime_type3(const oepl_efr32xg22_nfcconfig_t* cfg)
{
  {
    // Transfer structure
    I2C_TransferSeq_TypeDef i2cTransfer;
    I2C_TransferReturn_TypeDef result;
    uint8_t txBuffer[2];

    txBuffer[0] = 0x21;
    txBuffer[1] = 0x04;

    // Initialize I2C transfer
    i2cTransfer.addr          = 0x30 << 1;
    i2cTransfer.flags         = I2C_FLAG_WRITE;
    i2cTransfer.buf[0].data   = txBuffer;
    i2cTransfer.buf[0].len    = 2;
    i2cTransfer.buf[1].data   = NULL;
    i2cTransfer.buf[1].len    = 0;

    result = I2C_TransferInit(cfg->i2c, &i2cTransfer);

    // Send data
    while (result == i2cTransferInProgress) {
      result = I2C_Transfer(cfg->i2c);
    }

    if (result != i2cTransferDone) {
      DPRINTF("I2C fail %08x\n", result);
      return false;
    }
  }

  {
    // Transfer structure
    I2C_TransferSeq_TypeDef i2cTransfer;
    I2C_TransferReturn_TypeDef result;
    uint8_t txBuffer[1 + 1];

    txBuffer[0] = 0x25;
    txBuffer[1] = 0x00;

    // Initialize I2C transfer
    i2cTransfer.addr          = 0x30 << 1;
    i2cTransfer.flags         = I2C_FLAG_WRITE_READ;
    i2cTransfer.buf[0].data   = txBuffer;
    i2cTransfer.buf[0].len    = 1;
    i2cTransfer.buf[1].data   = &txBuffer[1];
    i2cTransfer.buf[1].len    = 1;

    result = I2C_TransferInit(cfg->i2c, &i2cTransfer);

    // Send data
    while (result == i2cTransferInProgress) {
      result = I2C_Transfer(cfg->i2c);
    }

    if (result != i2cTransferDone) {
      DPRINTF("I2C fail %08x\n", result);
      return false;
    } else {
      DPRINTF("I2C Response %02x\n", txBuffer[1]);
    }
  }

  sl_udelay_wait(20000);

  {
    // Transfer structure
    I2C_TransferSeq_TypeDef i2cTransfer;
    I2C_TransferReturn_TypeDef result;
    uint8_t txBuffer[1 + 16];

    txBuffer[0] = 0x30;

    // Initialize I2C transfer
    i2cTransfer.addr          = 0x43 << 1;
    i2cTransfer.flags         = I2C_FLAG_WRITE_READ;
    i2cTransfer.buf[0].data   = txBuffer;
    i2cTransfer.buf[0].len    = 1;
    i2cTransfer.buf[1].data   = txBuffer;
    i2cTransfer.buf[1].len    = 16;

    result = I2C_TransferInit(cfg->i2c, &i2cTransfer);

    // Send data
    while (result == i2cTransferInProgress) {
      result = I2C_Transfer(cfg->i2c);
    }

    if (result != i2cTransferDone) {
      DPRINTF("I2C fail %08x\n", result);
      return false;
    } else {
      DPRINTF("I2C Response: ");
      for(size_t i = 0; i < sizeof(txBuffer) - 1; i++) {
        DPRINTF("%02x ", txBuffer[1+i]);
      }
      DPRINTF("\n");
    }
  }

  return true;
}

// -----------------------------------------------------------------------------
//                          Public Function Definitions
// -----------------------------------------------------------------------------
bool oepl_nfc_init_tnb132m(const oepl_efr32xg22_nfcconfig_t* cfg)
{
  uint8_t ai[16];
  uint8_t data[64];
  uint32_t ln;
  uint16_t sum_calc, sum_read;
  unsigned need_blocks;
  bool success = true;

  open_io(cfg);

  tnb132m_prime_type3(cfg);
  sl_udelay_wait(2000);

  success = tnb132m_type3_paged_block_read16(cfg, 0x48u, 0x00u, ai);
  if (!success) {
    DPRINTF("NFC ndef AI @0x48 sub=0: NACK\r\n");
    goto cleanup;
  }

  if (ai[0] != 0x10u) {
    DPRINTF("NFC ndef AI ver=%02x (not 1.x) — skip decode (blank tag?)\r\n", ai[0]);
    goto cleanup;
  }
  ln = ((uint32_t)ai[11] << 16) | ((uint32_t)ai[12] << 8) | (uint32_t)ai[13];
  sum_read = (uint16_t)(((uint16_t)ai[14] << 8) | (uint16_t)ai[15]);
  sum_calc = 0;
  for (unsigned i = 0; i < 14u; i++) {
    sum_calc = (uint16_t)(sum_calc + ai[i]);
  }
  DPRINTF("NFC ndef AI Ver=%u.%u Nbr=%u Nbw=%u Nmaxb=%u RWFlag=%02x Ln=%lu Sum=%04x (calc %04x %s)\r\n",
        (unsigned)(ai[0] >> 4), (unsigned)(ai[0] & 0x0Fu), (unsigned)ai[1], (unsigned)ai[2],
        (unsigned)(((uint16_t)ai[3] << 8) | ai[4]), ai[10], (unsigned long)ln, (unsigned)sum_read,
        (unsigned)sum_calc, sum_calc == sum_read ? "OK" : "BAD");
  if (ln == 0u || ln > sizeof(data)) {
    DPRINTF("NFC ndef Ln=%lu out of range (max %u)\r\n",
          (unsigned long)ln, (unsigned)sizeof(data));
    goto cleanup;
  }
  need_blocks = (unsigned)((ln + 15u) / 16u);
  for (unsigned b = 0; b < need_blocks; b++) {
    uint8_t byte_off = (uint8_t)(0x10u + b * 0x10u);
    uint8_t throwaway[16];
    (void)tnb132m_type3_paged_block_read16(cfg, 0x48u, 0x00u,
                                          throwaway);
    sl_udelay_wait(500);
    success = tnb132m_type3_paged_block_read16(cfg, 0x40u,
                                        byte_off, &data[b * 16u]);
    if (!success) {
      DPRINTF("NFC ndef blk%u @0x40 sub=%02x: NACK\r\n", b + 1u, (unsigned)byte_off);
      goto cleanup;
    }
    DPRINTF("NFC ndef blk%u %04x:", b + 1u, b * 16u);
    for (unsigned j = 0; j < 16u; j++) {
      DPRINTF(" %02x", data[b * 16u + j]);
    }
    DPRINTF("\r\n");
    sl_udelay_wait(200);
  }
  {
    uint8_t tnf0 = data[0] & 0x07u;
    if (ln < 4u || tnf0 == 0u || tnf0 >= 6u) {
      DPRINTF("NFC ndef record too short or bad TNF (ln=%lu hdr=%02x tnf=%u)\r\n",
            (unsigned long)ln, data[0], tnf0);
      goto cleanup;
    }
  }
  {
    uint8_t hdr = data[0];
    uint8_t tnf = hdr & 0x07u;
    bool sr = (hdr & 0x10u) != 0;
    bool il = (hdr & 0x08u) != 0;
    unsigned tlen = data[1];
    unsigned plen;
    unsigned off;
    unsigned ilen = 0;
    if (sr) {
      plen = data[2];
      off = 3u;
    } else {
      if (ln < 6u) {
        DPRINTF("NFC ndef long-format truncated\r\n");
        goto cleanup;
      }
      plen = ((unsigned)data[2] << 24) | ((unsigned)data[3] << 16) | ((unsigned)data[4] << 8) | data[5];
      off = 6u;
    }
    if (il) {
      if (ln < off + 1u) {
        DPRINTF("NFC ndef IL truncated\r\n");
        goto cleanup;
      }
      ilen = data[off];
      off += 1u;
    }
    if (off + tlen + ilen + plen > ln) {
      DPRINTF("NFC ndef fields exceed Ln (off=%u tlen=%u il=%u plen=%u ln=%lu)\r\n",
            off, tlen, ilen, plen, (unsigned long)ln);
      goto cleanup;
    }
    DPRINTF("NFC ndef rec TNF=%u SR=%u IL=%u tlen=%u plen=%u type=\"%.*s\"\r\n",
          tnf, (unsigned)sr, (unsigned)il, tlen, plen, tlen, (const char *)&data[off]);
    off += tlen + ilen;
    if (tnf == 0x01u && tlen == 1u && data[off - tlen - ilen] == 'T' && plen >= 1u) {
      unsigned stat = data[off];
      unsigned lang_len = stat & 0x3Fu;
      if (1u + lang_len <= plen) {
        DPRINTF("NFC ndef Text lang=\"%.*s\" utf%u text=\"%.*s\"\r\n",
              lang_len, (const char *)&data[off + 1u], (stat & 0x80u) ? 16u : 8u,
              (unsigned)(plen - 1u - lang_len), (const char *)&data[off + 1u + lang_len]);
      }
    }
  }

cleanup:
  close_io(cfg);
  return success;
}

bool oepl_nfc_write_tnb132m(const oepl_efr32xg22_nfcconfig_t* cfg, oepl_nfc_buffer_type_t content_type, const uint8_t* raw_buffer, size_t length)
{
  bool success = true;
  uint8_t ai[16] = { 0 };
  uint8_t cur_ai[16];
  static uint8_t s_od_nfc_write_blocks[512];
  uint8_t *blocks = s_od_nfc_write_blocks;
  uint16_t sum;
  uint16_t record_len;
  uint16_t payload_len;
  uint8_t need_blocks;
  uint8_t i;

  if (raw_buffer == NULL || length == 0u) {
    return false;
  }

  DPRINTF("NFC write type %d, content:\n", content_type);
  for(size_t i = 0; i < length; i++) {
    DPRINTF("%02x", raw_buffer[i]);
  }
  DPRINTF("\n");

  /* Defense-in-depth: bound length so the uint16_t payload_len math below
   * (1 + hdr + length) cannot wrap and overrun s_od_nfc_write_blocks. Callers
   * validate too, but this keeps the record builder memory-safe on its own. */
  if (length > sizeof(s_od_nfc_write_blocks)) {
    return false;
  }
  memset(blocks, 0, sizeof(s_od_nfc_write_blocks));

  open_io(cfg);

  CORE_DECLARE_IRQ_STATE;

  if (content_type == TEXT) {
    payload_len = (uint16_t)(1u + 2u + length);
    if (payload_len > 255u || payload_len > (uint16_t)(sizeof(s_od_nfc_write_blocks) - 4u)) {
      success = false;
      goto cleanup;
    }
    record_len = (uint16_t)(4u + payload_len);
    blocks[0] = 0xD1u;
    blocks[1] = 0x01u;
    blocks[2] = (uint8_t)payload_len;
    blocks[3] = 0x54u;
    blocks[4] = 0x02u;
    blocks[5] = (uint8_t)'e';
    blocks[6] = (uint8_t)'n';
    memcpy(&blocks[7], raw_buffer, length);
  } else if (content_type == URI) {
    payload_len = (uint16_t)(1u + length);
    if (payload_len > 255u || payload_len > (uint16_t)(sizeof(s_od_nfc_write_blocks) - 4u)) {
      success = false;
      goto cleanup;
    }
    record_len = (uint16_t)(4u + payload_len);
    blocks[0] = 0xD1u;
    blocks[1] = 0x01u;
    blocks[2] = (uint8_t)payload_len;
    blocks[3] = 0x55u;
    blocks[4] = 0x00u;
    memcpy(&blocks[5], raw_buffer, length);
  } else if (content_type == WELL_KNOWN_RAW) {
    uint8_t type_len;
    uint16_t raw_payload_len;
    if (length < 2u) {
      success = false;
      goto cleanup;
    }
    type_len = raw_buffer[0];
    if (type_len == 0u || (uint16_t)(1u + type_len) > length) {
      success = false;
      goto cleanup;
    }
    raw_payload_len = (uint16_t)(length - 1u - type_len);
    if (raw_payload_len > 255u) {
      success = false;
      goto cleanup;
    }
    record_len = (uint16_t)(3u + type_len + raw_payload_len);
    if (record_len > sizeof(s_od_nfc_write_blocks)) {
      success = false;
      goto cleanup;
    }
    blocks[0] = 0xD1u;
    blocks[1] = type_len;
    blocks[2] = (uint8_t)raw_payload_len;
    memcpy(&blocks[3], &raw_buffer[1], type_len);
    if (raw_payload_len > 0u) {
      memcpy(&blocks[3u + type_len], &raw_buffer[1u + type_len], raw_payload_len);
    }
  } else if (content_type == MIME) {
    uint8_t mime_tl;
    uint16_t body_len;

    if (length < 3u) {
      success = false;
      goto cleanup;
    }
    mime_tl = raw_buffer[0];
    if (mime_tl == 0u || (uint16_t)(1u + mime_tl) > length) {
      success = false;
      goto cleanup;
    }
    body_len = (uint16_t)(length - 1u - mime_tl);
    if (body_len > 255u) {
      success = false;
      goto cleanup;
    }
    record_len = (uint16_t)(3u + mime_tl + body_len);
    if (record_len > sizeof(s_od_nfc_write_blocks)) {
      success = false;
      goto cleanup;
    }
    blocks[0] = 0xD2u; /* MB | ME | SR ; TNF = MIME */
    blocks[1] = mime_tl;
    blocks[2] = (uint8_t)body_len;
    memcpy(&blocks[3], &raw_buffer[1], mime_tl);
    if (body_len > 0u) {
      memcpy(&blocks[3u + mime_tl], &raw_buffer[1u + mime_tl], body_len);
    }
  } else if (content_type == RAW_NDEF_BUFFER) {
    record_len = length - 2;
    if (record_len == 0u || record_len > sizeof(s_od_nfc_write_blocks)) {
      success = false;
      goto cleanup;
    }
    memcpy(blocks, &raw_buffer[2], record_len);
  } else {
    success = false;
    goto cleanup;
  }

  CORE_ENTER_CRITICAL();

  tnb132m_prime_type3(cfg);
  sl_udelay_wait(2000);

  ai[0] = 0x10u;
  ai[1] = 0x02u;
  ai[2] = 0x01u;
  ai[3] = 0x00u;
  ai[4] = 0x3Cu;
  ai[11] = (uint8_t)((record_len >> 16) & 0xFFu);
  ai[12] = (uint8_t)((record_len >> 8) & 0xFFu);
  ai[13] = (uint8_t)(record_len & 0xFFu);

  if (tnb132m_type3_paged_block_read16(cfg, 0x48u, 0x00u, cur_ai)
      && (cur_ai[0] & 0xF0u) == 0x10u) {
    ai[10] = cur_ai[10];
  }
  sum = 0u;
  for (i = 0u; i < 14u; i++) {
    sum = (uint16_t)(sum + ai[i]);
  }
  ai[14] = (uint8_t)(sum >> 8);
  ai[15] = (uint8_t)(sum & 0xFFu);

  if (!tnb132m_type3_paged_block_write16(cfg, 0x48u, 0x00u, ai)) {
    CORE_EXIT_CRITICAL();
    success = false;
    goto cleanup;
  }
  sl_udelay_wait(10000);
  need_blocks = (uint8_t)((record_len + 15u) / 16u);
  for (i = 0u; i < need_blocks; i++) {
    uint8_t byte_off = (uint8_t)(0x10u + i * 0x10u);
    if (!tnb132m_type3_paged_block_write16(cfg, 0x40u, byte_off, &blocks[i * 16u])) {
      CORE_EXIT_CRITICAL();
      success = false;
      goto cleanup;
    }
    sl_udelay_wait(10000);
  }

  CORE_EXIT_CRITICAL();

cleanup:
  close_io(cfg);
  return success;
}
