#ifndef OEPL_RADIO_H
#define OEPL_RADIO_H

// -----------------------------------------------------------------------------
//                                   Includes
// -----------------------------------------------------------------------------
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "oepl-proto.h"
#include "oepl-definitions.h"

// -----------------------------------------------------------------------------
//                              Macros and Typedefs
// -----------------------------------------------------------------------------
typedef enum {
  UNINITIALIZED,    // Radio init has not yet been called
  SEARCHING,        // Radio is searching for an AP to link with
  ROAMING,          // Radio is performing a scan to see whether it can roam
  IDLE,             // Radio is linked with an AP and is idle
  POLLING,          // Radio is currently polling for new data
  DOWNLOADING,      // Radio is currently downloading data
  UPLOADING,        // Radio is currently uploading data
  CONFIRMING        // Radio is currently sending a transfer complete confirmation
} oepl_radio_status_t;

typedef enum {
  ASSOCIATED,             /// The radio has found and successfully connected with an AP
  ORPHANED,               /// The radio has been unable to find, or unable to re-establish communication with a previously found AP
  AP_DATA,                /// A non-empty data indication was received in response to a data poll (event_data = pointer to oepl_radio_data_indication_t)
  BLOCK_COMPLETE,         /// A block request is complete (event_data = pointer to oepl_radio_blockrecv_t)
  BLOCK_TIMEOUT,          /// A block request was unable to complete due to timing out
  BLOCK_CANCELED,         /// A block request was canceled actively or by a protocol error
  SCAN_TIMEOUT,           /// A channel scan has yielded no result
  POLL_TIMEOUT,           /// A data poll we sent while being associated has timed out
  CHANNEL_ROAM,           /// A channel switch has occurred (event_data = new channel number)
  CONFIRMATION_COMPLETE,  /// An acknowledge from us to the AP has been confirmed
  CONFIRMATION_TIMEOUT    /// An acknowledge from us to the AP has been sent, but no reply received
} oepl_radio_event_t;

typedef enum {
  NO_ACTION,              /// No action needs to be taken
  ACTION_COMPLETED        /// Send a transfer complete. Saves a call to \ref oepl_radio_acknowledge_action
} oepl_radio_action_t;

typedef enum {
  SUCCESS,                /// Requested action completed
  ERROR,                  /// Requested action could not be completed
  NOT_IMPLEMENTED         /// Requested action is not yet implemented
} oepl_radio_error_t;

typedef struct {
  uint8_t type;           /// Type of data/file transferred
  uint8_t id[8];          /// In most cases the MD5 checksum of the full file
  size_t filesize;        /// Total size of the data/file transferred
  uint8_t ap[8];          /// MAC address of the advertising AP
  uint16_t ap_pan;        /// PAN of the advertising AP (superfluous for now since PAN is hardcoded)
} oepl_datafile_descriptor_t;

typedef struct {
  oepl_datafile_descriptor_t file;  /// File this datablock refers to
  size_t idx;                       /// Index of this datablock in the file
} oepl_datablock_descriptor_t;

/**************************************************************************//**
 * Type definition for the radio process' callback function. This function is
 * called when the radio needs to inform the application of certain events.
 *
 * In case the application wants to immediately reply with an acknowledge, it
 * can return `ACTION_COMPLETED` and the radio will take care of it.
 *****************************************************************************/
typedef oepl_radio_action_t (*oepl_radio_event_cb_t)(oepl_radio_event_t event, const void* event_data);

typedef struct {
  /// Information returned by the AP in response to a data poll
  struct AvailDataInfo AP_data;
  /// MAC of the AP we received the indication from
  uint8_t AP_MAC[8];
  /// PAN of the AP we received the indication from (maybe superfluous, potentially used in future protocol enhancements)
  uint16_t AP_PAN;
} oepl_radio_data_indication_t;

typedef struct {
  /// Index of the block in the file
  size_t block_index;
  /// Size of the received block data in bytes
  size_t block_size;
  /// Pointer to the received block data, of length block_size.
  /// Note that this buffer must be explicitly free'd through calling
  /// oepl_radio_blockbuffer_release() when the application is
  /// done processing it, and before starting the next block request.
  const uint8_t* block_data;
} oepl_radio_blockrecv_t;

// -----------------------------------------------------------------------------
//                                Global Variables
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
//                          Public Function Declarations
// -----------------------------------------------------------------------------
/**************************************************************************//**
 * (Re-)initialize the radio, register the event handler and start association.
 *
 * @param cb Callback function which is called for @ref oepl_radio_event_t events
 * @param reason Initial wakeup reason communicated in the data poll packet
 * @param channel If known, the channel we were communicating on previously
 *****************************************************************************/
void oepl_radio_init(oepl_radio_event_cb_t cb, uint8_t reason, uint8_t channel);

/**************************************************************************//**
 * This is the event loop function for the radio implementation. Call it for
 * each iteration of the main event loop such that it can process events.
 *
 * Needs to be serviced as long as oepl_radio_is_event_pending() returns true
 *****************************************************************************/
void oepl_radio_process(void);

/**************************************************************************//**
 * Get the radio process' state.
 *****************************************************************************/
oepl_radio_status_t oepl_radio_get_status(void);

/**************************************************************************//**
 * Send an early poll in reaction to an async event
 *****************************************************************************/
oepl_radio_error_t oepl_radio_send_poll_with_reason(uint8_t reason);

/**************************************************************************//**
 * Trigger a channel scan with the intention to roam
 *****************************************************************************/
oepl_radio_error_t oepl_radio_try_roam(void);

/**************************************************************************//**
 * Send out-of-cycle tag information to the AP we're associated with.
 *
 * Seems to be mostly unimplemented in OEPL as of now.
 *****************************************************************************/
oepl_radio_error_t oepl_radio_send_tagdata(uint8_t* data, size_t length);

/**************************************************************************//**
 * Request data block. This will start a block download and generate a
 * BLOCK_COMPLETE event when the block has been downloaded.
 *
 * Reminder: A file is split in blocks of 4096 bytes, and each block is
 * transfered in parts which fit in the PHY's PSDU.
 *****************************************************************************/
oepl_radio_error_t oepl_radio_request_datablock(oepl_datablock_descriptor_t db);

/**************************************************************************//**
 * Release the resources associated with the datablock which was received in
 * the BLOCK_COMPLETE event.
 * 
 * Needs to have been called before requesting the next datablock.
 *****************************************************************************/
oepl_radio_error_t oepl_radio_release_datablock(void);

/**************************************************************************//**
 * Manually send a transfer complete. This allows the application to process
 * an indication or collect datablocks before ack'ing to the AP.
 *
 * Reminder: Any action (data pending, command, etc) in an 'AP_DATA' event must
 * be confirmed with an ack, else the AP will just keep sending the message.
 * Either the callback function must return 'ACKNOWLEDGE', or this function
 * must be called when the action was executed.
 *****************************************************************************/
oepl_radio_error_t oepl_radio_acknowledge_action(const uint8_t AP_MAC[8], uint16_t AP_PAN);

/**************************************************************************//**
 * Send out-of-cycle tag information to the AP we're associated with.
 *
 * Seems to be mostly unimplemented in OEPL as of now.
 *****************************************************************************/
bool oepl_radio_is_event_pending(void);

/**************************************************************************//**
 * Get the MAC address of the radio
 *****************************************************************************/
void oepl_radio_get_mac(uint8_t mac[8]);

/**************************************************************************//**
 * Get the details of the AP connection (if connected)
 *****************************************************************************/
bool oepl_radio_get_ap_link(uint8_t* channel, uint8_t AP_mac[8], uint8_t* lqi, int8_t* rssi);

#endif  // OEPL_RADIO_H
