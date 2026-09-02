#include "airtime.h"

#include <math.h>

float symbolTimeSec(const RadioSettings &radio)
{
    return (float)(1UL << radio.spreading_factor) / (float)radio.bandwidth_hz;
}

bool lowDataRateOptimize(const RadioSettings &radio)
{
    return symbolTimeSec(radio) > 0.016f;
}

static float preambleTimeSec(const RadioSettings &radio)
{
    /* The 4.25 is the sync word, which rides on the end of the preamble. */
    return ((float)radio.preamble_symbols + 4.25f) * symbolTimeSec(radio);
}

static uint32_t payloadSymbols(uint16_t payload_bytes, const RadioSettings &radio)
{
    const int sf = radio.spreading_factor;
    const int de = lowDataRateOptimize(radio) ? 1 : 0;

    const int numerator = 8 * (int)payload_bytes - 4 * sf + 28 +
                          (radio.payload_crc ? 16 : 0) -
                          (radio.explicit_header ? 0 : 20);
    const int denominator = 4 * (sf - 2 * de);

    int blocks = (int)ceilf((float)numerator / (float)denominator) * radio.coding_rate;
    if (blocks < 0) {
        blocks = 0;
    }
    return (uint32_t)(8 + blocks);
}

float airtimeSec(uint16_t payload_bytes, const RadioSettings &radio)
{
    return preambleTimeSec(radio) +
           (float)payloadSymbols(payload_bytes, radio) * symbolTimeSec(radio);
}

uint16_t maxPayloadForDwell(const RadioSettings &radio, float dwell_sec,
                            uint16_t ceiling)
{
    uint16_t best = 0;
    for (uint16_t size = 0; size <= ceiling; size++) {
        if (airtimeSec(size, radio) > dwell_sec) {
            break;
        }
        best = size;
    }
    return best;
}
