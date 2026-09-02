#include "hostlink.h"

#include <Arduino.h>

namespace {

FrameParser g_parser(LORA_MAX_PAYLOAD_BYTES);
ByteRing<LORA_HOST_TX_BUFFER_BYTES> g_outbound;

/* A ceiling on bytes consumed per call, so one very chatty host cannot hold
 * the loop long enough to starve the radio. Whatever is left stays in the
 * UART buffer and is picked up on the next pass, microseconds later. */
const size_t READ_CHUNK = 256;

} // namespace

void HostLink::begin()
{
    LORA_HOST_PORT.begin(LORA_HOST_BAUD);
    /* Deliberately no wait-for-port loop. On a USB CDC port that blocks
     * forever whenever nobody has opened the other end, which turns "the host
     * was not running yet" into "the modem is dead". */
}

void HostLink::drain(TxQueue &out)
{
    for (size_t i = 0; i < READ_CHUNK && LORA_HOST_PORT.available() > 0; i++) {
        const int b = LORA_HOST_PORT.read();
        if (b < 0) {
            break;
        }
        const int len = g_parser.feed((uint8_t)b);
        if (len >= 0) {
            out.push(g_parser.payload(), (uint8_t)len);
        }
    }
}

bool HostLink::send(const uint8_t *wire, size_t len)
{
    const bool queued = g_outbound.pushAll(wire, len);
    flush();
    return queued;
}

void HostLink::flush()
{
    /* availableForWrite() is what keeps this from blocking: write only as much
     * as the port has already promised to take. */
    while (!g_outbound.empty() && LORA_HOST_PORT.availableForWrite() > 0) {
        LORA_HOST_PORT.write(g_outbound.peek());
        g_outbound.pop();
    }
}

const FrameStats &HostLink::parserStats()
{
    return g_parser.stats();
}

uint32_t HostLink::outboundDropped()
{
    return g_outbound.dropped();
}
