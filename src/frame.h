#pragma once

#include <stddef.h>
#include <stdint.h>

/* The host <-> modem frame contract, and nothing else.
 *
 *   on the wire:   COBS(length || body || crc8(body)) || 0x00
 *   body:          [rssi:int16le][snr:int8][payload]   modem -> host
 *                  [payload]                           host  -> modem
 *
 * `length` counts the body bytes and is not covered by the CRC; it does not
 * need to be, because COBS decoding yields an exact byte count and the receiver
 * checks the two agree. A corrupted length byte fails that check; corruption
 * inside the body is what the CRC catches. Either way the frame is dropped --
 * nothing at this layer repairs, re-requests or acknowledges anything.
 *
 * The payload is opaque. Nothing here reads a byte of it.
 *
 * No Arduino, no radio, no project. This file would be unchanged if the link
 * carried weather data.
 */

/* COBS guarantees the encoded body never contains one of these. */
static const uint8_t FRAME_DELIMITER = 0x00;

/* The length field is one byte, so this is the hard ceiling on a body. */
static const size_t FRAME_MAX_BODY = 255;

/* rssi (int16, little-endian, dBm) + snr (int8, quarter-dB steps). */
static const size_t FRAME_LINK_STATS_BYTES = 3;

/* An inbound body is 3 + payload and a body caps at 255, so a 253-byte payload
 * could be transmitted but could never be delivered. The ceiling is therefore
 * 252 in *both* directions, and a transmitter that accepts 255 is building
 * frames that are structurally undeliverable. */
static const size_t FRAME_MAX_PAYLOAD = FRAME_MAX_BODY - FRAME_LINK_STATS_BYTES;

static const size_t FRAME_MAX_LOGICAL = FRAME_MAX_BODY + 2; /* length + body + crc */
static const size_t FRAME_MAX_WIRE =
    FRAME_MAX_LOGICAL + (FRAME_MAX_LOGICAL / 254 + 1) + 1; /* + COBS + delimiter */

/* Why every frame that failed was thrown away. Diagnostics only: no branch
 * anywhere depends on these, and every one of them counts a drop. */
struct FrameStats {
    uint32_t frames;          /* delivered intact */
    uint32_t crc_errors;      /* body corrupted on the UART hop */
    uint32_t cobs_errors;     /* malformed encoding: zero code byte, short block */
    uint32_t length_errors;   /* length byte disagrees with the decoded size */
    uint32_t oversize_errors; /* body larger than the configured ceiling */
    uint32_t resyncs;         /* no delimiter within one frame's worth of bytes */
};

/* Builds one delimited wire frame carrying `payload` plus the mandatory
 * link-stats header (R3). `snr_q` is in quarter-dB steps, straight off the
 * SX127x PktSnrValue register, so the host divides by 4.
 *
 * Returns bytes written to `out`, or 0 if the payload is undeliverable or the
 * buffer is too small. `out` needs FRAME_MAX_WIRE bytes.
 */
size_t frameEncodeWithLinkStats(const uint8_t *payload, uint8_t len, int16_t rssi,
                                int8_t snr_q, uint8_t *out, size_t out_cap);

/* Feed it bytes off the host port, take whole validated frames out.
 *
 * Self-resynchronising: a corrupt or truncated frame costs at most the bytes
 * between two delimiters, and parsing picks straight back up at the next one.
 */
class FrameParser {
public:
    explicit FrameParser(uint8_t max_payload);

    /* Returns the payload length when `b` completed a valid frame, or -1.
     * The payload lives in payload() until the next call. */
    int feed(uint8_t b);

    const uint8_t *payload() const { return _logical + 1; }
    const FrameStats &stats() const { return _stats; }

private:
    void fail(uint32_t FrameStats::*counter);

    uint8_t _max_payload;
    uint8_t _wire[FRAME_MAX_WIRE];
    uint8_t _logical[FRAME_MAX_LOGICAL];
    size_t _wire_len;
    FrameStats _stats;
};
