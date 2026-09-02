#pragma once

#include <stdint.h>

#include "airtime.h"

/* The radio, reduced to two non-blocking calls.
 *
 * One host frame is one radio packet (R1). No chunking, no reassembly, no
 * accumulation across frames -- a modem that splits a frame into indexed chunks
 * has invented an unprotected reliability protocol, and with no sequence
 * recovery and no retransmit a single lost chunk silently *corrupts* a message
 * instead of dropping it. That is strictly worse than not sending it, which is
 * why an oversized payload is rejected here rather than fragmented.
 */
namespace Radio {

/* Applies every setting explicitly (R4) and turns on the PHY CRC (R5).
 * Returns false if the radio does not answer. */
bool begin();

/* Hands one payload to the radio if it is free. Returns false if the radio is
 * still on air, in which case nothing was consumed and the caller should try
 * again -- it never waits for the channel. */
bool trySend(const uint8_t *payload, uint8_t len);

/* Returns the payload length of a received packet, or -1 if none arrived --
 * including when the radio is still transmitting, since the link is half-duplex
 * and there is nothing to hear until it stops. `rssi` is dBm and `snr_q` is in
 * quarter-dB steps, ready for the link-stats header the contract requires on
 * every inbound frame (R3). */
int tryReceive(uint8_t *out, uint8_t cap, int16_t *rssi, int8_t *snr_q);

/* Packets off the air too large to deliver to the host. */
uint32_t oversizeDropped();

/* What this build is actually configured for, for the startup report. */
const RadioSettings &settings();

} // namespace Radio
