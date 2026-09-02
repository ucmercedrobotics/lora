#include "frame.h"

#include <string.h>

#include "cobs.h"
#include "crc8.h"

size_t frameEncodeWithLinkStats(const uint8_t *payload, uint8_t len, int16_t rssi,
                                int8_t snr_q, uint8_t *out, size_t out_cap)
{
    if (len > FRAME_MAX_PAYLOAD) {
        return 0;
    }

    uint8_t logical[FRAME_MAX_LOGICAL];
    const uint8_t body_len = (uint8_t)(FRAME_LINK_STATS_BYTES + len);

    logical[0] = body_len;
    logical[1] = (uint8_t)(rssi & 0xFF);
    logical[2] = (uint8_t)((rssi >> 8) & 0xFF);
    logical[3] = (uint8_t)snr_q;
    memcpy(logical + 1 + FRAME_LINK_STATS_BYTES, payload, len);
    logical[1 + body_len] = crc8(logical + 1, body_len);

    const size_t encoded = cobsEncode(logical, (size_t)body_len + 2, out, out_cap);
    if (encoded == 0 || encoded + 1 > out_cap) {
        return 0;
    }
    out[encoded] = FRAME_DELIMITER;
    return encoded + 1;
}

FrameParser::FrameParser(uint8_t max_payload)
    : _max_payload(max_payload), _wire_len(0), _stats()
{
}

void FrameParser::fail(uint32_t FrameStats::*counter)
{
    _stats.*counter += 1;
}

int FrameParser::feed(uint8_t b)
{
    if (b != FRAME_DELIMITER) {
        if (_wire_len < sizeof(_wire)) {
            _wire[_wire_len++] = b;
        } else {
            /* A line that never delimits would otherwise sit here forever
             * holding a frame that can no longer become valid. Drop it and wait
             * for the next delimiter to re-anchor us.
             *
             * Sized to the protocol ceiling rather than to the configured
             * payload limit: shrinking it would shred an oversized-but-
             * well-formed frame into a resync, hiding the very mismatch
             * oversize_errors exists to report. */
            _wire_len = 0;
            fail(&FrameStats::resyncs);
        }
        return -1;
    }

    if (_wire_len == 0) {
        /* Repeated delimiters are legal padding, not an error -- firmware may
         * lead with one to flush a partial frame after a reset. */
        return -1;
    }

    const size_t wire_len = _wire_len;
    _wire_len = 0;

    size_t logical_len = 0;
    if (cobsDecode(_wire, wire_len, _logical, sizeof(_logical), &logical_len) != COBS_OK) {
        fail(&FrameStats::cobs_errors);
        return -1;
    }
    if (logical_len < 2) {
        fail(&FrameStats::length_errors);
        return -1;
    }

    const size_t body_len = logical_len - 2;
    if (_logical[0] != body_len) {
        fail(&FrameStats::length_errors);
        return -1;
    }
    if (crc8(_logical + 1, body_len) != _logical[logical_len - 1]) {
        fail(&FrameStats::crc_errors);
        return -1;
    }
    /* R2: oversize is dropped, not truncated. arduino-LoRa's write() silently
     * clamps to its own maximum, so a frame let through here would transmit
     * *successfully* as a truncated one and surface at the far end as a CRC
     * failure, looking for all the world like a bad air link. */
    if (body_len > _max_payload) {
        fail(&FrameStats::oversize_errors);
        return -1;
    }

    _stats.frames++;
    return (int)body_len;
}
