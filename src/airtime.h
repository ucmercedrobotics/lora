#pragma once

#include <stdint.h>

/* LoRa time on air, from Semtech's formula (SX1276 datasheet 4.1.1.7).
 *
 * Pure arithmetic, run once at startup, and it answers the one question the
 * frame contract cannot answer on its own: how many payload bytes actually fit
 * inside the regulatory dwell budget at whatever spreading factor this build is
 * configured for. That number -- not the length byte, not the radio FIFO -- is
 * what really caps the payload, and nothing in the code will tell you when you
 * have broken it. So the modem computes it and says so.
 */

struct RadioSettings {
    uint8_t spreading_factor;
    uint32_t bandwidth_hz;
    uint8_t coding_rate; /* the 4/N denominator, 5..8 */
    uint16_t preamble_symbols;
    bool explicit_header;
    bool payload_crc;
};

/* FCC Part 15.247 hybrid-mode dwell limit: one transmission on one channel may
 * not exceed 400 ms. This binds narrowband (BW125) operation in the US 915 MHz
 * band. At BW500 the occupied bandwidth clears the 500 kHz threshold, the link
 * falls under the digital-modulation rules instead, and there is no dwell limit
 * at all -- four times the throughput for about 8 dB of link budget. */
static const float DWELL_LIMIT_SEC = 0.4f;

float symbolTimeSec(const RadioSettings &radio);

/* Mandatory once a symbol runs past 16 ms (SF11 and SF12 at 125 kHz), where
 * crystal drift across a single symbol breaks demodulation. It costs
 * throughput, which is why it belongs in the airtime figure rather than being
 * quietly ignored. */
bool lowDataRateOptimize(const RadioSettings &radio);

float airtimeSec(uint16_t payload_bytes, const RadioSettings &radio);

/* Largest payload whose airtime fits the budget; 0 if none does. Searched
 * rather than solved -- the symbol count is a ceiling division, so the closed
 * form needs care and this runs 253 times, once. */
uint16_t maxPayloadForDwell(const RadioSettings &radio, float dwell_sec,
                            uint16_t ceiling);
