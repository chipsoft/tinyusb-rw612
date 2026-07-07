/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2019 Ha Thach (tinyusb.org)
 * Copyright (c) 2024 Hardy Griech
 * Copyright (c) 2020 Jacob Berg Potter
 * Copyright (c) 2020 Peter Lawrence
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 * This file is part of the TinyUSB stack.
 */

/**
 * Small Glossary (from the spec)
 * --------------
 * Datagram - A collection of bytes forming a single item of information, passed as a unit from source to destination.
 * NCM      - Network Control Model
 * NDP      - NCM Datagram Pointer: NTB structure that delineates Datagrams (typically Ethernet frames) within an NTB
 * NTB      - NCM Transfer Block: a data structure for efficient USB encapsulation of one or more datagrams
 *            Each NTB is designed to be a single USB transfer
 * NTH      - NTB Header: a data structure at the front of each NTB, which provides the information needed to validate
 *            the NTB and begin decoding
 *
 * Some explanations
 * -----------------
 * - rhport        is the USB port of the device, in most cases "0"
 * - itf_data_alt  if != 0 -> data xmit/recv are allowed (see spec)
 * - ep_in         IN endpoints take data from the device intended to go in to the host (the device transmits)
 * - ep_out        OUT endpoints send data out of the host to the device (the device receives)
 */

#include "tusb_option.h"

#if (CFG_TUD_ENABLED && CFG_TUD_NCM)

#include "device/usbd.h"
#include "device/usbd_pvt.h"

#include "ncm.h"
#include "net_device.h"

// Level where CFG_TUSB_DEBUG must be at least for this driver is logged
#ifndef CFG_TUD_NCM_LOG_LEVEL
  #define CFG_TUD_NCM_LOG_LEVEL   CFG_TUD_LOG_LEVEL
#endif

#define TU_LOG_DRV(...)   TU_LOG(CFG_TUD_NCM_LOG_LEVEL, __VA_ARGS__)
#define NCM_HOST_CONFIG_GRACE_BLOCKED_TRIES 20u

// Alignment must be 4
#define TUD_NCM_ALIGNMENT   4
// calculate alignment of xmit datagrams within an NTB
#define XMIT_ALIGN_OFFSET(x) ((TUD_NCM_ALIGNMENT - ((x) & (TUD_NCM_ALIGNMENT - 1))) & (TUD_NCM_ALIGNMENT - 1))

//-----------------------------------------------------------------------------
//
// Module global things
//
#define XMIT_NTB_N CFG_TUD_NCM_IN_NTB_N
#define RECV_NTB_N CFG_TUD_NCM_OUT_NTB_N

typedef struct {
  // general
  uint8_t ep_in;        // endpoint for outgoing datagrams (naming is a little bit confusing)
  uint8_t ep_out;       // endpoint for incoming datagrams (naming is a little bit confusing)
  uint8_t ep_notif;     // endpoint for notifications
  uint8_t itf_num;      // interface number
  uint8_t itf_data_alt; // ==0 -> no endpoints, i.e. no network traffic, ==1 -> normal operation with two endpoints (spec, chapter 5.3)
  uint8_t rhport;       // storage of \a rhport because some callbacks are done without it
  uint16_t ep_size;     // bulk endpoint max packet size (IN and OUT assumed equal)

  // recv handling
  recv_ntb_t *recv_free_ntb[RECV_NTB_N];                // free list of recv NTBs
  recv_ntb_t *recv_ready_ntb[RECV_NTB_N];               // NTBs waiting for transmission to glue logic (circular buffer)
  #if RECV_NTB_N > 1
  uint8_t recv_ready_head;                              // head index for recv_ready_ntb circular buffer
  uint8_t recv_ready_tail;                              // tail index for recv_ready_ntb circular buffer
  uint8_t recv_ready_count;                             // number of elements in recv_ready_ntb circular buffer
  #endif
  recv_ntb_t *recv_tinyusb_ntb;                         // buffer for the running transfer TinyUSB -> driver
  recv_ntb_t *recv_glue_ntb;                            // buffer for the running transfer driver -> glue logic
  uint16_t recv_glue_ntb_datagram_ndx;                  // index into \a recv_glue_ntb_datagram

  // xmit handling
  xmit_ntb_t *xmit_free_ntb[XMIT_NTB_N];                // free list of xmit NTBs
  xmit_ntb_t *xmit_ready_ntb[XMIT_NTB_N];               // NTBs waiting for transmission to TinyUSB (circular buffer)
  #if XMIT_NTB_N > 1
  uint8_t xmit_ready_head;                              // head index for xmit_ready_ntb circular buffer
  uint8_t xmit_ready_tail;                              // tail index for xmit_ready_ntb circular buffer
  uint8_t xmit_ready_count;                             // number of elements in xmit_ready_ntb circular buffer
  #endif
  xmit_ntb_t *xmit_tinyusb_ntb;                         // buffer for the running transfer driver -> TinyUSB
  xmit_ntb_t *xmit_glue_ntb;                            // buffer for the running transfer glue logic -> driver
  uint16_t xmit_sequence;                               // NTB sequence counter
  uint16_t xmit_glue_ntb_datagram_ndx;                  // index into \a xmit_glue_ntb_datagram

  // notification handling
  enum {
    NOTIFICATION_SPEED,
    NOTIFICATION_CONNECTED,
    NOTIFICATION_DONE
  } notification_xmit_state;                            // state of notification transmission
  bool notification_xmit_is_running;                    // notification is currently transmitted
  bool link_is_up;                                      // current link state

  // host-configured transmit limits
  uint8_t bm_capabilities;
  uint16_t xmit_max_ntb_size;                           // maximum NTB size device may send
  uint16_t xmit_max_datagrams;                          // maximum datagrams per NTB device may send
  ncm_ntb_input_size_t ntb_input_size;

  // macOS/interop control-request extensions and TX-readiness gating
  uint16_t host_config_blocked_tries;                   // count blocked TX tries while host setup is incomplete
  uint16_t packet_filter;                               // host packet filter selection
  bool host_sent_datagram;                              // host has sent at least one data datagram
  uint8_t class_request_data[512];                      // scratch buffer for optional class requests
  uint8_t net_address[16];                              // host-visible network address blob
  uint16_t net_address_len;                             // valid bytes in net_address
  uint16_t ntb_format;                                  // 0 = NTH16/NDP16
  uint16_t crc_mode;                                    // 0 = no CRC
  uint16_t max_datagram_size;                           // negotiated max datagram size (persistent -- see NCM_SET_MAX_DATAGRAM_SIZE)

  // misc
  bool tud_network_recv_renew_active;                   // tud_network_recv_renew() is active (avoid recursive invocations)
  bool tud_network_recv_renew_process_again;            // tud_network_recv_renew() should process again
} ncm_interface_t;

typedef struct {
  struct {
    TUD_EPBUF_TYPE_DEF(recv_ntb_t, ntb);
  } recv[RECV_NTB_N];

  struct {
    TUD_EPBUF_TYPE_DEF(xmit_ntb_t, ntb);
  } xmit[XMIT_NTB_N];

  TUD_EPBUF_TYPE_DEF(ncm_notify_t, epnotif);
} ncm_epbuf_t;

static ncm_interface_t ncm_interface;
CFG_TUD_MEM_SECTION static ncm_epbuf_t ncm_epbuf;

// Protects concurrent access to xmit_glue_ntb / xmit_glue_ntb_datagram_ndx
// between the lwIP task (tud_network_can_xmit / tud_network_xmit) and the
// USB task (tud_task -> ncm_flush_data_paths / xmit_start_if_possible).
static osal_spinlock_t s_xmit_glue_lock;
static uint32_t s_xmit_inflight_since_millis;
static uint32_t s_xmit_stall_reported_for_start;
static uint32_t s_xmit_stall_count;
static void recv_put_ntb_into_free_list(recv_ntb_t *free_ntb);

static bool ncm_host_strictly_configured_for_tx(void) {
  return (ncm_interface.itf_data_alt == 1) &&
         (ncm_interface.packet_filter != 0 || ncm_interface.host_sent_datagram);
}

static bool ncm_host_configured_for_tx(void) {
  if (ncm_host_strictly_configured_for_tx()) {
    return true;
  }

  return ncm_interface.host_config_blocked_tries >= NCM_HOST_CONFIG_GRACE_BLOCKED_TRIES;
}

bool tud_network_ncm_data_interface_active(void) {
  return ncm_interface.itf_data_alt == 1;
}

bool tud_network_ncm_host_configured(void) {
  return ncm_host_configured_for_tx();
}

bool tud_network_ncm_host_strictly_configured(void) {
  return ncm_host_strictly_configured_for_tx();
}

bool tud_network_ncm_tx_stalled(uint32_t timeout_ms) {
  if (s_xmit_inflight_since_millis == 0) {
    return false;
  }
  if ((osal_time_millis() - s_xmit_inflight_since_millis) < timeout_ms) {
    return false;
  }
  if (s_xmit_stall_reported_for_start != s_xmit_inflight_since_millis) {
    s_xmit_stall_reported_for_start = s_xmit_inflight_since_millis;
    s_xmit_stall_count++;
  }
  return true;
}

//--------------------------------------------------------------------+
// Weak stubs: invoked if no strong implementation is available
//--------------------------------------------------------------------+
TU_ATTR_WEAK void tud_network_set_packet_filter_cb(uint16_t packet_filter) {
  (void) packet_filter;
}

TU_ATTR_WEAK bool tud_network_default_link_state_cb(void) {
  #ifdef CFG_TUD_NCM_DEFAULT_LINK_UP
  return CFG_TUD_NCM_DEFAULT_LINK_UP;
  #else
  return true;
  #endif
}

/**
 * This is the NTB parameter structure
 *
 * \attention
 *     We are lucky, that byte order is correct
 */
TU_ATTR_ALIGNED(4) static const ntb_parameters_t ntb_parameters = {
  .wLength                  = sizeof(ntb_parameters_t),
  .bmNtbFormatsSupported    = 0x01,// 16-bit NTB supported
  .dwNtbInMaxSize           = CFG_TUD_NCM_IN_NTB_MAX_SIZE,
  .wNdbInDivisor            = 1,
  .wNdbInPayloadRemainder   = 0,
  .wNdbInAlignment          = TUD_NCM_ALIGNMENT,
  .wReserved                = 0,
  .dwNtbOutMaxSize          = CFG_TUD_NCM_OUT_NTB_MAX_SIZE,
  .wNdbOutDivisor           = 1,
  .wNdbOutPayloadRemainder  = 0,
  .wNdbOutAlignment         = TUD_NCM_ALIGNMENT,
  .wNtbOutMaxDatagrams      = CFG_TUD_NCM_OUT_MAX_DATAGRAMS_PER_NTB,
};

// Some confusing remarks about wNtbOutMaxDatagrams...
//      ==1 -> SystemView packets/s goes up to 2000 and events are lost during startup
//      ==0 -> SystemView runs fine, iperf shows in wireshark a lot of error
//      ==6 -> SystemView runs fine, iperf also
//      >6  -> iperf starts to show errors
//      -> 6 seems to be the best value.  Why?  Don't know, perhaps only on my system?
//
//      iperf:    for MSS in 100 200 400 800 1200 1450 1500; do iperf -c 192.168.14.1 -e -i 1 -M $MSS -l 8192 -P 1; sleep 2; done
//      sysview:  SYSTICKS_PER_SEC=35000, IDLE_US=1000, PRINT_MOD=1000
//

//-----------------------------------------------------------------------------
//
// everything about notifications
//

/**
 * Transmit next notification to the host (if appropriate).
 * Notifications are transferred to the host once during connection setup.
 */
static void notification_xmit(uint8_t rhport, bool force_next) {
  TU_LOG_DRV("notification_xmit(%d, %d) - %d %d\n", force_next, rhport, ncm_interface.notification_xmit_state, ncm_interface.notification_xmit_is_running);

  // Never attempt to queue a notification while endpoint is busy.
  // usbd_edpt_xfer() asserts on busy endpoint.
  if (usbd_edpt_busy(rhport, ncm_interface.ep_notif)) {
    TU_LOG_DRV("  notification endpoint busy, defer\n");
    // Keep "running" false here: no transfer was queued.
    // Otherwise future non-force retries can be blocked forever.
    ncm_interface.notification_xmit_is_running = false;
    return;
  }

  if (!force_next && ncm_interface.notification_xmit_is_running) {
    return;
  }

  if (ncm_interface.notification_xmit_state == NOTIFICATION_SPEED) {
    TU_LOG_DRV("  NOTIFICATION_SPEED\n");
    ncm_notify_t notify_speed_change = {
      .header = {
        .bmRequestType_bit = {
          .recipient = TUSB_REQ_RCPT_INTERFACE,
          .type = TUSB_REQ_TYPE_CLASS,
          .direction = TUSB_DIR_IN
        },
        .bRequest = CDC_NOTIF_CONNECTION_SPEED_CHANGE,
        .wValue = 0,
        .wIndex = ncm_interface.itf_num,
        .wLength = 8
      }
    };
    if (tud_speed_get() == TUSB_SPEED_HIGH) {
      notify_speed_change.downlink = 480000000;
      notify_speed_change.uplink = 480000000;
    } else {
      notify_speed_change.downlink = 12000000;
      notify_speed_change.uplink = 12000000;
    }

    uint16_t notif_len = sizeof(notify_speed_change.header) + notify_speed_change.header.wLength;
    ncm_epbuf.epnotif = notify_speed_change;
    bool queued = usbd_edpt_xfer(rhport, ncm_interface.ep_notif, (uint8_t*) &ncm_epbuf.epnotif, notif_len, false);
    if (queued) {
      ncm_interface.notification_xmit_state = NOTIFICATION_CONNECTED;
      ncm_interface.notification_xmit_is_running = true;
    } else {
      TU_LOG_DRV("(WW) notification speed xfer not queued, will retry\n");
      ncm_interface.notification_xmit_is_running = false;
    }
  } else if (ncm_interface.notification_xmit_state == NOTIFICATION_CONNECTED) {
    TU_LOG_DRV("  NOTIFICATION_CONNECTED\n");
    ncm_notify_t notify_connected = {
      .header = {
        .bmRequestType_bit = {
          .recipient = TUSB_REQ_RCPT_INTERFACE,
          .type = TUSB_REQ_TYPE_CLASS,
          .direction = TUSB_DIR_IN
        },
        .bRequest = CDC_NOTIF_NETWORK_CONNECTION,
        .wValue = ncm_interface.link_is_up ? 1 : 0,  /* Dynamic link state */
        .wIndex = ncm_interface.itf_num,
        .wLength = 0,
      },
    };

    uint16_t notif_len = sizeof(notify_connected.header) + notify_connected.header.wLength;
    ncm_epbuf.epnotif = notify_connected;
    bool queued = usbd_edpt_xfer(rhport, ncm_interface.ep_notif, (uint8_t *) &ncm_epbuf.epnotif, notif_len, false);
    if (queued) {
      ncm_interface.notification_xmit_state = NOTIFICATION_DONE;
      ncm_interface.notification_xmit_is_running = true;
    } else {
      TU_LOG_DRV("(WW) notification connected xfer not queued, will retry\n");
      ncm_interface.notification_xmit_is_running = false;
    }
  } else {
    TU_LOG_DRV("  NOTIFICATION_FINISHED\n");
    ncm_interface.notification_xmit_is_running = false;
  }
} // notification_xmit

//-----------------------------------------------------------------------------
//
// everything about packet transmission (driver -> TinyUSB)
//

/**
 * Put NTB into the transmitter free list.
 */
static void xmit_put_ntb_into_free_list(xmit_ntb_t *free_ntb) {
  TU_LOG_DRV("xmit_put_ntb_into_free_list() - %p\n", ncm_interface.xmit_tinyusb_ntb);

  if (free_ntb == NULL) { // can happen due to ZLPs
    return;
  }

  for (int i = 0; i < XMIT_NTB_N; ++i) {
    if (ncm_interface.xmit_free_ntb[i] == NULL) {
      ncm_interface.xmit_free_ntb[i] = free_ntb;
      return;
    }
  }
  TU_LOG_DRV("(EE) xmit_put_ntb_into_free_list - no entry in free list\n");// this should not happen
} // xmit_put_ntb_into_free_list

/**
 * Get an NTB from the free list
 */
static xmit_ntb_t *xmit_get_free_ntb(void) {
  TU_LOG_DRV("xmit_get_free_ntb()\n");

  for (int i = 0; i < XMIT_NTB_N; ++i) {
    if (ncm_interface.xmit_free_ntb[i] != NULL) {
      xmit_ntb_t *free = ncm_interface.xmit_free_ntb[i];
      ncm_interface.xmit_free_ntb[i] = NULL;
      return free;
    }
  }
  return NULL;
} // xmit_get_free_ntb

/**
 * Put a filled NTB into the ready list
 */
static void xmit_put_ntb_into_ready_list(xmit_ntb_t *ready_ntb) {
  TU_LOG_DRV("xmit_put_ntb_into_ready_list(%p) %d\n", ready_ntb, ready_ntb->nth.wBlockLength);

#if XMIT_NTB_N == 1
  ncm_interface.xmit_ready_ntb[0] = ready_ntb;
#else
  if (ncm_interface.xmit_ready_count >= XMIT_NTB_N) {
    TU_LOG_DRV("(EE) xmit_put_ntb_into_ready_list: ready list full\n");// this should not happen
    return;
  }
  ncm_interface.xmit_ready_ntb[ncm_interface.xmit_ready_head] = ready_ntb;
  ncm_interface.xmit_ready_head = (ncm_interface.xmit_ready_head + 1) % XMIT_NTB_N;
  ncm_interface.xmit_ready_count++;
#endif
} // xmit_put_ntb_into_ready_list

/**
 * Get the next NTB from the ready list (and remove it from the list).
 * If the ready list is empty, return NULL.
 */
static xmit_ntb_t *xmit_get_next_ready_ntb(void) {
#if XMIT_NTB_N == 1
  xmit_ntb_t *r = ncm_interface.xmit_ready_ntb[0];
  ncm_interface.xmit_ready_ntb[0] = NULL;
  TU_LOG_DRV("xmit_get_next_ready_ntb: %p\n", r);
  return r;
#else
  if (ncm_interface.xmit_ready_count == 0) {
    return NULL; // empty
  }

  xmit_ntb_t *r = ncm_interface.xmit_ready_ntb[ncm_interface.xmit_ready_tail];
  ncm_interface.xmit_ready_ntb[ncm_interface.xmit_ready_tail] = NULL;
  ncm_interface.xmit_ready_tail = (ncm_interface.xmit_ready_tail + 1) % XMIT_NTB_N;
  ncm_interface.xmit_ready_count--;

  TU_LOG_DRV("xmit_get_next_ready_ntb: %p\n", r);
  return r;
#endif
} // xmit_get_next_ready_ntb

/**
 * Flush buffered TX/RX NTBs after host data-interface alt-setting changes.
 * This prevents stale in-flight state from blocking traffic after alt flaps.
 */
static void ncm_flush_data_paths(void) {
  s_xmit_inflight_since_millis = 0;
  s_xmit_stall_reported_for_start = 0;

  // Do NOT reclaim a buffer whose bulk transfer is still queued in the DCD.
  // On an alt-setting flap the host may re-arm the data interface while the
  // IN transfer is still in flight; freeing the buffer here (and NULLing the
  // owner pointer) makes the later xfer-completion callback act on a stale
  // NTB. Leave ownership with the pending transfer -- netd_xfer_cb() reclaims
  // it normally when the transfer completes. (T-312)
  if (ncm_interface.xmit_tinyusb_ntb != NULL &&
      !usbd_edpt_busy(ncm_interface.rhport, ncm_interface.ep_in)) {
    xmit_put_ntb_into_free_list(ncm_interface.xmit_tinyusb_ntb);
    ncm_interface.xmit_tinyusb_ntb = NULL;
  }

  // Snapshot and clear under lock, then act on the snapshot outside it --
  // mirrors the pattern in xmit_setup_next_glue_ntb() below. Keeps the
  // critical section to a pointer swap instead of a logging call + list scan.
  osal_spin_lock(&s_xmit_glue_lock, false);
  xmit_ntb_t *stale_glue_ntb = ncm_interface.xmit_glue_ntb;
  ncm_interface.xmit_glue_ntb = NULL;
  ncm_interface.xmit_glue_ntb_datagram_ndx = 0;
  osal_spin_unlock(&s_xmit_glue_lock, false);

  if (stale_glue_ntb != NULL) {
    xmit_put_ntb_into_free_list(stale_glue_ntb);
  }

  for (int i = 0; i < XMIT_NTB_N; ++i) {
    if (ncm_interface.xmit_ready_ntb[i] != NULL) {
      xmit_put_ntb_into_free_list(ncm_interface.xmit_ready_ntb[i]);
      ncm_interface.xmit_ready_ntb[i] = NULL;
    }
  }
  #if XMIT_NTB_N > 1
  ncm_interface.xmit_ready_head = 0;
  ncm_interface.xmit_ready_tail = 0;
  ncm_interface.xmit_ready_count = 0;
  #endif

  // Same rule for the RX path: if the bulk-OUT transfer is still queued in the
  // DCD, do not free its buffer or NULL the owner pointer. Otherwise the host's
  // alt=1 -> alt=0 -> alt=1 flap (Linux CDC-NCM bring-up) leaves the stale
  // transfer to complete against a NULL recv_tinyusb_ntb -> NULL deref in
  // recv_validate_datagram() -> HardFault. (T-312)
  if (ncm_interface.recv_tinyusb_ntb != NULL &&
      !usbd_edpt_busy(ncm_interface.rhport, ncm_interface.ep_out)) {
    recv_put_ntb_into_free_list(ncm_interface.recv_tinyusb_ntb);
    ncm_interface.recv_tinyusb_ntb = NULL;
  }

  if (ncm_interface.recv_glue_ntb != NULL) {
    recv_put_ntb_into_free_list(ncm_interface.recv_glue_ntb);
    ncm_interface.recv_glue_ntb = NULL;
  }
  ncm_interface.recv_glue_ntb_datagram_ndx = 0;

  for (int i = 0; i < RECV_NTB_N; ++i) {
    if (ncm_interface.recv_ready_ntb[i] != NULL) {
      recv_put_ntb_into_free_list(ncm_interface.recv_ready_ntb[i]);
      ncm_interface.recv_ready_ntb[i] = NULL;
    }
  }
  #if RECV_NTB_N > 1
  ncm_interface.recv_ready_head = 0;
  ncm_interface.recv_ready_tail = 0;
  ncm_interface.recv_ready_count = 0;
  #endif
} // ncm_flush_data_paths

/**
 * Transmit a ZLP if required
 *
 * \note
 *    Insertion of the ZLPs is a little bit different then described in the spec.
 *    But the below implementation actually works.  Don't know if this is a spec
 *    or TinyUSB issue.
 *
 * \pre
 *    This must be called from netd_xfer_cb() so that ep_in is ready
 */
static bool xmit_insert_required_zlp(uint8_t rhport, uint32_t xferred_bytes) {
  TU_LOG_DRV("xmit_insert_required_zlp(%d,%ld)\n", rhport, xferred_bytes);

  uint16_t const ep_size = ncm_interface.ep_size;
  if (xferred_bytes == 0 || (xferred_bytes & (ep_size-1)) != 0) {
    return false;
  }

  TU_ASSERT(ncm_interface.itf_data_alt == 1, false);
  TU_ASSERT(!usbd_edpt_busy(rhport, ncm_interface.ep_in), false);

  TU_LOG_DRV("xmit_insert_required_zlp! (%u)\n", (unsigned) xferred_bytes);

  // start transmission of the ZLP
  usbd_edpt_xfer(rhport, ncm_interface.ep_in, NULL, 0, false);

  return true;
} // xmit_insert_required_zlp

/**
 * Start transmission if it there is a waiting packet and if can be done from interface side.
 */
static void xmit_start_if_possible(uint8_t rhport) {
  TU_LOG_DRV("xmit_start_if_possible()\n");

  if (ncm_interface.xmit_tinyusb_ntb != NULL) {
    TU_LOG_DRV("  !xmit_start_if_possible 1\n");
    return;
  }
  if (ncm_interface.itf_data_alt != 1) {
    TU_LOG_DRV("(EE) !xmit_start_if_possible 2\n");
    return;
  }
  if (!ncm_host_configured_for_tx()) {
    TU_LOG_DRV("  !xmit_start_if_possible host not configured (filter=0x%04x, host_rx=%u)\n",
               ncm_interface.packet_filter, ncm_interface.host_sent_datagram);
    return;
  }
  if (usbd_edpt_busy(rhport, ncm_interface.ep_in)) {
    TU_LOG_DRV("  !xmit_start_if_possible 3\n");
    return;
  }

  ncm_interface.xmit_tinyusb_ntb = xmit_get_next_ready_ntb();
  if (ncm_interface.xmit_tinyusb_ntb == NULL) {
    // Guard and steal of xmit_glue_ntb must be atomic with respect to the
    // lwIP task that publishes / reads xmit_glue_ntb and datagram_ndx.
    osal_spin_lock(&s_xmit_glue_lock, false);
    if (ncm_interface.xmit_glue_ntb == NULL || ncm_interface.xmit_glue_ntb_datagram_ndx == 0) {
      // -> really nothing is waiting
      osal_spin_unlock(&s_xmit_glue_lock, false);
      return;
    }
    ncm_interface.xmit_tinyusb_ntb = ncm_interface.xmit_glue_ntb;
    ncm_interface.xmit_glue_ntb = NULL;
    osal_spin_unlock(&s_xmit_glue_lock, false);
  }

  #if CFG_TUD_NCM_LOG_LEVEL >= 3
  {
    uint16_t len = ncm_interface.xmit_tinyusb_ntb->nth.wBlockLength;
    TU_LOG_BUF(3, ncm_interface.xmit_tinyusb_ntb->data[i], len);
  }
  #endif

  if (ncm_interface.xmit_glue_ntb_datagram_ndx != 1) {
    TU_LOG_DRV(">> %d %d\n", ncm_interface.xmit_tinyusb_ntb->nth.wBlockLength, ncm_interface.xmit_glue_ntb_datagram_ndx);
  }

  // Kick off an endpoint transfer. If the DCD refuses the transfer even
  // though usbd_edpt_busy() was false, do not leave the NTB owned by the
  // TinyUSB in-flight slot forever. Requeue it so a later SOF/callback can
  // retry and so tud_network_can_xmit() does not wedge permanently.
  if (usbd_edpt_xfer(rhport, ncm_interface.ep_in, ncm_interface.xmit_tinyusb_ntb->data,
                     ncm_interface.xmit_tinyusb_ntb->nth.wBlockLength, false)) {
    s_xmit_inflight_since_millis = osal_time_millis();
    s_xmit_stall_reported_for_start = 0;
  } else {
    TU_LOG_DRV("(EE) xmit_start_if_possible: usbd_edpt_xfer failed\n");
    xmit_put_ntb_into_ready_list(ncm_interface.xmit_tinyusb_ntb);
    ncm_interface.xmit_tinyusb_ntb = NULL;
    s_xmit_inflight_since_millis = 0;
    s_xmit_stall_reported_for_start = 0;
  }
} // xmit_start_if_possible

/**
 * check if a new datagram fits into the current NTB
 */
static bool xmit_requested_datagram_fits_into_current_ntb(uint16_t datagram_size) {
  TU_LOG_DRV("xmit_requested_datagram_fits_into_current_ntb(%d) - %p %p\n", datagram_size, ncm_interface.xmit_tinyusb_ntb, ncm_interface.xmit_glue_ntb);

  // Lock across the whole read sequence: xmit_glue_ntb can be concurrently
  // stolen (set NULL) by xmit_start_if_possible() on the USB task between
  // the NULL check and the wBlockLength dereference below without this.
  osal_spin_lock(&s_xmit_glue_lock, false);
  xmit_ntb_t *ntb = ncm_interface.xmit_glue_ntb;
  if (ntb == NULL) {
    osal_spin_unlock(&s_xmit_glue_lock, false);
    return false;
  }
  if (ncm_interface.xmit_glue_ntb_datagram_ndx >= ncm_interface.xmit_max_datagrams) {
    osal_spin_unlock(&s_xmit_glue_lock, false);
    return false;
  }
  bool fits = ntb->nth.wBlockLength + datagram_size + (uint32_t)XMIT_ALIGN_OFFSET(datagram_size) <= (uint32_t)ncm_interface.xmit_max_ntb_size;
  osal_spin_unlock(&s_xmit_glue_lock, false);
  return fits;
} // xmit_requested_datagram_fits_into_current_ntb

/**
 * Setup an NTB for the glue logic
 */
static bool xmit_setup_next_glue_ntb(void) {
  TU_LOG_DRV("xmit_setup_next_glue_ntb - %p\n", ncm_interface.xmit_glue_ntb);

  // Snapshot and clear the old glue NTB under lock so the USB task cannot
  // race on xmit_glue_ntb while we decide what to do with the old buffer.
  osal_spin_lock(&s_xmit_glue_lock, false);
  xmit_ntb_t *old_ntb = ncm_interface.xmit_glue_ntb;
  ncm_interface.xmit_glue_ntb = NULL;
  osal_spin_unlock(&s_xmit_glue_lock, false);

  if (old_ntb != NULL) {
    // put NTB into waiting list (the new datagram did not fit in)
    xmit_put_ntb_into_ready_list(old_ntb);
  }

  // Allocate next free NTB -- non-blocking array scan, no lock needed here.
  xmit_ntb_t *ntb = xmit_get_free_ntb();
  if (ntb == NULL) {
    TU_LOG_DRV("  xmit_setup_next_glue_ntb - nothing free\n");// should happen rarely
    return false;
  }

  // Fill in NTB and NDP16 headers via local variable -- no shared-state access.
  ntb->nth.dwSignature = NTH16_SIGNATURE;
  ntb->nth.wHeaderLength = sizeof(ntb->nth);
  ntb->nth.wSequence = ncm_interface.xmit_sequence++;
  ntb->nth.wBlockLength = sizeof(ntb->nth) + sizeof(ntb->ndp) + sizeof(ntb->ndp_datagram);
  ntb->nth.wNdpIndex = sizeof(ntb->nth);

  ntb->ndp.dwSignature = NDP16_SIGNATURE_NCM0;
  ntb->ndp.wLength = sizeof(ntb->ndp) + sizeof(ntb->ndp_datagram);
  ntb->ndp.wNextNdpIndex = 0;

  memset(ntb->ndp_datagram, 0, sizeof(ntb->ndp_datagram));

  // Publish: set datagram_ndx and xmit_glue_ntb together under lock so the
  // USB task always sees a consistent (ptr, ndx) pair.
  osal_spin_lock(&s_xmit_glue_lock, false);
  ncm_interface.xmit_glue_ntb_datagram_ndx = 0;
  ncm_interface.xmit_glue_ntb = ntb;
  osal_spin_unlock(&s_xmit_glue_lock, false);
  return true;
} // xmit_setup_next_glue_ntb

//-----------------------------------------------------------------------------
//
// all the recv_*() stuff (TinyUSB -> driver -> glue logic)
//

/**
 * Return pointer to an available receive buffer or NULL.
 * Returned buffer (if any) has the size \a CFG_TUD_NCM_OUT_NTB_MAX_SIZE.
 */
static recv_ntb_t *recv_get_free_ntb(void) {
  TU_LOG_DRV("recv_get_free_ntb()\n");

  for (int i = 0; i < RECV_NTB_N; ++i) {
    if (ncm_interface.recv_free_ntb[i] != NULL) {
      recv_ntb_t *free = ncm_interface.recv_free_ntb[i];
      ncm_interface.recv_free_ntb[i] = NULL;
      return free;
    }
  }
  return NULL;
} // recv_get_free_ntb

/**
 * Get the next NTB from the ready list (and remove it from the list).
 * If the ready list is empty, return NULL.
 */
static recv_ntb_t *recv_get_next_ready_ntb(void) {
#if RECV_NTB_N == 1
  recv_ntb_t *r = ncm_interface.recv_ready_ntb[0];
  ncm_interface.recv_ready_ntb[0] = NULL;
  TU_LOG_DRV("recv_get_next_ready_ntb: %p\n", r);
  return r;
#else
  if (ncm_interface.recv_ready_count == 0) {
    return NULL; // empty
  }

  recv_ntb_t *r = ncm_interface.recv_ready_ntb[ncm_interface.recv_ready_tail];
  ncm_interface.recv_ready_ntb[ncm_interface.recv_ready_tail] = NULL;
  ncm_interface.recv_ready_tail = (ncm_interface.recv_ready_tail + 1) % RECV_NTB_N;
  ncm_interface.recv_ready_count--;

  TU_LOG_DRV("recv_get_next_ready_ntb: %p\n", r);
  return r;
#endif
} // recv_get_next_ready_ntb

/**
 * Put NTB into the receiver free list.
 */
static void recv_put_ntb_into_free_list(recv_ntb_t *free_ntb) {
  TU_LOG_DRV("recv_put_ntb_into_free_list(%p)\n", free_ntb);

  for (int i = 0; i < RECV_NTB_N; ++i) {
    if (ncm_interface.recv_free_ntb[i] == NULL) {
      ncm_interface.recv_free_ntb[i] = free_ntb;
      return;
    }
  }
  TU_LOG_DRV("(EE) recv_put_ntb_into_free_list - no entry in free list\n");// this should not happen
} // recv_put_ntb_into_free_list

/**
 * \a ready_ntb holds a validated NTB,
 * put this buffer into the waiting list.
 */
static void recv_put_ntb_into_ready_list(recv_ntb_t *ready_ntb) {
  TU_LOG_DRV("recv_put_ntb_into_ready_list(%p) %d\n", ready_ntb, ready_ntb->nth.wBlockLength);

#if RECV_NTB_N == 1
  ncm_interface.recv_ready_ntb[0] = ready_ntb;
#else
  if (ncm_interface.recv_ready_count >= RECV_NTB_N) {
    TU_LOG_DRV("(EE) recv_put_ntb_into_ready_list: ready list full\n");// this should not happen
    return;
  }
  ncm_interface.recv_ready_ntb[ncm_interface.recv_ready_head] = ready_ntb;
  ncm_interface.recv_ready_head = (ncm_interface.recv_ready_head + 1) % RECV_NTB_N;
  ncm_interface.recv_ready_count++;
#endif
} // recv_put_ntb_into_ready_list

/**
 * If possible, start a new reception TinyUSB -> driver.
 */
static void recv_try_to_start_new_reception(uint8_t rhport) {
  TU_LOG_DRV("recv_try_to_start_new_reception(%d)\n", rhport);

  if (ncm_interface.itf_data_alt != 1) {
    return;
  }
  if (ncm_interface.recv_tinyusb_ntb != NULL) {
    return;
  }
  if (usbd_edpt_busy(rhport, ncm_interface.ep_out)) {
    return;
  }

  ncm_interface.recv_tinyusb_ntb = recv_get_free_ntb();
  if (ncm_interface.recv_tinyusb_ntb == NULL) {
    return;
  }

  // initiate transfer
  TU_LOG_DRV("  start reception\n");
  bool r = usbd_edpt_xfer(rhport, ncm_interface.ep_out, ncm_interface.recv_tinyusb_ntb->data, CFG_TUD_NCM_OUT_NTB_MAX_SIZE, false);
  if (!r) {
    recv_put_ntb_into_free_list(ncm_interface.recv_tinyusb_ntb);
    ncm_interface.recv_tinyusb_ntb = NULL;
  }
} // recv_try_to_start_new_reception

/**
 * Validate incoming datagram.
 * \return true if valid
 *
 * \note
 *    \a ndp16->wNextNdpIndex != 0 is not supported
 */
static bool recv_validate_datagram(const recv_ntb_t *ntb, uint32_t len) {
  TU_LOG_DRV("recv_validate_datagram(%p, %d)\n", ntb, (int) len);

  // Defensive: never dereference a NULL NTB (see T-312). The header access
  // below would otherwise fault on a stale/cleared owner pointer.
  if (ntb == NULL) {
    TU_LOG_DRV("(EE) recv_validate_datagram: NULL ntb\n");
    return false;
  }

  const nth16_t *nth16 = &(ntb->nth);

  // check header
  if (nth16->wHeaderLength != sizeof(nth16_t)) {
    TU_LOG_DRV("(EE) ill nth16 length: %d\n", nth16->wHeaderLength);
    return false;
  }
  if (nth16->dwSignature != NTH16_SIGNATURE) {
    TU_LOG_DRV("(EE) ill signature: 0x%08x\n", (unsigned) nth16->dwSignature);
    return false;
  }
  if (len < sizeof(nth16_t) + sizeof(ndp16_t) + 2 * sizeof(ndp16_datagram_t)) {
    TU_LOG_DRV("(EE) ill min len: %lu\n", len);
    return false;
  }
  if (nth16->wBlockLength > len) {
    TU_LOG_DRV("(EE) ill block length: %d > %lu\n", nth16->wBlockLength, len);
    return false;
  }
  if (nth16->wBlockLength > CFG_TUD_NCM_OUT_NTB_MAX_SIZE) {
    TU_LOG_DRV("(EE) ill block length2: %d > %d\n", nth16->wBlockLength, CFG_TUD_NCM_OUT_NTB_MAX_SIZE);
    return false;
  }
  if (nth16->wNdpIndex < sizeof(nth16) || nth16->wNdpIndex > len - (sizeof(ndp16_t) + 2 * sizeof(ndp16_datagram_t))) {
    TU_LOG_DRV("(EE) ill position of first ndp: %d (%lu)\n", nth16->wNdpIndex, len);
    return false;
  }

  // check (first) NDP(16)
  const ndp16_t *ndp16 = (const ndp16_t *) (ntb->data + nth16->wNdpIndex);

  if (ndp16->wLength < sizeof(ndp16_t) + 2 * sizeof(ndp16_datagram_t)) {
    TU_LOG_DRV("(EE) ill ndp16 length: %d\n", ndp16->wLength);
    return false;
  }
  if (ndp16->dwSignature != NDP16_SIGNATURE_NCM0 && ndp16->dwSignature != NDP16_SIGNATURE_NCM1) {
    TU_LOG_DRV("(EE) ill signature: 0x%08x\n", (unsigned) ndp16->dwSignature);
    return false;
  }
  if (ndp16->wNextNdpIndex != 0) {
    TU_LOG_DRV("(EE) cannot handle wNextNdpIndex!=0 (%d)\n", ndp16->wNextNdpIndex);
    return false;
  }

  const ndp16_datagram_t *ndp16_datagram = (const ndp16_datagram_t *) (ntb->data + nth16->wNdpIndex + sizeof(ndp16_t));
  int ndx = 0;
  uint16_t max_ndx = (uint16_t) ((ndp16->wLength - sizeof(ndp16_t)) / sizeof(ndp16_datagram_t));

  if (max_ndx > 2) { // number of datagrams in NTB > 1
    TU_LOG_DRV("<< %d (%d)\n", max_ndx - 1, ntb->nth.wBlockLength);
  }
  if (ndp16_datagram[max_ndx - 1].wDatagramIndex != 0 || ndp16_datagram[max_ndx - 1].wDatagramLength != 0) {
    TU_LOG_DRV("  max_ndx != 0\n");
    return false;
  }
  while (ndp16_datagram[ndx].wDatagramIndex != 0 && ndp16_datagram[ndx].wDatagramLength != 0) {
    TU_LOG_DRV("  << %d %d\n", ndp16_datagram[ndx].wDatagramIndex, ndp16_datagram[ndx].wDatagramLength);
    if (ndp16_datagram[ndx].wDatagramIndex > len) {
      TU_LOG_DRV("(EE) ill start of datagram[%d]: %d (%lu)\n", ndx, ndp16_datagram[ndx].wDatagramIndex, len);
      return false;
    }
    if (ndp16_datagram[ndx].wDatagramIndex + ndp16_datagram[ndx].wDatagramLength > len) {
      TU_LOG_DRV("(EE) ill end of datagram[%d]: %d (%lu)\n", ndx, ndp16_datagram[ndx].wDatagramIndex + ndp16_datagram[ndx].wDatagramLength, len);
      return false;
    }
    ++ndx;
  }

  #if CFG_TUD_NCM_LOG_LEVEL >= 3
  TU_LOG_BUF(3, ntb->data[i], len);
  #endif

  // -> ntb contains a valid packet structure
  //    ok... I did not check for garbage within the datagram indices...
  return true;
} // recv_validate_datagram

/**
 * Transfer the next (pending) datagram to the glue logic and return receive buffer if empty.
 */
static void recv_transfer_datagram_to_glue_logic(void) {
  TU_LOG_DRV("recv_transfer_datagram_to_glue_logic()\n");

  if (ncm_interface.recv_glue_ntb == NULL) {
    ncm_interface.recv_glue_ntb = recv_get_next_ready_ntb();
    TU_LOG_DRV("  new buffer for glue logic: %p\n", ncm_interface.recv_glue_ntb);
    ncm_interface.recv_glue_ntb_datagram_ndx = 0;
  }

  if (ncm_interface.recv_glue_ntb != NULL) {
    const ndp16_datagram_t *ndp16_datagram = (ndp16_datagram_t *) (ncm_interface.recv_glue_ntb->data + ncm_interface.recv_glue_ntb->nth.wNdpIndex + sizeof(ndp16_t));

    if (ndp16_datagram[ncm_interface.recv_glue_ntb_datagram_ndx].wDatagramIndex == 0) {
      TU_LOG_DRV("(EE) SOMETHING WENT WRONG 1\n");
    } else if (ndp16_datagram[ncm_interface.recv_glue_ntb_datagram_ndx].wDatagramLength == 0) {
      TU_LOG_DRV("(EE) SOMETHING WENT WRONG 2\n");
    } else {
      uint16_t datagramIndex = ndp16_datagram[ncm_interface.recv_glue_ntb_datagram_ndx].wDatagramIndex;
      uint16_t datagramLength = ndp16_datagram[ncm_interface.recv_glue_ntb_datagram_ndx].wDatagramLength;

      TU_LOG_DRV("  recv[%d] - %d %d\n", ncm_interface.recv_glue_ntb_datagram_ndx, datagramIndex, datagramLength);
      if (tud_network_recv_cb(ncm_interface.recv_glue_ntb->data + datagramIndex, datagramLength)) {
        // send datagram successfully to glue logic
        TU_LOG_DRV("    OK\n");
        datagramIndex = ndp16_datagram[ncm_interface.recv_glue_ntb_datagram_ndx + 1].wDatagramIndex;
        datagramLength = ndp16_datagram[ncm_interface.recv_glue_ntb_datagram_ndx + 1].wDatagramLength;

        if (datagramIndex != 0 && datagramLength != 0) {
          // -> next datagram
          ++ncm_interface.recv_glue_ntb_datagram_ndx;
        } else {
          // end of datagrams reached
          recv_put_ntb_into_free_list(ncm_interface.recv_glue_ntb);
          ncm_interface.recv_glue_ntb = NULL;
        }
      }
    }
  }
} // recv_transfer_datagram_to_glue_logic

//-----------------------------------------------------------------------------
//
// all the tud_network_*() stuff (glue logic -> driver)
//

/**
 * Check if the glue logic is allowed to call tud_network_xmit().
 * This function also fetches a next buffer if required, so that tud_network_xmit() is ready for copy
 * and transmission operation.
 */
bool tud_network_can_xmit(uint16_t size) {
  TU_LOG_DRV("tud_network_can_xmit(%d)\n", size);

  TU_ASSERT(size <= ncm_interface.xmit_max_ntb_size - (sizeof(nth16_t) + sizeof(ndp16_t) + 2 * sizeof(ndp16_datagram_t)), false);

  if (!ncm_host_configured_for_tx()) {
    TU_LOG_DRV("  !tud_network_can_xmit host not configured (alt=%u, filter=0x%04x, host_rx=%u)\n",
               ncm_interface.itf_data_alt, ncm_interface.packet_filter, ncm_interface.host_sent_datagram);
    if (ncm_interface.itf_data_alt == 1 &&
        ncm_interface.packet_filter == 0 &&
        !ncm_interface.host_sent_datagram &&
        ncm_interface.host_config_blocked_tries < NCM_HOST_CONFIG_GRACE_BLOCKED_TRIES) {
      ncm_interface.host_config_blocked_tries++;
    }
    return false;
  }

  if (xmit_requested_datagram_fits_into_current_ntb(size) || xmit_setup_next_glue_ntb()) {
    // -> everything is fine
    return true;
  }
  xmit_start_if_possible(ncm_interface.rhport);
  TU_LOG_DRV("(II) tud_network_can_xmit: request blocked\n");// could happen if all xmit buffers are full (but should happen rarely)
  return false;
} // tud_network_can_xmit

/**
 * Put a datagram into a waiting NTB.
 * If currently no transmission is started, then initiate transmission.
 */
void tud_network_xmit(void *ref, uint16_t arg) {
  TU_LOG_DRV("tud_network_xmit(%p, %d)\n", ref, arg);

  // Hold the lock across the entire fill+publish sequence, not just the
  // pointer/index snapshot. xmit_glue_ntb_datagram_ndx is the signal
  // xmit_start_if_possible() checks to decide the buffer has a complete
  // datagram ready to DMA out; publishing it (releasing the lock) before
  // tud_network_xmit_cb() has copied the payload and before ndp_datagram/
  // wBlockLength are updated lets the USB task steal and start
  // transmitting this NTB while we are still writing into the same memory
  // -- a live write-during-DMA-read race (Codex review finding). Ends up
  // consistent with, and covered by, the T-145 lock this port already
  // added to xmit_start_if_possible()/xmit_setup_next_glue_ntb().
  //
  // tud_network_xmit_cb() (bsp_usb_ncm.c) is a bounded, non-blocking
  // pbuf-chain memcpy -- safe to run under this task-context critical
  // section.
  osal_spin_lock(&s_xmit_glue_lock, false);
  xmit_ntb_t *ntb = ncm_interface.xmit_glue_ntb;
  if (ntb == NULL) {
    osal_spin_unlock(&s_xmit_glue_lock, false);
    TU_LOG_DRV("(EE) tud_network_xmit: no buffer\n");// must not happen (really)
    return;
  }
  uint16_t ndx = ncm_interface.xmit_glue_ntb_datagram_ndx;

  // copy new datagram to the end of the current NTB
  uint16_t size = tud_network_xmit_cb(ntb->data + ntb->nth.wBlockLength, ref, arg);

  // correct NTB internals using the captured slot index, then publish by
  // advancing xmit_glue_ntb_datagram_ndx -- all still under the lock.
  ntb->ndp_datagram[ndx].wDatagramIndex = ntb->nth.wBlockLength;
  ntb->ndp_datagram[ndx].wDatagramLength = size;
  ntb->nth.wBlockLength += (uint16_t) (size + XMIT_ALIGN_OFFSET(size));
  ncm_interface.xmit_glue_ntb_datagram_ndx = (uint16_t) (ndx + 1);
  osal_spin_unlock(&s_xmit_glue_lock, false);

  if (ntb->nth.wBlockLength > CFG_TUD_NCM_IN_NTB_MAX_SIZE) {
    TU_LOG_DRV("(EE) tud_network_xmit: buffer overflow\n"); // must not happen (really)
    return;
  }

  xmit_start_if_possible(ncm_interface.rhport);
} // tud_network_xmit

/**
 * Keep the receive logic busy and transfer pending packets to the glue logic.
 * Avoid recursive calls due to wrong expectations of the net glue logic,
 * see https://github.com/hathach/tinyusb/issues/2711
 */
void tud_network_recv_renew(void) {
  TU_LOG_DRV("tud_network_recv_renew()\n");

  ncm_interface.tud_network_recv_renew_process_again = true;

  if (ncm_interface.tud_network_recv_renew_active) {
    TU_LOG_DRV("Re-entrant into tud_network_recv_renew, will process later\n");
    return;
  }

  while (ncm_interface.tud_network_recv_renew_process_again) {
    ncm_interface.tud_network_recv_renew_process_again = false;

    // If the current function is called within recv_transfer_datagram_to_glue_logic,
    // tud_network_recv_renew_process_again will become true, and the loop will run again
    // Otherwise the loop will not run again
    ncm_interface.tud_network_recv_renew_active = true;
    recv_transfer_datagram_to_glue_logic();
    ncm_interface.tud_network_recv_renew_active = false;
  }
  recv_try_to_start_new_reception(ncm_interface.rhport);
} // tud_network_recv_renew

/**
 * Same as tud_network_recv_renew() but knows \a rhport
 */
static void tud_network_recv_renew_r(uint8_t rhport) {
  TU_LOG_DRV("tud_network_recv_renew_r(%d)\n", rhport);

  ncm_interface.rhport = rhport;
  tud_network_recv_renew();
} // tud_network_recv_renew

/**
 * Set the link state and send notification to host
 */
void tud_network_link_state(uint8_t rhport, bool is_up) {
  TU_LOG_DRV("tud_network_link_state(%d, %d)\n", rhport, is_up);

  if (ncm_interface.link_is_up == is_up) {
    // No change in link state
    return;
  }

  ncm_interface.link_is_up = is_up;

  // Only send notification if we have an active data interface
  if (ncm_interface.itf_data_alt != 1) {
    TU_LOG_DRV("  link state notification skipped (interface not active)\n");
    return;
  }

  // Reset notification state to send speed change notification first, then link state notification
  ncm_interface.notification_xmit_state = NOTIFICATION_SPEED;

  // Trigger notification transmission
  notification_xmit(rhport, false);
}

//-----------------------------------------------------------------------------
//
// all the netd_*() stuff (interface TinyUSB -> driver)
//
/**
 * Initialize the driver data structures.
 * Might be called several times.
 */
void netd_init(void) {
  TU_LOG_DRV("netd_init()\n");

  memset(&ncm_interface, 0, sizeof(ncm_interface));

  ncm_interface.xmit_max_ntb_size = CFG_TUD_NCM_IN_NTB_MAX_SIZE;
  ncm_interface.xmit_max_datagrams = CFG_TUD_NCM_IN_MAX_DATAGRAMS_PER_NTB;
  ncm_interface.max_datagram_size = CFG_TUD_NET_MTU;

  // Seed the NCM_GET_NET_ADDRESS response with the device's actual MAC so a
  // host that queries it before ever calling NCM_SET_NET_ADDRESS (this
  // descriptor advertises NCM_NETWORK_CAPS_NET_ADDRESS) gets a valid
  // 6-byte address instead of a 0-length reply.
  memcpy(ncm_interface.net_address, tud_network_mac_address, sizeof(tud_network_mac_address));
  ncm_interface.net_address_len = sizeof(tud_network_mac_address);

  for (int i = 0; i < XMIT_NTB_N; ++i) {
    ncm_interface.xmit_free_ntb[i] = &ncm_epbuf.xmit[i].ntb;
  }
  for (int i = 0; i < RECV_NTB_N; ++i) {
    ncm_interface.recv_free_ntb[i] = &ncm_epbuf.recv[i].ntb;
  }
  ncm_interface.link_is_up = tud_network_default_link_state_cb();

  // s_xmit_inflight_since_millis/s_xmit_stall_reported_for_start are file-static,
  // not part of ncm_interface, so the memset above does not touch them. Clear
  // them explicitly: netd_init() runs on every USB reset (via netd_reset()), and
  // a stale nonzero timestamp surviving a reset would make
  // tud_network_ncm_tx_stalled() report a phantom stall (it only checks elapsed
  // time, not whether xmit_tinyusb_ntb -- now NULL -- is actually in flight),
  // triggering an unwanted BSP watchdog reconnect/reset cycle right after boot.
  s_xmit_inflight_since_millis = 0;
  s_xmit_stall_reported_for_start = 0;
} // netd_init

/**
 * Deinit driver
 */
bool netd_deinit(void) {
  return true;
}

/**
 * Resets the port.
 * In this driver this is the same as netd_init()
 */
void netd_reset(uint8_t rhport) {
  (void) rhport;

  netd_init();
} // netd_reset

/**
 * Open the USB interface.
 * - parse the USB descriptor \a TUD_CDC_NCM_DESCRIPTOR for itfnum and endpoints
 * - a specific order of elements in the descriptor is tested.
 *
 * \note
 *   Actually all of the information could be read directly from \a itf_desc, because the
 *   structure and the values are well known.  But we do it this way.
 *
 * \post
 * - \a itf_num set
 * - \a ep_notif, \a ep_in and \a ep_out are set
 * - USB interface is open
 */
uint16_t netd_open(uint8_t rhport, tusb_desc_interface_t const *itf_desc, uint16_t max_len) {
  TU_ASSERT(ncm_interface.ep_notif == 0, 0);// assure that the interface is only opened once

  ncm_interface.itf_num = itf_desc->bInterfaceNumber;// management interface

  uint16_t drv_len = sizeof(tusb_desc_interface_t);
  uint8_t const *p_desc = tu_desc_next(itf_desc);
  while (tu_desc_type(p_desc) == TUSB_DESC_CS_INTERFACE && drv_len <= max_len) {
    if (tu_desc_subtype(p_desc) == CDC_FUNC_DESC_NCM) {
      TU_ASSERT(tu_desc_len(p_desc) >= sizeof(tusb_desc_cdc_ncm_func_t), 0);
      tusb_desc_cdc_ncm_func_t const *ncm_func = (tusb_desc_cdc_ncm_func_t const *) p_desc;
      ncm_interface.bm_capabilities = ncm_func->bmCapabilities;
    }
    drv_len += tu_desc_len(p_desc);
    p_desc = tu_desc_next(p_desc);
  }

  // get notification endpoint
  TU_ASSERT(tu_desc_type(p_desc) == TUSB_DESC_ENDPOINT, 0);
  TU_ASSERT(usbd_edpt_open(rhport, (tusb_desc_endpoint_t const *) p_desc), 0);
  ncm_interface.ep_notif = ((tusb_desc_endpoint_t const *) p_desc)->bEndpointAddress;
  drv_len += tu_desc_len(p_desc);
  p_desc = tu_desc_next(p_desc);

  // skip the following TUSB_DESC_INTERFACE entries (which must be TUSB_CLASS_CDC_DATA)
  while (tu_desc_type(p_desc) == TUSB_DESC_INTERFACE && drv_len <= max_len) {
    tusb_desc_interface_t const *data_itf_desc = (tusb_desc_interface_t const *) p_desc;
    TU_ASSERT(data_itf_desc->bInterfaceClass == TUSB_CLASS_CDC_DATA, 0);

    drv_len += tu_desc_len(p_desc);
    p_desc = tu_desc_next(p_desc);
  }

  // a TUSB_DESC_ENDPOINT (actually two) must follow, open these endpoints
  TU_ASSERT(tu_desc_type(p_desc) == TUSB_DESC_ENDPOINT, 0);
  TU_ASSERT(usbd_open_edpt_pair(rhport, p_desc, 2, TUSB_XFER_BULK, &ncm_interface.ep_out, &ncm_interface.ep_in));
  ncm_interface.ep_size = tu_edpt_packet_size((tusb_desc_endpoint_t const *) p_desc);
  drv_len += 2 * sizeof(tusb_desc_endpoint_t);

  return drv_len;
} // netd_open

/**
 * Handle TinyUSB requests to process transfer events.
 */
bool netd_xfer_cb(uint8_t rhport, uint8_t ep_addr, xfer_result_t result, uint32_t xferred_bytes) {
  (void) result;

  if (ep_addr == ncm_interface.ep_out) {
    // new NTB received
    // - make the NTB valid
    // - if ready transfer datagrams to the glue logic for further processing
    // - if there is a free receive buffer, initiate reception
    if (ncm_interface.recv_tinyusb_ntb == NULL) {
      // Completion for a transfer whose owning NTB was already reclaimed
      // (e.g. an alt-setting flap). Nothing to validate -- just re-arm. (T-312)
      TU_LOG_DRV("(WW) ep_out xfer complete with no owning NTB, ignoring\n");
    } else if (!recv_validate_datagram(ncm_interface.recv_tinyusb_ntb, xferred_bytes)) {
      // verification failed: ignore NTB and return it to free
      TU_LOG_DRV("Invalid datatagram. Ignoring NTB\n");
      recv_put_ntb_into_free_list(ncm_interface.recv_tinyusb_ntb);
    } else {
      // packet ok -> put it into ready list
      ncm_interface.host_sent_datagram = true;
      recv_put_ntb_into_ready_list(ncm_interface.recv_tinyusb_ntb);
    }
    ncm_interface.recv_tinyusb_ntb = NULL;
    tud_network_recv_renew_r(rhport);
  } else if (ep_addr == ncm_interface.ep_in) {
    // transmission of an NTB finished
    // - free the transmitted NTB buffer
    // - insert ZLPs when necessary
    // - if there is another transmit NTB waiting, try to start transmission
    xmit_put_ntb_into_free_list(ncm_interface.xmit_tinyusb_ntb);
    ncm_interface.xmit_tinyusb_ntb = NULL;
    s_xmit_inflight_since_millis = 0;
    s_xmit_stall_reported_for_start = 0;
    if (!xmit_insert_required_zlp(rhport, xferred_bytes)) {
      xmit_start_if_possible(rhport);
    }
  } else if (ep_addr == ncm_interface.ep_notif) {
    // next transfer on notification channel
    notification_xmit(rhport, true);
  }

  return true;
} // netd_xfer_cb

/**
 * Respond to TinyUSB control requests.
 * At startup transmission of notification packets are done here.
 */
bool netd_control_xfer_cb(uint8_t rhport, uint8_t stage, tusb_control_request_t const *request) {

  switch (request->bmRequestType_bit.type) {
    case TUSB_REQ_TYPE_STANDARD:
      if (stage != CONTROL_STAGE_SETUP) {
        return true;
      }

      switch (request->bRequest) {
        case TUSB_REQ_GET_INTERFACE: {
          TU_VERIFY(ncm_interface.itf_num + 1 == request->wIndex, false);

          tud_control_xfer(rhport, request, &ncm_interface.itf_data_alt, 1);
        } break;

        case TUSB_REQ_SET_INTERFACE: {
          TU_VERIFY(ncm_interface.itf_num + 1 == request->wIndex && request->wValue < 2, false);

          uint8_t prev_alt = ncm_interface.itf_data_alt;
          ncm_interface.itf_data_alt = (uint8_t) request->wValue;
          if (prev_alt != ncm_interface.itf_data_alt) {
            // Alt-setting flap: discard stale in-flight NTB state so a later
            // completion callback cannot act on a buffer this reset already
            // freed (T-312 HardFault on Linux NCM alt-setting flap).
            ncm_flush_data_paths();
          }

          if (ncm_interface.itf_data_alt == 1) {
            ncm_interface.host_config_blocked_tries = 0;
            tud_network_recv_renew_r(rhport);
            notification_xmit(rhport, false);
          } else {
            // Reset notification state to send link state update when interface is re-activated
            ncm_interface.notification_xmit_state = NOTIFICATION_SPEED;
            ncm_interface.host_sent_datagram = false;
            ncm_interface.host_config_blocked_tries = 0;
            // Also clear packet_filter: otherwise a stale nonzero value from
            // the previous session survives an alt=1->0->1 flap and makes
            // ncm_host_strictly_configured_for_tx() report "ready" on the new
            // session before the host has reprogrammed the filter.
            ncm_interface.packet_filter = 0;
          }
          tud_control_status(rhport, request);
        } break;

        // unsupported request
        default:
          return false;
      }
      break;

    case TUSB_REQ_TYPE_CLASS:
      // Be permissive: some hosts issue NCM class requests using either the
      // control interface index or the associated data interface index.
      TU_VERIFY((request->wIndex == ncm_interface.itf_num) ||
                (request->wIndex == (uint16_t) (ncm_interface.itf_num + 1)), false);
      switch (request->bRequest) {
        case NCM_GET_NTB_PARAMETERS: {
          if (stage != CONTROL_STAGE_SETUP) {
            return true;
          }
          // transfer NTB parameters to host.
          tud_control_xfer(rhport, request, (void *) (uintptr_t) &ntb_parameters, sizeof(ntb_parameters));
        } break;

        case NCM_SET_ETHERNET_PACKET_FILTER: {
          if (stage != CONTROL_STAGE_SETUP) {
            return true;
          }

          // Some hosts issue this request even if ETH_FILTER is not advertised,
          // see https://bugzilla.kernel.org/show_bug.cgi?id=217290

          ncm_interface.packet_filter = request->wValue;
          tud_network_set_packet_filter_cb(request->wValue);
          tud_control_xfer(rhport, request, NULL, 0);

          // Some hosts (including macOS) expect/benefit from a fresh
          // NETWORK_CONNECTION notification after packet filter programming.
          if (ncm_interface.link_is_up) {
            ncm_interface.notification_xmit_state = NOTIFICATION_CONNECTED;
            notification_xmit(rhport, false);
          }

          // Host is now ready to receive payload traffic.
          xmit_start_if_possible(rhport);
        } break;

        case NCM_SET_ETHERNET_MULTICAST_FILTERS:
        case NCM_SET_ETHERNET_POWER_MANAGEMENT_PATTERN_FILTER: {
          if (stage != CONTROL_STAGE_SETUP) {
            return true;
          }
          uint16_t xfer_len = request->wLength;
          if (xfer_len > sizeof(ncm_interface.class_request_data)) {
            xfer_len = sizeof(ncm_interface.class_request_data);
          }

          if (xfer_len > 0) {
            tud_control_xfer(rhport, request, (void *) (uintptr_t) &ncm_interface.class_request_data, xfer_len);
          } else {
            tud_control_xfer(rhport, request, NULL, 0);
          }
        } break;

        case NCM_GET_ETHERNET_POWER_MANAGEMENT_PATTERN_FILTER:
        case NCM_GET_ETHERNET_STATISTIC: {
          if (stage != CONTROL_STAGE_SETUP) {
            return true;
          }
          uint16_t xfer_len = request->wLength;
          if (xfer_len > sizeof(ncm_interface.class_request_data)) {
            xfer_len = sizeof(ncm_interface.class_request_data);
          }

          tu_memclr(ncm_interface.class_request_data, xfer_len);
          tud_control_xfer(rhport, request, (void *) (uintptr_t) &ncm_interface.class_request_data, xfer_len);
        } break;

        case NCM_GET_NET_ADDRESS: {
          if (stage != CONTROL_STAGE_SETUP) {
            return true;
          }
          tud_control_xfer(rhport, request, (void *) (uintptr_t) ncm_interface.net_address,
                           ncm_interface.net_address_len);
        } break;

        case NCM_SET_NET_ADDRESS: {
          if (stage != CONTROL_STAGE_SETUP) {
            return true;
          }
          TU_VERIFY(request->wLength <= sizeof(ncm_interface.net_address), false);
          ncm_interface.net_address_len = request->wLength;
          tud_control_xfer(rhport, request, (void *) (uintptr_t) ncm_interface.net_address,
                           ncm_interface.net_address_len);
        } break;

        case NCM_GET_NTB_FORMAT: {
          if (stage != CONTROL_STAGE_SETUP) {
            return true;
          }
          tud_control_xfer(rhport, request, (void *) (uintptr_t) &ncm_interface.ntb_format,
                           sizeof(ncm_interface.ntb_format));
        } break;

        case NCM_SET_NTB_FORMAT: {
          if (stage != CONTROL_STAGE_SETUP) {
            return true;
          }
          if (request->wLength != 0 || request->wValue != 0) {
            return false;
          }
          ncm_interface.ntb_format = 0;
          tud_control_status(rhport, request);
        } break;

        case NCM_GET_MAX_DATAGRAM_SIZE: {
          if (stage != CONTROL_STAGE_SETUP) {
            return true;
          }
          tud_control_xfer(rhport, request, (void *) (uintptr_t) &ncm_interface.max_datagram_size,
                           sizeof(ncm_interface.max_datagram_size));
        } break;

        case NCM_SET_MAX_DATAGRAM_SIZE: {
          // The DATA stage of this OUT request is copied into our buffer
          // asynchronously by the control-transfer machinery, after this
          // callback returns for the SETUP stage -- the destination must be
          // a persistent field (ncm_interface.max_datagram_size), not a
          // stack local, or the write lands on stale/reused stack memory
          // once the SETUP-stage call frame is gone. Mirrors the existing
          // NCM_SET_NTB_INPUT_SIZE pattern above.
          if (stage == CONTROL_STAGE_SETUP) {
            TU_VERIFY(request->wLength == sizeof(ncm_interface.max_datagram_size), false);
            tud_control_xfer(rhport, request, (void *) (uintptr_t) &ncm_interface.max_datagram_size,
                             sizeof(ncm_interface.max_datagram_size));
          } else if (stage == CONTROL_STAGE_DATA) {
            if (ncm_interface.max_datagram_size == 0 || ncm_interface.max_datagram_size > CFG_TUD_NET_MTU) {
              ncm_interface.max_datagram_size = CFG_TUD_NET_MTU;
            }
          }
        } break;

        case NCM_GET_CRC_MODE: {
          if (stage != CONTROL_STAGE_SETUP) {
            return true;
          }
          tud_control_xfer(rhport, request, (void *) (uintptr_t) &ncm_interface.crc_mode,
                           sizeof(ncm_interface.crc_mode));
        } break;

        case NCM_SET_CRC_MODE: {
          if (stage != CONTROL_STAGE_SETUP) {
            return true;
          }
          // CRC_MODE is not advertised in NCM_CAPABILITIES (no actual CRC
          // append/validate exists in the TX/RX datapath), so a compliant
          // host will never issue this with wValue=1. Reject it explicitly
          // rather than silently ACKing a mode the device cannot honor, in
          // case a host tries it anyway without checking the capability bit.
          if (request->wLength != 0 || request->wValue != 0) {
            return false;
          }
          ncm_interface.crc_mode = 0;
          tud_control_status(rhport, request);
        } break;

        case NCM_GET_NTB_INPUT_SIZE: {
          if (stage != CONTROL_STAGE_SETUP) {
            return true;
          }

          TU_VERIFY(request->wLength >=4, false);

          uint8_t resp_len = (request->wLength >= 8 && (ncm_interface.bm_capabilities & NCM_NETWORK_CAPS_NTB_INPUT_SIZE)) ? 8 : 4;

          ncm_ntb_input_size_t ntb_input_size = {
            .dwNtbInMaxSize = ncm_interface.xmit_max_ntb_size,
            .wNtbInMaxDatagrams = ncm_interface.xmit_max_datagrams
          };
          tud_control_xfer(rhport, request, &ntb_input_size, resp_len);
        } break;

        case NCM_SET_NTB_INPUT_SIZE: {
          if (stage == CONTROL_STAGE_SETUP) {
            /* wLength == 8 -> the NTB Input Size Structure (if NCM_NETWORK_CAPS_NTB_INPUT_SIZE is set)
               wLength == 4 -> dwNtbInMaxSize field of the NTB Input Size Structure. */
            TU_VERIFY(request->wLength == 4 || request->wLength == 8, false);
            if (request->wLength == 8) {
              TU_VERIFY(ncm_interface.bm_capabilities & NCM_NETWORK_CAPS_NTB_INPUT_SIZE, false);
            }

            tu_memclr(&ncm_interface.ntb_input_size, sizeof(ncm_interface.ntb_input_size));
            tud_control_xfer(rhport, request, &ncm_interface.ntb_input_size, request->wLength);
          } else if (stage == CONTROL_STAGE_DATA) {
            /* CDC-NCM 1.0 Table 6-4, up to NTB16 size */
            const uint32_t requested_size = ncm_interface.ntb_input_size.dwNtbInMaxSize;
            if (requested_size < 2048u || requested_size > 65535u) {
              return false;
            }
            ncm_interface.xmit_max_ntb_size = tu_min16(requested_size, CFG_TUD_NCM_IN_NTB_MAX_SIZE);

            if (ncm_interface.ntb_input_size.wNtbInMaxDatagrams == 0 || ncm_interface.ntb_input_size.wNtbInMaxDatagrams > CFG_TUD_NCM_IN_MAX_DATAGRAMS_PER_NTB) {
              ncm_interface.xmit_max_datagrams = CFG_TUD_NCM_IN_MAX_DATAGRAMS_PER_NTB;
            } else {
              ncm_interface.xmit_max_datagrams = ncm_interface.ntb_input_size.wNtbInMaxDatagrams;
            }
          }
        } break;

        // unsupported request
        default:
          return false;
      }
      break;

    // unsupported request
    default:
      return false;
  }

  return true;
} // netd_control_xfer_cb

#endif // ( CFG_TUD_ENABLED && CFG_TUD_NCM )
