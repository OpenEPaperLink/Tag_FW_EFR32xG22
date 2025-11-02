// -----------------------------------------------------------------------------
//                                   Includes
// -----------------------------------------------------------------------------
#include "oepl_radio.h"
#include "oepl_hw_abstraction.h"
#include "oepl_nvm.h"
#include "sl_rail_util_init.h"
#include "rail.h"
#include "rail_ieee802154.h"
#include <stdio.h>
#include <stddef.h>

#include "sl_sleeptimer.h"
#include "sl_power_manager.h"


#include "oepl-proto.h"
#include "oepl-definitions.h"
#include "oepl_efr32_hwtypes.h"
#include "sl_rail_util_pti_config.h"

// -----------------------------------------------------------------------------
//                              Configuration values
// -----------------------------------------------------------------------------
#ifndef RADIO_DEBUG_PRINT
#define RADIO_DEBUG_PRINT 1
#endif

#ifndef RADIO_DEBUG_PRINT_IN_IRQ
#define RADIO_DEBUG_PRINT_IN_IRQ 0
#endif

// Rescan intervals in orphaned state
#define INTERVAL_1_TIME 3600UL    // Try every hour
#define INTERVAL_1_ATTEMPTS 24    // for 24 attempts (an entire day)
#define INTERVAL_2_TIME 7200UL    // Try every 2 hours
#define INTERVAL_2_ATTEMPTS 12    // for 12 attempts (an additional day)
#define INTERVAL_3_TIME 86400UL   // Finally, try every day

// Poll intervals when not getting a reply
#define POLL_INTERVAL_BASE_TIME 40UL
#define POLL_INTERVAL_BASE_ATTEMPTS 4

#define PONG_TIMEOUT_MS 20
#define POLL_TIMEOUT_MS 20
#define MAX_PING_ROUNDS 20
#define MAX_POLL_ROUNDS 14
#define CHANNEL_LIST {11, 15, 20, 25, 26}
#define FORCED_SCAN_ROUNDS 4
#define QUICK_SCAN_ROUNDS 2
#define LONG_POLL_INTERVAL 300     // How often to do a long poll (including temperature and voltage measurements)



// -----------------------------------------------------------------------------
//                              Macros and Typedefs
// -----------------------------------------------------------------------------
#if RADIO_DEBUG_PRINT
#define DPRINTF(fmt_, ...) oepl_hw_debugprint(DBG_RADIO, (fmt_), ##__VA_ARGS__)
#else
#define DPRINTF(...)
#endif

// Internal radio state, not exposed through API
typedef enum rx_state_e {
  OFF,                // RX turned off
  AWAIT_TRIGGER,      // Not actively trying to receive anything
  AWAIT_PONG,         // Expecting a pong packet addressed to us
  AWAIT_DATAINFO,     // Expecting an Available Data packet addressed to us
  AWAIT_BLOCK,        // Expecting a data block
  AWAIT_BLOCKREQ_ACK, // Expecting an ACK for a block request we sent
  AWAIT_XFER_END_ACK, // Expecting an ACK for an xfer complete/cancel we sent
  AWAIT_TAGDATA_ACK,  // Expecting an ACK for an unsolicited tag data we sent
} rx_state_t;

typedef struct {
  // highest observed RSSI during this scan
  int8_t highest_rssi;
  // highest observed LQI during this scan
  uint8_t highest_lqi;
  // Channel index on which the highest LQI was observed
  int8_t highest_lqi_chidx;
  // MAC of the AP which gave us the highest RSSI and LQI so far
  uint8_t higest_mac[8];
  // Remaining amount of full channel loops
  uint8_t remaining_scan_it;
  // Remaining amount of pings on this channel in this loop
  uint8_t remaining_channel_it;
  // Channel we're currently scanning
  uint8_t current_chidx;
  // Whether to scan in fast-associate mode
  bool fast_associate;
} scan_data_t;

typedef struct {
  // Remaining polls
  uint8_t remaining_poll_it;
  // Does the current poll have data?
  bool has_payload;
  // Data with which we are polling
  uint8_t poll_payload[sizeof(struct AvailDataReq)]; 
  // Result
  oepl_radio_data_indication_t result;
} poll_data_t;

typedef struct {
  // Data block we are requesting
  oepl_datablock_descriptor_t requested_block;
  // Remaining retries
  size_t retries;
  // Mask of block parts still missing
  uint8_t remaining_parts_mask[6];
  // Did we receive an ACK for our block request?
  bool ack_recv;
} blockreq_data_t;

typedef struct {
  // MAC address of the AP we are confirming to
  uint8_t AP_MAC[8];
  // PAN of the AP we are confirming to
  uint16_t AP_PAN;
  // Remaining retries
  size_t retries;
} confirm_data_t;

// State tracking variables for each of the radio states (oepl_radio_status_t)
typedef union {
  scan_data_t searching;
  poll_data_t polling;
  blockreq_data_t blockreq;
  confirm_data_t confirm;
} state_data_t;

// -----------------------------------------------------------------------------
//                          Static Function Declarations
// -----------------------------------------------------------------------------
/// Turn on the radio on the given channel number (not index), i.e. 11 - 26
static void start_rx(uint8_t channel);
/// Turn off the radio
static void stop_rx(void);
/// Try to parse a packet from the RX queue
/// Returns true if a packet matching one of the expected packet types has been
/// received, in which case the data for the pointers will be valid
static bool try_ingest_packet(
  const uint8_t* expected_packettypes, size_t num_packettypes,
  const struct MacFrameNormal** f, uint8_t* payload_type,
  const uint8_t** payload, size_t* payload_size,
  RAIL_RxPacketInfo_t* packet_info, RAIL_RxPacketDetails_t* packet_details);
/// Start a channel scan
static void start_scan(uint8_t rounds, bool is_roam, size_t fast_associate_idx);
/// Start the state timer for doing the next scan
static void schedule_next_scan(void);
/// Start the state timer for doing the next data poll
static void schedule_next_poll(size_t timeout_s);
/// Send a ping packet (to detect whether an AP is active on a channel)
static void send_ping(uint8_t channel);
/// Send a poll packet (to ask any AP on the channel whether there is data for us)
static void send_poll(uint8_t channel, bool is_short);
/// Copy our hardware MAC to the array in big endian format (human readable)
static void get_mac_be(uint8_t* mac);
/// Copy our hardware MAC to the array in little endian format (on-air)
static void get_mac_le(uint8_t* mac);
/// Check whether a packet's FCS matches that of a unicast frame
static bool is_packet_unicast(const void *buffer);
/// Check whether the packet has been unicast to our hardware MAC address
static const struct MacFrameNormal* is_packet_for_us(const void *buffer);
/// Reset the radio state
static void reset_radio(void);
/// Set the radio to idle state
static void idle_radio(void);

/// Check the OEPL checksum on a data packet (poll response or data block part)
static bool checksum_check(const void *p, const uint8_t len);
/// Calculate and set the OEPL checksum on a data packet (extended poll or block request)
static void checksum_add(void *p, const uint8_t len);

/// Callback function for the protocol timer
static void protocol_timer_cb(sl_sleeptimer_timer_handle_t *handle, void *data);
/// Callback function for the state timer
static void state_timer_cb(sl_sleeptimer_timer_handle_t *handle, void *data);

// -----------------------------------------------------------------------------
//                                Global Variables
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
//                                Static Variables
// -----------------------------------------------------------------------------

/// Array of channel numbers to include in scans
static const uint8_t channel_list[] = CHANNEL_LIST;

/// Receive buffer to copy to-be-handled packet
static uint8_t rx_buffer[128];
/// Transmit buffer for the RAIL layer to use as TX FIFO
static uint8_t tx_buffer[128];
/// Block buffer to assemble a data block which is received in parts from the AP
static uint8_t* datablock_buffer = NULL;
/// Packet handle (written by IRQ, cleared by the event loop)
static volatile RAIL_RxPacketHandle_t phandle = NULL;
/// State tracking for the radio state machine (internal)
static rx_state_t rx_state = OFF;
/// State tracking for the outside-visible radio process state
static oepl_radio_status_t radio_state = UNINITIALIZED;
/// Sequence number for outgoing packets
static uint8_t seqno = 0;
/// Channel index in \p channel_list which we are currently connected on
static uint8_t cur_channel_idx = 0;
/// Number of scans which have not yielded any APs since last being connected
static size_t num_empty_scans = 0;
/// Number of polls without response since the last successful poll
static size_t num_poll_timeouts = 0;
/// Number of successful sequential polls
static size_t num_polls = 0;
/// MAC of the AP with which we are assumedly connected
static uint8_t associated_ap[8];
/// LQI of the last received packet which was unicast to us
static uint8_t last_lqi;
/// RSSI of the last received packet which was unicast to us
static int8_t last_rssi;
/// Tick counter value (sleeptimer) of the last received packet which was unicast
static uint32_t last_packet_recv_ticks;
/// Tick counter value (sleeptimer) of the last time we sent a long poll
static uint32_t last_long_poll_sent_ticks;
/// Internal state tracking data (dependent on current state)
static state_data_t current_state_data;
/// Tracking value for whether the radio process is currently inhibiting sleep
static bool has_sleepblock = false;
/// Reason for which we are trying to send a poll
static uint8_t poll_reason = WAKEUP_REASON_TIMED;
static bool have_sent_reason = false;

// Protocol timer is responsible for flagging protocol timeouts, which are
// timeouts waiting for a radio response from another node.
/// Handle for the protocol timer
static sl_sleeptimer_timer_handle_t protocol_timer_handle;
/// Timer expiry flag for the protocol timer
static volatile bool protocol_timer_expired = false;

// State timer is responsible for progressing the state machine of the radio.
// It is handling e.g. data poll intervals, scan intervals, etc.
/// Handle for the state timer
static sl_sleeptimer_timer_handle_t state_timer_handle;
/// Timer expiry flag for the protocol timer
static volatile bool state_timer_expired = false;

/// Stored function pointer for notifying app of events
static oepl_radio_event_cb_t cb_fptr = NULL;

// -----------------------------------------------------------------------------
//                          Public Function Definitions
// -----------------------------------------------------------------------------

/**************************************************************************//**
 * (Re-)initialize the radio, register the event handler and start association.
 *****************************************************************************/
void oepl_radio_init(oepl_radio_event_cb_t cb, uint8_t reason, uint8_t channel)
{
  RAIL_Status_t rstat;
  if(cb == NULL) {
    // Radio needs to have a defined callback to call
    return;
  }

  // If the radio process was in use, reset it.
  if(rx_state != OFF) {
    reset_radio();
  }
  
  const oepl_efr32xg22_tagconfig_t* tagconfig = oepl_efr32xg22_get_config();
  
  // Setup PTI on devkits
  if(tagconfig->hwtype == BRD4402B_WSTK ||
     tagconfig->hwtype == BRD4402B_WSTK_EPD) {
    RAIL_PtiConfig_t railPtiConfig = {
      .mode = RAIL_PTI_MODE_UART,
      .baud = SL_RAIL_UTIL_PTI_BAUD_RATE_HZ,
  #if defined(SL_RAIL_UTIL_PTI_DOUT_PORT) && defined(SL_RAIL_UTIL_PTI_DOUT_PIN)
      .doutPort = (uint8_t)SL_RAIL_UTIL_PTI_DOUT_PORT,
      .doutPin = SL_RAIL_UTIL_PTI_DOUT_PIN,
    #ifdef SL_RAIL_UTIL_PTI_DOUT_LOC
      .doutLoc = SL_RAIL_UTIL_PTI_DOUT_LOC,
    #endif // SL_RAIL_UTIL_PTI_DOUT_LOC
  #endif // dout support
  #if defined(SL_RAIL_UTIL_PTI_DCLK_PORT) && defined(SL_RAIL_UTIL_PTI_DCLK_PIN)
      .dclkPort = (uint8_t)SL_RAIL_UTIL_PTI_DCLK_PORT,
      .dclkPin = SL_RAIL_UTIL_PTI_DCLK_PIN,
    #ifdef SL_RAIL_UTIL_PTI_DCLK_LOC
      .dclkLoc = SL_RAIL_UTIL_PTI_DCLK_LOC,
    #endif // SL_RAIL_UTIL_PTI_DCLK_LOC
  #endif // dclk support
  #if defined(SL_RAIL_UTIL_PTI_DFRAME_PORT) && defined(SL_RAIL_UTIL_PTI_DFRAME_PIN)
      .dframePort = (uint8_t)SL_RAIL_UTIL_PTI_DFRAME_PORT,
      .dframePin = SL_RAIL_UTIL_PTI_DFRAME_PIN,
    #ifdef SL_RAIL_UTIL_PTI_DFRAME_LOC
      .dframeLoc = SL_RAIL_UTIL_PTI_DFRAME_LOC,
    #endif // SL_RAIL_UTIL_PTI_DFRAME_LOC
  #endif // dframe support
    };

    RAIL_ConfigPti(sl_rail_util_get_handle(SL_RAIL_UTIL_HANDLE_INST0), &railPtiConfig);
  }

  RAIL_InitPowerManager();
  RAIL_ConfigSleep(sl_rail_util_get_handle(SL_RAIL_UTIL_HANDLE_INST0), RAIL_SLEEP_CONFIG_TIMERSYNC_DISABLED);

  // Set the TX FIFO (not done by the automatic initialisation)
  uint16_t txlen = RAIL_SetTxFifo(sl_rail_util_get_handle(SL_RAIL_UTIL_HANDLE_INST0), tx_buffer, 0, sizeof(tx_buffer));
  if(txlen != sizeof(tx_buffer)) {
    DPRINTF("TXFIFO %d\n", txlen);
  }

  DPRINTF("Max tick expression %ld ms\n", sl_sleeptimer_get_max_ms32_conversion());
  
  cb_fptr = cb;

  cur_channel_idx = sizeof(channel_list);
  num_empty_scans = 0;
  num_poll_timeouts = 0;
  num_polls = 0;
  last_packet_recv_ticks = 0;
  poll_reason = reason;
  memset(associated_ap, 0, sizeof(associated_ap));

  // Setup address filter to avoid overloading the RX chain
  rstat = RAIL_IEEE802154_SetPanId(sl_rail_util_get_handle(SL_RAIL_UTIL_HANDLE_INST0),
                                   PROTO_PAN_ID,
                                   0);
  if(rstat != RAIL_STATUS_NO_ERROR) {
    DPRINTF("PANID %08x\n", rstat);
  }
  rstat = RAIL_IEEE802154_SetLongAddress(sl_rail_util_get_handle(SL_RAIL_UTIL_HANDLE_INST0),
                                         (const uint8_t*)&DEVINFO->EUI64L,
                                         0);
  if(rstat != RAIL_STATUS_NO_ERROR) {
    DPRINTF("EUI %08x\n", rstat);
  }

  // Random delay to ensure tags don't all spam the radio channel on boot
  // when powered from the same power source
  uint8_t ranbyte;
  uint16_t ranlen = RAIL_GetRadioEntropy(sl_rail_util_get_handle(SL_RAIL_UTIL_HANDLE_INST0), &ranbyte, 1);

  // random delay between 0 and 2550 ms
  DPRINTF("Delaying radio by %dms\n", ranbyte * 10);
  state_timer_expired = false;
  sl_sleeptimer_start_timer_ms(&state_timer_handle,
                               ranbyte * 10,
                               state_timer_cb,
                               NULL, 0, SL_SLEEPTIMER_NO_HIGH_PRECISION_HF_CLOCKS_REQUIRED_FLAG);
  while(!state_timer_expired) {
    sl_power_manager_sleep();
  }

  state_timer_expired = false;

  // If we have a valid channel, try quick-resume
  for(size_t i = 0; i < sizeof(channel_list); i++) {
    if(channel_list[i] == channel) {
      DPRINTF("Trying fast-associate on channel %d\n", channel_list[i]);
      start_scan(QUICK_SCAN_ROUNDS, false, i);
      return;
    }
  }

  // Else, do a full scan
  start_scan(FORCED_SCAN_ROUNDS, false, sizeof(channel_list));
}

/**************************************************************************//**
 * This is the event loop function for the radio implementation. Call it for
 * each iteration of the main event loop such that it can process events.
 *****************************************************************************/
void oepl_radio_process(void)
{
  RAIL_RxPacketInfo_t packet_info;
  RAIL_RxPacketDetails_t packet_details;
  const struct MacFrameNormal* f = NULL;
  const uint8_t* payload;
  uint8_t payload_type;
  size_t payload_size;

  switch(rx_state) {
    case OFF:
      break;
    case AWAIT_TRIGGER:
    {
      idle_radio();

      if(state_timer_expired) {
        state_timer_expired = false;
        switch(radio_state) {
          case SEARCHING:
            DPRINTF("Scanning anew\n");
            start_scan(FORCED_SCAN_ROUNDS, false, sizeof(channel_list));
            break;
          case IDLE:
            DPRINTF("Polling\n");
            send_poll(channel_list[cur_channel_idx], true);
            break;
          default:
            DPRINTF("Timer expired but we wouldn't know why...\n");
            break;
        }
      }
      break;
    }
    case AWAIT_PONG:
    {
      bool pong_received = false;
      // Handle received packets first
      static const uint8_t expected_packettypes[] = {PKT_PONG};
      if(try_ingest_packet(expected_packettypes, sizeof(expected_packettypes), &f, &payload_type, &payload, &payload_size, &packet_info, &packet_details)) {
        DPRINTF("RX pong (len=%d, RSSI=%d dBm, LQI=%d, chan=%d) ", packet_info.packetBytes, packet_details.rssi, packet_details.lqi, packet_details.channel);

        if(payload_size == 1 && payload[0] == channel_list[current_state_data.searching.current_chidx]) {
          // If this AP has a stronger signal than what we've previously seen, store it.
          if(current_state_data.searching.highest_lqi < packet_details.lqi ||
            current_state_data.searching.highest_lqi_chidx == -1) {
            current_state_data.searching.highest_lqi = packet_details.lqi;
            current_state_data.searching.highest_rssi = packet_details.rssi;
            current_state_data.searching.highest_lqi_chidx = current_state_data.searching.current_chidx;
            memcpy(current_state_data.searching.higest_mac, f->src, 8);
            DPRINTF("\nStronger signal at ch%d with LQI %d", channel_list[current_state_data.searching.current_chidx], packet_details.lqi);
          }
          pong_received = true;
        }
        DPRINTF("\n");
      }

      if(protocol_timer_expired || pong_received) {
        // Go to next iteration of the scan or idle
        if((current_state_data.searching.remaining_channel_it > 0) && !pong_received) {
          current_state_data.searching.remaining_channel_it--;
          send_ping(channel_list[current_state_data.searching.current_chidx]);
        } else if(current_state_data.searching.current_chidx < sizeof(channel_list) - 1 &&
                  !current_state_data.searching.fast_associate) {
          // Hop to the next channel as long as we're not fast-associating
          current_state_data.searching.remaining_channel_it = MAX_PING_ROUNDS - 1;
          send_ping(channel_list[++current_state_data.searching.current_chidx]);
        } else {
          // Check if we need to scan once more
          if(current_state_data.searching.remaining_scan_it > 0) {
            if(!current_state_data.searching.fast_associate) {
              current_state_data.searching.current_chidx = 0;
            }
            current_state_data.searching.remaining_scan_it--;
            send_ping(channel_list[current_state_data.searching.current_chidx]);
          } else {
            // Let the app know we're done scanning and give the result.
            idle_radio();
            if(current_state_data.searching.highest_lqi_chidx < 0) {
              // No APs found during scan
              if(radio_state == SEARCHING) {
                // If we had already lost contact, schedule another round appropriately
                num_empty_scans++;
                schedule_next_scan();
                cb_fptr(SCAN_TIMEOUT, NULL);
              } else if(radio_state == ROAMING) {
                // If we were trying to roam, go back to idle as there is no need to change behaviour here.
                // If our currently-associated AP has gone down, we'll detect that as part of the polling cycle
                radio_state = IDLE;
              } else {
                // In any other case, this was a scan to try and find another AP after losing
                // contact with the one we had. If we didn't find any, we're SOL.
                radio_state = SEARCHING;
                num_empty_scans = 1;
                num_polls = 0;
                schedule_next_scan();
                cb_fptr(ORPHANED, NULL);
              }
            } else {
              // An AP was selected
              num_empty_scans = 0;
              num_poll_timeouts = 0;

              memcpy(associated_ap, current_state_data.searching.higest_mac, 8);
              // Let the AP on next poll know that we scanned
              if(poll_reason == WAKEUP_REASON_TIMED) {
                poll_reason = WAKEUP_REASON_NETWORK_SCAN;
              }
              
              if(radio_state == SEARCHING) {
                // If we were actively looking for any AP, let the app know we've found one.
                radio_state = IDLE;                
                cur_channel_idx = current_state_data.searching.highest_lqi_chidx;
                last_lqi = current_state_data.searching.highest_lqi;
                last_rssi = current_state_data.searching.highest_rssi;

                cb_fptr(ASSOCIATED, (void*)((uint32_t)channel_list[current_state_data.searching.highest_lqi_chidx]));
              } else {
                // If we were trying to roam, or find an alternate AP, check whether it would be appropriate to callback, then roam
                radio_state = IDLE;
                if(current_state_data.searching.highest_lqi_chidx != cur_channel_idx) {
                  cur_channel_idx = current_state_data.searching.highest_lqi_chidx;
                  last_lqi = current_state_data.searching.highest_lqi;
                  last_rssi = current_state_data.searching.highest_rssi;

                  cb_fptr(CHANNEL_ROAM, (void*)((uint32_t)channel_list[current_state_data.searching.highest_lqi_chidx]));
                }
              }

              // If the radio is still idle after processing the callback, send a poll packet
              if(rx_state == AWAIT_TRIGGER) {
                send_poll(channel_list[cur_channel_idx], false);
              } else if(rx_state != OFF) {
                schedule_next_poll(0);
              }
            }
          }
        }
      }
      break;
    }
    case AWAIT_DATAINFO:
    {
      // Handle received packets first
      static const uint8_t expected_packettypes[] = {PKT_AVAIL_DATA_INFO};
      if(try_ingest_packet(expected_packettypes, sizeof(expected_packettypes), &f, &payload_type, &payload, &payload_size, &packet_info, &packet_details)) {
        DPRINTF("RX ind (len=%d, RSSI=%d dBm, LQI=%d, chan=%d) ", packet_info.packetBytes, packet_details.rssi, packet_details.lqi, packet_details.channel);

        // Check it's a reply to our data poll. Ind packets are a normal packet with a struct payload
        if(payload_size == sizeof(struct AvailDataInfo)) {
          if(checksum_check(payload, sizeof(struct AvailDataInfo))) {
            oepl_radio_action_t cb_result;
            
            idle_radio();
            radio_state = IDLE;

            if(num_poll_timeouts >= POLL_INTERVAL_BASE_ATTEMPTS * 3) {
              // First say that we're connected now
              cb_fptr(ASSOCIATED, (void*)((uint32_t)channel_list[current_state_data.searching.highest_lqi_chidx]));
            }

            memcpy(&current_state_data.polling.result.AP_data, payload, sizeof(struct AvailDataInfo));
            memcpy(&current_state_data.polling.result.AP_MAC, f->src, 8);
            current_state_data.polling.result.AP_PAN = f->pan;

            if(current_state_data.polling.result.AP_data.dataType != DATATYPE_NOUPDATE) {
              cb_result = cb_fptr(AP_DATA, &current_state_data.polling.result);
            }
            
            DPRINTF("AvailDataInfo: ");
            for(size_t i = 0; i < sizeof(struct AvailDataInfo); i++) {
              DPRINTF("%02x", ((uint8_t*)&current_state_data.polling.result.AP_data)[i]);
            }
            DPRINTF("\n");

            // We've communicated the reason successfully since we got a reply. Revert
            // back to the regular reason since the next poll will be timed unless we
            // get another async event.
            if(poll_reason != WAKEUP_REASON_TIMED) {
              have_sent_reason = true;
              poll_reason = WAKEUP_REASON_TIMED;
            }
            
            // Reset the timeout counter
            num_poll_timeouts = 0;
            num_polls++;
            if(current_state_data.polling.result.AP_data.dataType == DATATYPE_NOUPDATE) {
              if(current_state_data.polling.result.AP_data.nextCheckIn >= 0x8000) {
                schedule_next_poll(current_state_data.polling.result.AP_data.nextCheckIn - 0x8000);
              } else {
                schedule_next_poll(current_state_data.polling.result.AP_data.nextCheckIn * 60);
              }
            } else {
              schedule_next_poll(0);
            }

            uint8_t enable_roaming;
            if((num_polls & 0x1F) == 0 && (oepl_nvm_setting_get(OEPL_ENABLE_TAGROAMING, &enable_roaming, sizeof(enable_roaming))) == NVM_SUCCESS && enable_roaming > 0) {
              oepl_radio_try_roam();
            }
            
            if(cb_result == ACTION_COMPLETED) {
              oepl_radio_acknowledge_action(f->src, f->pan);
            }
          } else {
            DPRINTF("Wrong checksum\n");
          }
        } else {
          DPRINTF("Payload size %d not expected\n", payload_size);
        }
      }

      if(protocol_timer_expired) {
        // Go to next iteration of the poll or idle
        if(current_state_data.polling.remaining_poll_it > 0) {
          current_state_data.polling.remaining_poll_it--;
          send_poll(channel_list[cur_channel_idx], !current_state_data.polling.has_payload);
        } else {
          idle_radio();
          radio_state = IDLE;
          cb_fptr(POLL_TIMEOUT, NULL);

          num_poll_timeouts++;
          schedule_next_poll(0);
        }
      }
      break;
    }
    case AWAIT_BLOCK:
    {
      // Handle received packets first
      static const uint8_t expected_packettypes[] = {PKT_BLOCK_PART};
      if(try_ingest_packet(expected_packettypes, sizeof(expected_packettypes), &f, &payload_type, &payload, &payload_size, &packet_info, &packet_details)) {
        DPRINTF("RX block part (len=%d, RSSI=%d dBm, LQI=%d, chan=%d) ", packet_info.packetBytes, packet_details.rssi, packet_details.lqi, packet_details.channel);

        if(payload_size >= sizeof(struct blockPart) + 99
           && checksum_check(payload, sizeof(struct blockPart) + 99)) {
          struct blockPart * bp = (struct blockPart *)payload;
          if(bp->blockId != current_state_data.blockreq.requested_block.idx) {
            DPRINTF("Received block data not in current block\n");
            // Todo: error handling
          } else if (bp->blockPart < 8 * sizeof(current_state_data.blockreq.remaining_parts_mask)) {
            if((current_state_data.blockreq.remaining_parts_mask[bp->blockPart/8] & (1 << (bp->blockPart % 8))) != 0) {
              DPRINTF("unseen part %d\n", bp->blockPart);
              size_t size_to_copy = bp->blockPart == 41 ? 41 : 99;
              memcpy(&datablock_buffer[bp->blockPart * 99], &payload[sizeof(struct blockPart)], size_to_copy);
              current_state_data.blockreq.remaining_parts_mask[bp->blockPart/8] &= ~(1 << (bp->blockPart % 8));
              for(size_t i = 0; i < sizeof(current_state_data.blockreq.remaining_parts_mask); i++) {
                if(current_state_data.blockreq.remaining_parts_mask[i] != 0) {
                  DPRINTF("Rem [");
                  for(size_t j = 0; j < sizeof(current_state_data.blockreq.remaining_parts_mask); j++) {
                    DPRINTF("%02X", current_state_data.blockreq.remaining_parts_mask[j]);
                  }
                  DPRINTF("]\n");
                  break;
                }
                if(i == sizeof(current_state_data.blockreq.remaining_parts_mask) - 1) {
                  oepl_radio_blockrecv_t blockdesc;
                  bool blockvalid = false;
                  struct blockData * bd = (struct blockData *)datablock_buffer;
                  blockdesc.block_index = current_state_data.blockreq.requested_block.idx;
                  blockdesc.block_size = bd->size;
                  blockdesc.block_data = &datablock_buffer[sizeof(struct blockData)];

                  if (bd->size > BLOCK_XFER_BUFFER_SIZE - sizeof(struct blockData)) {
                    DPRINTF("PROTO: Impossible data size; size = %d\n", bd->size);
                  } else {
                    uint16_t t = 0;
                    for (uint16_t c = 0; c < bd->size; c++) {
                      t += bd->data[c];
                    }
                    blockvalid = t == bd->checksum;
                    if(!blockvalid) {
                      DPRINTF("Checksum on block invalid, expected 0x%04x but calculated %04x\n", bd->checksum, t);
                    }
                  }

                  idle_radio();
                  radio_state = IDLE;

                  oepl_radio_action_t cb_result;
                  if(blockvalid) {
                    DPRINTF("Complete\n");
                    cb_result = cb_fptr(BLOCK_COMPLETE, &blockdesc);
                  } else {
                    DPRINTF("First block bytes 0x%02x 0x%02x 0x%02x 0x%02x\n", datablock_buffer[0], datablock_buffer[1], datablock_buffer[2], datablock_buffer[3]);
                    DPRINTF("Final block bytes 0x%02x 0x%02x 0x%02x 0x%02x\n", datablock_buffer[bd->size-4], datablock_buffer[bd->size-3], datablock_buffer[bd->size-2], datablock_buffer[bd->size-1]);
                    cb_result = cb_fptr(BLOCK_CANCELED, NULL);
                    if(rx_state != AWAIT_BLOCK && rx_state != AWAIT_BLOCKREQ_ACK) {
                      if(datablock_buffer) {
                        free(datablock_buffer);
                        datablock_buffer = NULL;
                      }
                    }
                  }
                  if(cb_result == ACTION_COMPLETED) {
                    oepl_radio_acknowledge_action(f->src, f->pan);
                  }
                }
              }
            } else {
              DPRINTF("Dup\n");
            }
          } else {
            DPRINTF("part outside of mask range\n");
          }
        }
      }

      if(protocol_timer_expired) {
        idle_radio();
        if(current_state_data.blockreq.retries > 0) {
          // retry with a partial block
          rx_state = AWAIT_BLOCK;
          current_state_data.blockreq.retries--;
          oepl_radio_request_datablock(current_state_data.blockreq.requested_block);
        } else {
          radio_state = IDLE;
          if(datablock_buffer) {
            free(datablock_buffer);
            datablock_buffer = NULL;
          }
          cb_fptr(BLOCK_TIMEOUT, NULL);
        }
      }
      break;
    }
    case AWAIT_BLOCKREQ_ACK:
    {
      // Handle received packets first
      static const uint8_t expected_packettypes[] = {PKT_BLOCK_REQUEST_ACK, PKT_CANCEL_XFER, PKT_BLOCK_PART};
      if(try_ingest_packet(expected_packettypes, sizeof(expected_packettypes), &f, &payload_type, &payload, &payload_size, &packet_info, &packet_details)) {
        switch(payload_type) {
          case PKT_BLOCK_REQUEST_ACK:
            idle_radio();
            
            if(payload_size >= sizeof(struct blockRequestAck)) {
                struct blockRequestAck *ack = (struct blockRequestAck *)payload;
              DPRINTF("Scheduling block RX in %d ms\n", ack->pleaseWaitMs);
              RAIL_Idle(sl_rail_util_get_handle(SL_RAIL_UTIL_HANDLE_INST0), RAIL_IDLE_ABORT, false);
              current_state_data.blockreq.ack_recv = true;

              // Manually ensure we end up in here again
              rx_state = AWAIT_BLOCKREQ_ACK;
              sl_sleeptimer_stop_timer(&protocol_timer_handle);
              protocol_timer_expired = false;
              sl_sleeptimer_start_timer_ms(&protocol_timer_handle,
                                           ack->pleaseWaitMs,
                                           protocol_timer_cb,
                                           NULL, 0, SL_SLEEPTIMER_NO_HIGH_PRECISION_HF_CLOCKS_REQUIRED_FLAG);
            } else {
              DPRINTF("Size mismatch for block request ack\n");
              radio_state = IDLE;
              cb_fptr(BLOCK_CANCELED, NULL);
            }
            break;
          case PKT_CANCEL_XFER:
            idle_radio();
            radio_state = IDLE;
            cb_fptr(BLOCK_CANCELED, NULL);
            break;
          case PKT_BLOCK_PART:
          {
            bool expect_more_blocks = true;
            if(payload_size >= sizeof(struct blockPart) + 99) {
              if(checksum_check(payload, sizeof(struct blockPart) + 99)) {
                struct blockPart * bp = (struct blockPart *)payload;
                if(bp->blockId != current_state_data.blockreq.requested_block.idx) {
                  DPRINTF("Received block data not in current block\n");
                  // Todo: error handling
                } else if (bp->blockPart < 42) {
                  if((current_state_data.blockreq.remaining_parts_mask[bp->blockPart/8] & (1 << (bp->blockPart % 8))) != 0) {
                    DPRINTF("unseen part %d\n", bp->blockPart);
                    size_t size_to_copy = bp->blockPart == 41 ? 41 : 99;
                    memcpy(&datablock_buffer[bp->blockPart * 99], &payload[sizeof(struct blockPart)], size_to_copy);
                    current_state_data.blockreq.remaining_parts_mask[bp->blockPart/8] &= ~(1 << (bp->blockPart % 8));
                    for(size_t i = 0; i < sizeof(current_state_data.blockreq.remaining_parts_mask); i++) {
                      if(current_state_data.blockreq.remaining_parts_mask[i] != 0) {
                        DPRINTF("Rem [");
                        for(size_t j = 0; j < sizeof(current_state_data.blockreq.remaining_parts_mask); j++) {
                          DPRINTF("%02X", current_state_data.blockreq.remaining_parts_mask[j]);
                        }
                        DPRINTF("]\n");
                        break;
                      }
                      if(i == sizeof(current_state_data.blockreq.remaining_parts_mask) - 1) {
                        expect_more_blocks = false;
                        oepl_radio_blockrecv_t blockdesc;
                        bool blockvalid = false;
                        struct blockData * bd = (struct blockData *)datablock_buffer;
                        blockdesc.block_index = current_state_data.blockreq.requested_block.idx;
                        blockdesc.block_size = bd->size;
                        blockdesc.block_data = &datablock_buffer[sizeof(struct blockData)];

                        if (bd->size > BLOCK_XFER_BUFFER_SIZE - sizeof(struct blockData)) {
                          DPRINTF("PROTO: Impossible data size; size = %d\n", bd->size);
                        } else {
                          uint16_t t = 0;
                          for (uint16_t c = 0; c < bd->size; c++) {
                            t += bd->data[c];
                          }
                          blockvalid = t == bd->checksum;
                          if(!blockvalid) {
                            DPRINTF("Checksum on block invalid, expected 0x%04x but calculated %04x\n", bd->checksum, t);
                          }
                        }

                        idle_radio();
                        radio_state = IDLE;

                        oepl_radio_action_t cb_result;
                        if(blockvalid) {
                          DPRINTF("Complete\n");
                          cb_result = cb_fptr(BLOCK_COMPLETE, &blockdesc);
                        } else {
                          DPRINTF("Checksum on block invalid after skipping blockreq ack\n");
                          DPRINTF("First block bytes 0x%02x 0x%02x 0x%02x 0x%02x\n", datablock_buffer[0], datablock_buffer[1], datablock_buffer[2], datablock_buffer[3]);
                          DPRINTF("Final block bytes 0x%02x 0x%02x 0x%02x 0x%02x\n", datablock_buffer[bd->size-4], datablock_buffer[bd->size-3], datablock_buffer[bd->size-2], datablock_buffer[bd->size-1]);
                          cb_result = cb_fptr(BLOCK_CANCELED, NULL);
                          if(rx_state != AWAIT_BLOCK && rx_state != AWAIT_BLOCKREQ_ACK) {
                            if(datablock_buffer) {
                              free(datablock_buffer);
                              datablock_buffer = NULL;
                            }
                          }
                        }
                        if(cb_result == ACTION_COMPLETED) {
                          oepl_radio_acknowledge_action(f->src, f->pan);
                        }
                      }
                    }
                  }
                }
              } else {
                DPRINTF("Block part checksum mismatch\n");
              }
            } else {
              DPRINTF("Size mismatch for block part\n");
            }

            if(expect_more_blocks) {
              rx_state = AWAIT_BLOCK;
              sl_sleeptimer_stop_timer(&protocol_timer_handle);
              protocol_timer_expired = false;
              sl_sleeptimer_start_timer_ms(&protocol_timer_handle,
                                            350,
                                            protocol_timer_cb,
                                            NULL, 0, SL_SLEEPTIMER_NO_HIGH_PRECISION_HF_CLOCKS_REQUIRED_FLAG);
            }

            break;
          }
          default:
            DPRINTF("packet type %02X should have been filtered\n", payload_type);
            NVIC_SystemReset();
        }
      }

      if(protocol_timer_expired && rx_state == AWAIT_BLOCKREQ_ACK) {
        protocol_timer_expired = false;
        if(current_state_data.blockreq.ack_recv) {
          // Re-enable RX
          RAIL_StartRx(sl_rail_util_get_handle(SL_RAIL_UTIL_HANDLE_INST0), channel_list[cur_channel_idx], NULL);
          rx_state = AWAIT_BLOCK;
          sl_sleeptimer_start_timer_ms(&protocol_timer_handle,
                                       350,
                                       protocol_timer_cb,
                                       NULL, 0, SL_SLEEPTIMER_NO_HIGH_PRECISION_HF_CLOCKS_REQUIRED_FLAG);
        } else {
          // Go to next iteration of the poll or idle
          idle_radio();
          if(current_state_data.blockreq.retries > 0) {
            rx_state = AWAIT_BLOCK;
            current_state_data.blockreq.retries--;
            oepl_radio_request_datablock(current_state_data.blockreq.requested_block);
          } else {
            radio_state = IDLE;
            if(datablock_buffer) {
              free(datablock_buffer);
              datablock_buffer = NULL;
            }
            cb_fptr(BLOCK_TIMEOUT, NULL);
          }
        }
      }
      break;
    }
    case AWAIT_TAGDATA_ACK:
      //Todo: implement tag data
      break;
    case AWAIT_XFER_END_ACK:
    {
      static const uint8_t expected_packettypes[] = {PKT_XFER_COMPLETE_ACK};
      if(try_ingest_packet(expected_packettypes, sizeof(expected_packettypes), &f, &payload_type, &payload, &payload_size, &packet_info, &packet_details)) {
        DPRINTF("RX confack (len=%d, RSSI=%d dBm, LQI=%d, chan=%d)\n", packet_info.packetBytes, packet_details.rssi, packet_details.lqi, packet_details.channel);

        DPRINTF("ACK recv");
        idle_radio();
        radio_state = IDLE;
        cb_fptr(CONFIRMATION_COMPLETE, NULL);
      }

      if(protocol_timer_expired) {
        idle_radio();
        if(current_state_data.confirm.retries > 0) {
          // Send another
          rx_state = AWAIT_XFER_END_ACK;
          current_state_data.confirm.retries--;
          oepl_radio_acknowledge_action(NULL, 0);
        } else {
          // Give up
          radio_state = IDLE;
          cb_fptr(CONFIRMATION_TIMEOUT, NULL);
        }
      }
      break;
    }
  }
  
  if(state_timer_expired) {
    switch(rx_state) {
      case AWAIT_BLOCK:
      case AWAIT_BLOCKREQ_ACK:
      case AWAIT_XFER_END_ACK:
        // We've been transferring for a long time, postpone once more
        schedule_next_poll(0);
        break;
      default:
        oepl_hw_crash(DBG_RADIO, true, "Unhandled state timer expiry in state %d %d\n", (uint8_t)rx_state, (uint8_t)radio_state);
    }
  }
}

/**************************************************************************//**
 * Get the radio process' state.
 *****************************************************************************/
oepl_radio_status_t oepl_radio_get_status(void)
{
  return radio_state;
}

/**************************************************************************//**
 * Send an early poll in reaction to an async event
 *****************************************************************************/
oepl_radio_error_t oepl_radio_send_poll_with_reason(uint8_t reason)
{
  poll_reason = reason;

  // If we currently have nothing to do (and think we're connected), send the
  // poll reason async.
  if(radio_state == IDLE && rx_state == AWAIT_TRIGGER) {
    send_poll(channel_list[cur_channel_idx], false);
  } else {
    DPRINTF("Blocking async poll in state %d %d\n", radio_state, rx_state);
  }

  // If we're currently disconnected, trigger a new scan round in hopes of
  // finding another AP and communicating our wakeup reason (probably button)
  if(radio_state == SEARCHING && rx_state == AWAIT_TRIGGER) {
    sl_sleeptimer_stop_timer(&state_timer_handle);
    state_timer_expired = false;
    start_scan(FORCED_SCAN_ROUNDS, false, sizeof(channel_list));
  }

  // Else, we'll have to piggyback on the next poll. Not going to interrupt
  // currently ongoing processes.
  return SUCCESS;
}

/**************************************************************************//**
 * Trigger a channel scan with the intention to roam
 *****************************************************************************/
oepl_radio_error_t oepl_radio_try_roam(void)
{
  if((radio_state == IDLE || radio_state == SEARCHING) && rx_state == AWAIT_TRIGGER) {
    start_scan(QUICK_SCAN_ROUNDS, true, sizeof(channel_list));
    return SUCCESS;
  } else {
    // Can't roam if not associated and idle
    return ERROR;
  }
}

/**************************************************************************//**
 * Send out-of-cycle tag information to the AP we're associated with.
 *****************************************************************************/
oepl_radio_error_t oepl_radio_send_tagdata(uint8_t* data, size_t length)
{
  // Todo: implement tag data
  (void) data;
  (void) length;
  return NOT_IMPLEMENTED;
}

/**************************************************************************//**
 * Request data block. This will start a block download and generate
 * BLOCK_PART_RECEIVED events as the block parts come in.
 * 
 * Reminder: A file is split in blocks of 4096 bytes, and each block is
 * transfered in parts which fit in the PHY's PSDU.
 *****************************************************************************/
oepl_radio_error_t oepl_radio_request_datablock(oepl_datablock_descriptor_t db)
{
  if(rx_state != OFF && rx_state != AWAIT_TRIGGER
     && rx_state != AWAIT_BLOCKREQ_ACK && rx_state != AWAIT_BLOCK) {
    return ERROR;
  }

  sl_sleeptimer_stop_timer(&protocol_timer_handle);
  protocol_timer_expired = false;

  size_t blocks_in_file = db.file.filesize / 4096;
  if(db.file.filesize % 4096) {
    blocks_in_file++;
  }
  size_t blocksize = db.idx < blocks_in_file - 1 ? 4096 : db.file.filesize - (db.idx * 4096);
  size_t blockparts = blocksize / 99;
  if(blocksize % 99) {
    blockparts++;
  }

  DPRINTF("Request block %ld of %ld bytes, block size %ld in %ld parts\n", db.idx, db.file.filesize, blocksize, blockparts);

  RAIL_Handle_t rail_handle = sl_rail_util_get_handle(SL_RAIL_UTIL_HANDLE_INST0);
  uint8_t packet[1 + sizeof(struct MacFrameNormal) + 1 + sizeof(struct blockRequest)];
  struct MacFrameNormal *f = (struct MacFrameNormal *)(&packet[1]);
  struct blockRequest *blockreq = (struct blockRequest *)(&packet[1 + sizeof(struct MacFrameNormal) + 1]);
  memset(packet, 0, sizeof(packet));
  packet[0] = sizeof(struct MacFrameNormal) + 1 + sizeof(struct blockRequest) + 3; // Todo: figure out why the extra byte is required
  packet[1 + sizeof(struct MacFrameNormal)] = db.idx == blocks_in_file - 1 ? PKT_BLOCK_PARTIAL_REQUEST : PKT_BLOCK_REQUEST;
  get_mac_le(f->src);
  memcpy(f->dst, db.file.ap, 8);
  f->fcs.frameType = 1;
  f->fcs.secure = 0;
  f->fcs.framePending = 0;
  f->fcs.ackReqd = 0;
  f->fcs.panIdCompressed = 1;
  f->fcs.destAddrType = 3;
  f->fcs.frameVer = 0;
  f->fcs.srcAddrType = 3;
  f->seq = seqno++;
  f->pan = db.file.ap_pan;

  blockreq->blockId = db.idx;
  blockreq->type = db.file.type;
  memcpy(&blockreq->ver, db.file.id, 8);

  if(rx_state != AWAIT_BLOCKREQ_ACK && rx_state != AWAIT_BLOCK) {
    if(datablock_buffer == NULL) {
      datablock_buffer = malloc(sizeof(struct blockData) + 4096);
      if(datablock_buffer == NULL) {
        DPRINTF("Error: couldn't allocate buffer\n");
        return ERROR;
      }
    }
    radio_state = DOWNLOADING;
    rx_state = AWAIT_BLOCKREQ_ACK;

    packet[1 + sizeof(struct MacFrameNormal)] = PKT_BLOCK_REQUEST;
    current_state_data.blockreq.ack_recv = false;
    current_state_data.blockreq.retries = 10;
    memcpy(&current_state_data.blockreq.requested_block, &db, sizeof(current_state_data.blockreq.requested_block));
    memset(current_state_data.blockreq.remaining_parts_mask, 0, sizeof(current_state_data.blockreq.remaining_parts_mask));
    for(size_t i = 0; i < blockparts;) {
      if(blockparts - i >= 8) {
        current_state_data.blockreq.remaining_parts_mask[i/8] = 0xFF;
        i += 8;
      } else {
        current_state_data.blockreq.remaining_parts_mask[i/8] |= 1 << (i % 8);
        i += 1;
      }
    }
  } else if(rx_state == AWAIT_BLOCK) {
    DPRINTF("RB %d []", current_state_data.blockreq.retries);
    for(size_t i = 0; i < sizeof(current_state_data.blockreq.remaining_parts_mask); i++) {
      DPRINTF("%02X", current_state_data.blockreq.remaining_parts_mask[i]);
    }
    DPRINTF("]\n");
    rx_state = AWAIT_BLOCKREQ_ACK;
    packet[1 + sizeof(struct MacFrameNormal)] = PKT_BLOCK_PARTIAL_REQUEST;
  } else {
    oepl_hw_crash(DBG_RADIO, true, "Trying to request a block while waiting on block ack\n");
  }
  
  memcpy(&blockreq->requestedParts, current_state_data.blockreq.remaining_parts_mask, sizeof(blockreq->requestedParts));
  checksum_add(blockreq, sizeof(struct blockRequest));

  uint16_t wrlen = RAIL_WriteTxFifo(rail_handle,
                                    packet,
                                    packet[0] + 1,
                                    true);
  if(wrlen != packet[0] + 1) {
    DPRINTF("TXWR %08x\n", wrlen);
    return ERROR;
  }

  RAIL_Status_t rstat = RAIL_StartTx(rail_handle,
                                     channel_list[cur_channel_idx],
                                     false,
                                     NULL);
  if(rstat != RAIL_STATUS_NO_ERROR) {
    DPRINTF("TXERR %08x\n", rstat);
    return ERROR;
  }

  if(!has_sleepblock) {
    sl_power_manager_add_em_requirement(SL_POWER_MANAGER_EM1);
    has_sleepblock = true;
  }

  if(packet[1 + sizeof(struct MacFrameNormal)] == PKT_BLOCK_PARTIAL_REQUEST) {
    DPRINTF("Partial ");
  }

  DPRINTF("Block request started\n");
  sl_sleeptimer_start_timer_ms(&protocol_timer_handle,
                               350,
                               protocol_timer_cb,
                               NULL, 0, SL_SLEEPTIMER_NO_HIGH_PRECISION_HF_CLOCKS_REQUIRED_FLAG);

  return SUCCESS;
}

oepl_radio_error_t oepl_radio_release_datablock(void)
{
  if(rx_state == AWAIT_BLOCK || rx_state == AWAIT_BLOCKREQ_ACK) {
    // Can't free resources currently in use
    return ERROR;
  }

  if(datablock_buffer != NULL) {
    free(datablock_buffer);
    datablock_buffer = NULL;
  }
  return SUCCESS;
}

oepl_radio_error_t oepl_radio_acknowledge_action(const uint8_t AP_MAC[8], uint16_t AP_PAN)
{
  // ACK can only happen in idle state (i.e. internal processing in the await state
  // has concluded) or as a retry of an ongoing ack.
  if(rx_state != OFF && rx_state != AWAIT_TRIGGER && rx_state != AWAIT_XFER_END_ACK) {
    return ERROR;
  }

  sl_sleeptimer_stop_timer(&protocol_timer_handle);
  protocol_timer_expired = false;

  RAIL_Handle_t rail_handle = sl_rail_util_get_handle(SL_RAIL_UTIL_HANDLE_INST0);
  uint8_t packet[1 + sizeof(struct MacFrameNormal) + 1];
  struct MacFrameNormal *f = (struct MacFrameNormal *)(&packet[1]);
  memset(packet, 0, sizeof(packet));
  packet[0] = sizeof(struct MacFrameNormal) + 1 + 3;
  packet[1 + sizeof(struct MacFrameNormal)] = PKT_XFER_COMPLETE;
  get_mac_le(f->src);
  f->fcs.frameType = 1;
  f->fcs.secure = 0;
  f->fcs.framePending = 0;
  f->fcs.ackReqd = 0;
  f->fcs.panIdCompressed = 1;
  f->fcs.destAddrType = 3;
  f->fcs.frameVer = 0;
  f->fcs.srcAddrType = 3;
  f->seq = seqno++;

  if(rx_state != AWAIT_XFER_END_ACK) {
    radio_state = CONFIRMING;
    rx_state = AWAIT_XFER_END_ACK;

    current_state_data.confirm.retries = 16;
    current_state_data.confirm.AP_PAN = AP_PAN;
    memcpy(current_state_data.confirm.AP_MAC, AP_MAC, 8);
  } else {
    DPRINTF("RA %d\n", current_state_data.confirm.retries);
  }

  memcpy(f->dst, current_state_data.confirm.AP_MAC, 8);
  f->pan = current_state_data.confirm.AP_PAN;

  uint16_t wrlen = RAIL_WriteTxFifo(rail_handle,
                                    packet,
                                    packet[0] + 1,
                                    true);
  if(wrlen != packet[0] + 1) {
    DPRINTF("TXWR %08x\n", wrlen);
    return ERROR;
  }

  RAIL_Status_t rstat = RAIL_StartTx(rail_handle,
                                     channel_list[cur_channel_idx],
                                     false,
                                     NULL);
  if(rstat != RAIL_STATUS_NO_ERROR) {
    DPRINTF("TXERR %08x\n", rstat);
    return ERROR;
  }

  if(!has_sleepblock) {
    sl_power_manager_add_em_requirement(SL_POWER_MANAGER_EM1);
    has_sleepblock = true;
  }

  DPRINTF("Confirmation sent\n");
  protocol_timer_expired = false;
  sl_sleeptimer_start_timer_ms(&protocol_timer_handle,
                                10,
                                protocol_timer_cb,
                                NULL, 0, SL_SLEEPTIMER_NO_HIGH_PRECISION_HF_CLOCKS_REQUIRED_FLAG);

  return SUCCESS;
}

/******************************************************************************
 * RAIL callback, called if a RAIL event occurs
 *****************************************************************************/
void sl_rail_util_on_event(RAIL_Handle_t rail_handle, RAIL_Events_t events)
{
  ///////////////////////////////////////////////////////////////////////////
  // Put your RAIL event handling here!                                    //
  // This is called from ISR context.                                      //
  // Do not call blocking functions from here!                             //
  ///////////////////////////////////////////////////////////////////////////

  if(events & RAIL_EVENT_RX_PACKET_RECEIVED) {
    //Todo: Check if we can easily prefilter packets
    RAIL_RxPacketInfo_t info;
    RAIL_RxPacketHandle_t handle = RAIL_HoldRxPacket(rail_handle);
    RAIL_GetRxPacketInfo(sl_rail_util_get_handle(SL_RAIL_UTIL_HANDLE_INST0), handle, &info);
    if(info.packetStatus == RAIL_RX_PACKET_READY_SUCCESS && info.packetBytes > 4) {
      RAIL_RxPacketDetails_t details;
      RAIL_GetRxPacketDetails(rail_handle, handle, &details);
      if(!details.isAck && phandle == NULL) {
        phandle = handle;
      } else {
        RAIL_ReleaseRxPacket(rail_handle, handle);
      }
    } else {
      RAIL_ReleaseRxPacket(rail_handle, handle);
    }
  }
  events &= ~RAIL_EVENT_RX_PACKET_RECEIVED;
  if(events != 0) {
    #if RADIO_DEBUG_PRINT_IN_IRQ
    DPRINTF("r");
    DPRINTF("[");
    for(size_t i = 0; i < 64; i++) {
      if(events & (1 << i)) {
        DPRINTF("%d ",i);
      }
    }
    DPRINTF("]");
    #endif
  }

#if defined(SL_CATALOG_KERNEL_PRESENT)
  app_task_notify();
#endif
}

bool oepl_radio_is_event_pending(void)
{
  // Async actions we might be waiting to process:
  // - Timer expiry
  // - packet RX
  return state_timer_expired || protocol_timer_expired || (phandle != NULL);
}

void oepl_radio_get_mac(uint8_t mac[8])
{
  get_mac_be(mac);
}

bool oepl_radio_get_ap_link(uint8_t* channel, uint8_t AP_mac[8], uint8_t* lqi, int8_t* rssi)
{
  if(radio_state > SEARCHING) {
    *channel = channel_list[cur_channel_idx];
    memcpy(AP_mac, associated_ap, 8);
    *lqi = last_lqi;
    *rssi = last_rssi;
    return true;
  } else {
    return false;
  }
}

// -----------------------------------------------------------------------------
//                          Static Function Definitions
// -----------------------------------------------------------------------------

static bool is_packet_unicast(const void *buffer)
{
  const struct MacFcs *fcs = (struct MacFcs *)buffer;
  if ((fcs->frameType == 1) && (fcs->destAddrType == 2) && (fcs->srcAddrType == 3) && (fcs->panIdCompressed == 0)) {
    return false;
  } else if ((fcs->frameType == 1) && (fcs->destAddrType == 3) && (fcs->srcAddrType == 3) && (fcs->panIdCompressed == 1)) {
    // normal frame
    return true;
  }
  // unknown type...
  return false;
}

static const struct MacFrameNormal* is_packet_for_us(const void *buffer) {
  if(!is_packet_unicast(buffer)) {
    return NULL;
  }
  const struct MacFrameNormal *f = (struct MacFrameNormal *)buffer;
  if(memcmp((const uint8_t*)&DEVINFO->EUI64L, f->dst, 8) == 0) {
    return f;
  } else {
    return NULL;
  }
}

static void start_scan(uint8_t rounds, bool is_roam, size_t fast_associate_idx)
{
  if(rx_state != OFF && rx_state != AWAIT_TRIGGER) {
    return;
  }

  // Move radio state
  if(radio_state != POLLING) {
    radio_state = is_roam ? ROAMING: SEARCHING;
  }
  if(fast_associate_idx < sizeof(channel_list)) {
    current_state_data.searching.current_chidx = fast_associate_idx;
    current_state_data.searching.fast_associate = true;
  } else {
    current_state_data.searching.current_chidx = 0;
    current_state_data.searching.fast_associate = false;
  }
  current_state_data.searching.highest_lqi = 0;
  current_state_data.searching.highest_lqi_chidx = -1;
  current_state_data.searching.remaining_channel_it = MAX_PING_ROUNDS - 1;
  current_state_data.searching.remaining_scan_it = rounds - 1;

  // Send first ping
  send_ping(channel_list[current_state_data.searching.current_chidx]);
}

static void start_rx(uint8_t channel)
{
  RAIL_Status_t rstat = RAIL_StartRx(sl_rail_util_get_handle(SL_RAIL_UTIL_HANDLE_INST0), channel, NULL);
  rx_state = AWAIT_TRIGGER;
  DPRINTF("Radio RX enable on channel %d: %d\n", channel, rstat);
}

static void stop_rx(void)
{
  RAIL_Idle(sl_rail_util_get_handle(SL_RAIL_UTIL_HANDLE_INST0), RAIL_IDLE_ABORT, false);
  rx_state = OFF;
  DPRINTF("Radio RX disabled\n");
}

static bool try_ingest_packet(
  const uint8_t* expected_packettypes, size_t num_packettypes,
  const struct MacFrameNormal** f, uint8_t* payload_type,
  const uint8_t** payload, size_t* payload_size,
  RAIL_RxPacketInfo_t* packet_info, RAIL_RxPacketDetails_t* packet_details)
{
  bool success = false;
  if(phandle == NULL) {
    return false;
  }

  phandle = RAIL_GetRxPacketInfo(sl_rail_util_get_handle(SL_RAIL_UTIL_HANDLE_INST0), phandle, packet_info);
  
  if(phandle == RAIL_RX_PACKET_HANDLE_INVALID) {
    DPRINTF("Invalid handle\n");
    return false;
  }

  if(packet_info->packetBytes > sizeof(rx_buffer)) {
    DPRINTF("Packet too big, %d > %d\n", packet_info->packetBytes, sizeof(rx_buffer));
    goto done;
  }

  if(packet_info->packetStatus != RAIL_RX_PACKET_READY_SUCCESS) {
    DPRINTF("Ignoring packet with malformed CRC\n");
    goto done;
  }

  RAIL_CopyRxPacket(rx_buffer, packet_info);
  if(packet_info->packetBytes != rx_buffer[0] - 1) {
    DPRINTF("Mismatch FHR\n");
    goto done;
  }

  RAIL_GetRxPacketDetails(sl_rail_util_get_handle(SL_RAIL_UTIL_HANDLE_INST0), phandle, packet_details);
  success = true;

done:
  RAIL_ReleaseRxPacket(sl_rail_util_get_handle(SL_RAIL_UTIL_HANDLE_INST0), phandle);
  phandle = NULL;

  if(success) {
    if((*f = is_packet_for_us(&rx_buffer[1])) != NULL
       && packet_info->packetBytes >= 1 + sizeof(struct MacFrameNormal) + 1) {
      for(size_t i = 0; i < num_packettypes; i++) {
        if(rx_buffer[1 + sizeof(struct MacFrameNormal)] == expected_packettypes[i]) {
          *payload_type = rx_buffer[1 + sizeof(struct MacFrameNormal)];
          *payload = &rx_buffer[1 + sizeof(struct MacFrameNormal) + 1];
          *payload_size = packet_info->packetBytes - 1 - sizeof(struct MacFrameNormal) - 1;
          last_packet_recv_ticks = sl_sleeptimer_get_tick_count();
          last_lqi = packet_details->lqi;
          last_rssi = packet_details->rssi;
          return true;
        }
      }
      DPRINTF("Unexpected packet type %02x\n", rx_buffer[1 + sizeof(struct MacFrameNormal)]);
      return false;
    } else {
      DPRINTF("Drop pkt len %d chan %d\n", packet_info->packetBytes, packet_details->channel);
      return false;
    }
  } else {
    return false;
  }
}

static void send_ping(uint8_t channel)
{
  if(rx_state != OFF && rx_state != AWAIT_TRIGGER && rx_state != AWAIT_PONG) {
    return;
  }

  RAIL_Handle_t rail_handle = sl_rail_util_get_handle(SL_RAIL_UTIL_HANDLE_INST0);
  uint8_t packet[1 + sizeof(struct MacFrameBcast) + 1 + 2];
  struct MacFrameBcast *txframe = (struct MacFrameBcast *)(&packet[1]);
  memset(packet, 0, sizeof(packet));
  packet[0] = sizeof(struct MacFrameBcast) + 1 + 2;
  packet[1 + sizeof(struct MacFrameBcast)] = PKT_PING;
  get_mac_le(txframe->src);
  txframe->fcs.frameType = 1;
  txframe->fcs.ackReqd = 1;
  txframe->fcs.destAddrType = 2;
  txframe->fcs.srcAddrType = 3;
  txframe->seq = seqno++;
  txframe->dstPan = PROTO_PAN_ID;
  txframe->dstAddr = 0xFFFF;
  txframe->srcPan = PROTO_PAN_ID;

  uint16_t wrlen = RAIL_WriteTxFifo(rail_handle,
                                    packet,
                                    sizeof(packet),
                                    true);
  if(wrlen != sizeof(packet)) {
    DPRINTF("TXWR %08x\n", wrlen);
    return;
  }

  RAIL_Status_t rstat = RAIL_StartTx(rail_handle,
                                     channel,
                                     RAIL_TX_OPTION_WAIT_FOR_ACK,
                                     NULL);
  if(rstat != RAIL_STATUS_NO_ERROR) {
    DPRINTF("TXERR %08x\n", rstat);
    return;
  }

  if(!has_sleepblock) {
    sl_power_manager_add_em_requirement(SL_POWER_MANAGER_EM1);
    has_sleepblock = true;
  }

  rx_state = AWAIT_PONG;
  sl_sleeptimer_stop_timer(&protocol_timer_handle);
  protocol_timer_expired = false;
  sl_sleeptimer_start_timer_ms(&protocol_timer_handle,
                                PONG_TIMEOUT_MS,
                                protocol_timer_cb,
                                NULL, 0, SL_SLEEPTIMER_NO_HIGH_PRECISION_HF_CLOCKS_REQUIRED_FLAG);
}

static void send_poll(uint8_t channel, bool is_short)
{
  if(rx_state != OFF && rx_state != AWAIT_TRIGGER && rx_state != AWAIT_DATAINFO) {
    DPRINTF("Blocking poll in state %d\n", rx_state);
    return;
  }

  if(is_short) {
    uint32_t current_ticks = sl_sleeptimer_get_tick_count();
    // Send a long poll when it has been 10 minutes since the last one
    if(sl_sleeptimer_tick_to_ms(current_ticks - last_long_poll_sent_ticks) > 600000) {
      is_short = false;
    }
  }

  if(have_sent_reason) {
    is_short = false;
    have_sent_reason = false;
  }

  sl_sleeptimer_stop_timer(&protocol_timer_handle);
  protocol_timer_expired = false;

  RAIL_Handle_t rail_handle = sl_rail_util_get_handle(SL_RAIL_UTIL_HANDLE_INST0);
  uint8_t packet[1 + sizeof(struct MacFrameBcast) + sizeof(struct AvailDataReq) + 4 + 2];
  struct MacFrameBcast *txframe = (struct MacFrameBcast *)(&packet[1]);
  memset(packet, 0, sizeof(packet));
  packet[0] = sizeof(struct MacFrameBcast) + 1 + (is_short ? 2 : sizeof(struct AvailDataReq) + 2 + 1); // Todo: figure out why the extra byte is required
  packet[1 + sizeof(struct MacFrameBcast)] = is_short ? PKT_AVAIL_DATA_SHORTREQ : PKT_AVAIL_DATA_REQ;
  get_mac_le(txframe->src);
  txframe->fcs.frameType = 1;
  txframe->fcs.ackReqd = 1;
  txframe->fcs.destAddrType = 2;
  txframe->fcs.srcAddrType = 3;
  txframe->seq = seqno++;
  txframe->dstPan = PROTO_PAN_ID;
  txframe->dstAddr = 0xFFFF;
  txframe->srcPan = PROTO_PAN_ID;

  if(rx_state != AWAIT_DATAINFO) {
    DPRINTF("Poll\n");
    radio_state = POLLING;
    rx_state = AWAIT_DATAINFO;
    current_state_data.polling.remaining_poll_it = MAX_POLL_ROUNDS - 1;
    current_state_data.polling.has_payload = !is_short;
    if(!is_short) {
      struct AvailDataReq *availreq = (struct AvailDataReq*)current_state_data.polling.poll_payload;
      availreq->hwType = oepl_hw_get_hwid();
      availreq->wakeupReason = poll_reason;
      availreq->lastPacketRSSI = last_rssi;
      availreq->lastPacketLQI = last_lqi;
      oepl_hw_get_temperature(&availreq->temperature);
      uint16_t voltage;
      oepl_hw_get_voltage(&voltage, false);
      availreq->batteryMv = voltage;
      availreq->capabilities = oepl_hw_get_capabilities();
      availreq->currentChannel = channel_list[cur_channel_idx];
      availreq->tagSoftwareVersion = oepl_hw_get_swversion();
      checksum_add(availreq, sizeof(struct AvailDataReq));
    }
  } else {
    DPRINTF("RP %d\n", current_state_data.polling.remaining_poll_it);
  }

  if(!is_short) {
    // Add tag info
    memcpy(&packet[1+sizeof(struct MacFrameBcast)+1], current_state_data.polling.poll_payload, sizeof(struct AvailDataReq));
  }

  uint16_t wrlen = RAIL_WriteTxFifo(rail_handle,
                                    packet,
                                    packet[0] + 1,
                                    true);
  if(wrlen != packet[0] + 1) {
    DPRINTF("TXWR %08x\n", wrlen);
    return;
  }

  RAIL_Status_t rstat = RAIL_StartTx(rail_handle,
                                     channel,
                                     false,
                                     NULL);
  if(rstat != RAIL_STATUS_NO_ERROR) {
    DPRINTF("TXERR %08x\n", rstat);
    return;
  }

  if(!has_sleepblock) {
    sl_power_manager_add_em_requirement(SL_POWER_MANAGER_EM1);
    has_sleepblock = true;
  }

  DPRINTF("%s poll started\n", is_short ? "Short" : "Long");
  if(!is_short) {
    last_long_poll_sent_ticks = sl_sleeptimer_get_tick_count();
  }
  sl_sleeptimer_start_timer_ms(&protocol_timer_handle,
                                POLL_TIMEOUT_MS,
                                protocol_timer_cb,
                                NULL, 0, SL_SLEEPTIMER_NO_HIGH_PRECISION_HF_CLOCKS_REQUIRED_FLAG);
}

static void get_mac_be(uint8_t* mac)
{
  uint32_t part_eui = __REV(DEVINFO->EUI64L);
  memcpy(&mac[4], &part_eui, 4);
  part_eui = __REV(DEVINFO->EUI64H);
  memcpy(mac, &part_eui, 4);
}

static void get_mac_le(uint8_t* mac)
{
  memcpy(mac, (const uint8_t*)&DEVINFO->EUI64L, 8);
}

static void reset_radio(void)
{
  sl_sleeptimer_stop_timer(&protocol_timer_handle);
  protocol_timer_expired = false;
  sl_sleeptimer_stop_timer(&state_timer_handle);
  state_timer_expired = false;
  // Todo: cancel transfers if in progress
  stop_rx();
}

static void schedule_next_scan(void)
{
  sl_sleeptimer_stop_timer(&state_timer_handle);
  state_timer_expired = false;

  uint32_t seconds;
  if (current_state_data.searching.fast_associate &&
      num_empty_scans == 1) {
    // Retry with a full scan in 10 seconds
    seconds = 10;
  } else if(num_empty_scans < INTERVAL_1_ATTEMPTS) {
    seconds = INTERVAL_1_TIME;
  } else if(num_empty_scans < INTERVAL_2_ATTEMPTS) {
    seconds = INTERVAL_2_TIME;
  } else {
    seconds = INTERVAL_3_TIME;
  }

  DPRINTF("Next scan in %ld seconds\n", seconds);
  sl_sleeptimer_start_timer_ms(&state_timer_handle,
                               seconds * 1000,
                               state_timer_cb,
                               NULL, 0, SL_SLEEPTIMER_NO_HIGH_PRECISION_HF_CLOCKS_REQUIRED_FLAG);
}

static void schedule_next_poll(size_t timeout_s)
{
  sl_sleeptimer_stop_timer(&state_timer_handle);
  state_timer_expired = false;
  uint8_t scan_after_timeout = 0;
  uint16_t checkin_time;

  if(timeout_s == 0) {
    if(oepl_nvm_setting_get(OEPL_MIN_CHECKIN_INTERVAL_S, &checkin_time, sizeof(checkin_time)) != NVM_SUCCESS) {
      checkin_time = POLL_INTERVAL_BASE_TIME;
    }

    if(checkin_time < POLL_INTERVAL_BASE_TIME) {
      checkin_time = POLL_INTERVAL_BASE_TIME;
    }

    if(num_poll_timeouts < POLL_INTERVAL_BASE_ATTEMPTS) {
      timeout_s = checkin_time;
    } else if(num_poll_timeouts < POLL_INTERVAL_BASE_ATTEMPTS * 2) {
      timeout_s = checkin_time * 2;
    } else if(num_poll_timeouts < POLL_INTERVAL_BASE_ATTEMPTS * 3) {
      timeout_s = checkin_time * 3;
    } else {
      // Too long without contact, scan and reattach or become orphan
      if(oepl_nvm_setting_get(OEPL_ENABLE_AUTOSCAN_ON_ORPHAN, &scan_after_timeout, sizeof(scan_after_timeout)) != NVM_SUCCESS) {
        scan_after_timeout = 1;
      }

      if(scan_after_timeout > 0) {
        radio_state = POLLING;
        start_scan(QUICK_SCAN_ROUNDS, true, sizeof(channel_list));
        return;
      } else if (num_poll_timeouts == POLL_INTERVAL_BASE_ATTEMPTS * 3) {
        // Notify the application we've become an orphan, but else continue scanning
        cb_fptr(ORPHANED, NULL);
      }
      timeout_s = checkin_time * 3;
    }
  }

  DPRINTF("Next poll in %ds\n", timeout_s);
  sl_sleeptimer_start_timer_ms(&state_timer_handle,
                               timeout_s * 1000,
                               state_timer_cb,
                               NULL, 0, SL_SLEEPTIMER_NO_HIGH_PRECISION_HF_CLOCKS_REQUIRED_FLAG);
}

static void protocol_timer_cb(sl_sleeptimer_timer_handle_t *handle, void *data)
{
  (void) handle;
  (void) data;
  protocol_timer_expired = true;
}

static void state_timer_cb(sl_sleeptimer_timer_handle_t *handle, void *data)
{
  (void) handle;
  (void) data;
  state_timer_expired = true;
}

static bool checksum_check(const void *p, const uint8_t len) {
  uint8_t total = 0;
  for (uint8_t c = 1; c < len; c++) {
    total += ((uint8_t *)p)[c];
  }
  return ((uint8_t *)p)[0] == total;
}

static void checksum_add(void *p, const uint8_t len) {
  uint8_t total = 0;
  for (uint8_t c = 1; c < len; c++) {
    total += ((uint8_t *)p)[c];
  }
  ((uint8_t *)p)[0] = total;
}

static void idle_radio(void)
{
  sl_sleeptimer_stop_timer(&protocol_timer_handle);
  protocol_timer_expired = false;

  // Don't care about packets received here, since tags don't accept unsolicited messages...
  if((RAIL_GetRadioState(sl_rail_util_get_handle(SL_RAIL_UTIL_HANDLE_INST0)) & RAIL_RF_STATE_IDLE) == 0) {
    // First ensure no more packet IRQs
    RAIL_Idle(sl_rail_util_get_handle(SL_RAIL_UTIL_HANDLE_INST0), RAIL_IDLE_ABORT, true);
  }

  if(phandle != NULL) {
    // Clear out straggling packet
    RAIL_ReleaseRxPacket(sl_rail_util_get_handle(SL_RAIL_UTIL_HANDLE_INST0), phandle);
    phandle = NULL;
  }

  if(has_sleepblock) {
    sl_power_manager_remove_em_requirement(SL_POWER_MANAGER_EM1);
    has_sleepblock = false;
  }

  rx_state = AWAIT_TRIGGER;
}
