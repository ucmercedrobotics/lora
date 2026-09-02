#include "radio.h"

#include <Arduino.h>
#include <LoRa.h>
#include <math.h>
#include <SPI.h>

#include "config.h"
#include "frame.h"

namespace {

/* True between handing a packet to the radio and confirming it has left.
 *
 * arduino-LoRa keeps isTransmitting() private, but beginPacket() returns 0
 * while the radio is on air, so it doubles as the query -- and when it does
 * succeed it has already put the radio in standby with the FIFO pointer reset,
 * which is exactly the state the next write wants. `g_primed` remembers that,
 * so the SPI work is never done twice. */
bool g_tx_pending = false;
bool g_primed = false;

uint32_t g_oversize_dropped = 0;

const RadioSettings g_settings = {
    LORA_SPREADING_FACTOR,
    LORA_BANDWIDTH_HZ,
    LORA_CODING_RATE,
    LORA_PREAMBLE_SYMBOLS,
    true, /* arduino-LoRa transmits in explicit-header mode */
    true, /* R5, below */
};

/* True once the radio is idle and primed for a write. */
bool claimRadio()
{
    if (g_primed) {
        return true;
    }
    if (LoRa.beginPacket() == 0) {
        return false;
    }
    g_primed = true;
    return true;
}

} // namespace

bool Radio::begin()
{
#if defined(LORA_SS_PIN)
    LoRa.setPins(LORA_SS_PIN, LORA_RESET_PIN, LORA_DIO0_PIN);
#endif

#if defined(LORA_BOOT0) && defined(LORA_RESET)
    /* MKR WAN boards: the SX1276 lives inside a Murata module behind its own
     * STM32, which has to be held out of the bootloader and reset before the
     * radio will answer on SPI1. Boards that define neither pin skip this and
     * use the library's own reset line. */
    pinMode(LORA_BOOT0, OUTPUT);
    digitalWrite(LORA_BOOT0, LOW);
    pinMode(LORA_RESET, OUTPUT);
    digitalWrite(LORA_RESET, HIGH);
    delay(200);
    digitalWrite(LORA_RESET, LOW);
    delay(200);
    digitalWrite(LORA_RESET, HIGH);
    delay(50);
#endif

    if (!LoRa.begin(LORA_FREQUENCY_HZ)) {
        return false;
    }

    /* R4: assigned, never inherited. begin() sets none of these, so leaving one
     * out means running on an SX127x reset default that nobody chose and nobody
     * wrote down -- and one of them silently decides the legal payload size.
     *
     * Spreading factor before bandwidth, and not by accident: setSignalBandwidth()
     * is what recomputes the low-data-rate-optimize flag, and it reads whatever
     * spreading factor is set at the time. Reverse these two lines and SF11/SF12
     * at 125 kHz come up with LDRO off, where crystal drift across a single
     * symbol breaks demodulation. */
    LoRa.setSpreadingFactor(LORA_SPREADING_FACTOR);
    LoRa.setSignalBandwidth(LORA_BANDWIDTH_HZ);
    LoRa.setCodingRate4(LORA_CODING_RATE);
    LoRa.setPreambleLength(LORA_PREAMBLE_SYMBOLS);
    LoRa.setSyncWord(LORA_SYNC_WORD); /* R8 */
    LoRa.setTxPower(LORA_TX_POWER_DBM, LORA_PA_OUTPUT_PIN);

    /* R5. The SX127x powers up with RxPayloadCrcOn = 0 and begin() does not
     * change it. With it off, a packet corrupted over the air is handed
     * straight to the host, and the only thing between it and a published
     * frame is our 8-bit CRC -- which misses roughly 1 corruption in 256. That
     * CRC exists to protect the serial hop and was never sized to be the air
     * link's only defence. */
    LoRa.enableCrc();

    /* No receive() here on purpose. parsePacket() arms the radio itself, in
     * single-receive mode, and re-arms on the next call -- so a receive() would
     * be undone by the first poll rather than adding anything. The cost of the
     * polled idiom is a deaf window of one loop iteration each time single
     * receive times out; the loop never blocks, so that window is microseconds
     * against a timeout of ~100 symbols. */
    return true;
}

bool Radio::trySend(const uint8_t *payload, uint8_t len)
{
    if (g_tx_pending) {
        if (LoRa.beginPacket() == 0) {
            return false; /* still on air */
        }
        g_tx_pending = false;
        g_primed = true;
    }
    if (!claimRadio()) {
        return false;
    }

    LoRa.write(payload, len);
    /* Asynchronous: this returns as soon as the radio is started rather than
     * parking here for the length of the transmission. The caller goes back to
     * draining the host port, which is the whole of R7. */
    LoRa.endPacket(true);

    g_primed = false;
    g_tx_pending = true;
    return true;
}

int Radio::tryReceive(uint8_t *out, uint8_t cap, int16_t *rssi, int8_t *snr_q)
{
    if (g_tx_pending) {
        if (LoRa.beginPacket() == 0) {
            return -1; /* half-duplex: deaf until the transmission finishes */
        }
        g_tx_pending = false;
    }
    /* parsePacket() resets the FIFO pointer and puts the radio back in receive,
     * undoing anything beginPacket() primed. */
    g_primed = false;

    const int size = LoRa.parsePacket();
    if (size <= 0) {
        return -1;
    }

    /* R2 again, in the other direction. A payload past the ceiling cannot be
     * delivered -- an inbound body is 3 + payload and the length byte caps the
     * body at 255 -- so it is dropped whole rather than clipped into something
     * that would decode as valid. */
    if ((size_t)size > cap || (size_t)size > FRAME_MAX_PAYLOAD) {
        while (LoRa.available()) {
            (void)LoRa.read();
        }
        g_oversize_dropped++;
        return -1;
    }

    int len = 0;
    while (LoRa.available() && len < size) {
        out[len++] = (uint8_t)LoRa.read();
    }

    int packet_rssi = LoRa.packetRssi();
    if (packet_rssi > 32767) {
        packet_rssi = 32767;
    } else if (packet_rssi < -32768) {
        packet_rssi = -32768;
    }
    *rssi = (int16_t)packet_rssi;

    /* Quarter-dB steps, which is the raw SX127x PktSnrValue scale; the host
     * divides by 4. Representable range is -32.00 .. +31.75 dB. */
    long q = lroundf(LoRa.packetSnr() * 4.0f);
    if (q > 127) {
        q = 127;
    } else if (q < -128) {
        q = -128;
    }
    *snr_q = (int8_t)q;

    return len;
}

uint32_t Radio::oversizeDropped()
{
    return g_oversize_dropped;
}

const RadioSettings &Radio::settings()
{
    return g_settings;
}
