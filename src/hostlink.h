#pragma once

#include <stdint.h>

#include "config.h"
#include "frame.h"
#include "queue.h"

/* The framed serial port to the host. Frames in, frames out, nothing else.
 *
 * Both directions are non-blocking, which is R7's whole point: a modem that
 * stalls here while the radio is busy loses host bytes it was never told
 * about. At 115200 baud a 318 ms transmit is ~3.6 kB of arriving data against
 * a 64-byte UART buffer, so "drain it later" is not a strategy.
 */

typedef FrameQueue<LORA_TX_QUEUE_DEPTH, LORA_MAX_PAYLOAD_BYTES> TxQueue;

namespace HostLink {

void begin();

/* Reads whatever the port is holding and pushes every validated payload onto
 * `out`. Call it often and unconditionally -- including while the radio is
 * mid-transmit, which is when it matters. */
void drain(TxQueue &out);

/* Hands one already-encoded frame to the outbound buffer. Returns false if
 * there was no room, in which case the frame is dropped whole and counted; a
 * partial frame would be a guaranteed parse error at the far end, which is
 * strictly worse than one the host never hears about. */
bool send(const uint8_t *wire, size_t len);

/* Pushes out as many buffered bytes as the port will take without waiting. */
void flush();

const FrameStats &parserStats();
uint32_t outboundDropped();

} // namespace HostLink
