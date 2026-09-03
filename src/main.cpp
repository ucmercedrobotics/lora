/* A LoRa modem.
 *
 * It exposes a radio to a host over one UART, as framed opaque bytes, and it
 * knows nothing else. No message types, no addressing, no acknowledgements, no
 * deduplication, no retries, no sequence numbers, no reassembly. The payload is
 * bytes; what they mean is the host's business. The test for whether something
 * belongs in this firmware is whether it would still be here if the link
 * carried weather data.
 *
 * Wire format and the numbered requirements this implements are in the host's
 * frame contract; the short version is COBS(length || body || crc8(body)) || 0x00,
 * with a mandatory rssi/snr header on everything sent up to the host.
 *
 * The loop never blocks. That is not a style preference: the radio is
 * half-duplex and slow, the host UART is two orders of magnitude faster, and a
 * modem that waits for the channel drops host bytes it was never told about.
 */

#include <Arduino.h>

#include "airtime.h"
#include "config.h"
#include "crc8.h"
#include "debug.h"
#include "frame.h"
#include "hostlink.h"
#include "queue.h"
#include "radio.h"

static_assert(LORA_MAX_PAYLOAD_BYTES <= FRAME_MAX_PAYLOAD,
              "LORA_MAX_PAYLOAD_BYTES exceeds 252: an inbound body is 3 + payload "
              "and the length byte caps the body at 255, so such a frame could be "
              "transmitted but could never be delivered");
static_assert(LORA_MAX_PAYLOAD_BYTES > 0, "LORA_MAX_PAYLOAD_BYTES must be positive");

namespace {

TxQueue g_tx_queue;
uint8_t g_rx_payload[FRAME_MAX_PAYLOAD];
uint8_t g_rx_wire[FRAME_MAX_WIRE];

uint32_t g_last_stats_ms = 0;

/* Packets accepted off the air. The counter line reports every reason a frame
 * was thrown away but had no count of the ones that arrived, which makes "the
 * radio heard nothing" and "the radio heard it and the host link lost it"
 * look identical from the debug port. */
uint32_t g_air_rx = 0;

/* Packets the radio accepted for transmission. Distinguishes "the host frame
 * never reached the radio" from "the radio took it and nothing came out". */
uint32_t g_air_tx = 0;

#if LORA_SELFTEST_MS > 0
uint32_t g_last_selftest_ms = 0;
uint32_t g_selftest_seq = 0;
#endif

bool g_led_on = false;
uint32_t g_led_off_at_ms = 0;

/* Non-blocking pulse: turns LED_BUILTIN on now and lets serviceLed() turn it
 * back off after LORA_LED_FLASH_MS, so a TX/RX event is visible without ever
 * calling delay() (R7) or leaving the LED latched on. */
void flashLed()
{
    digitalWrite(LED_BUILTIN, HIGH);
    g_led_on = true;
    g_led_off_at_ms = millis() + LORA_LED_FLASH_MS;
}

void serviceLed()
{
    if (g_led_on && (int32_t)(millis() - g_led_off_at_ms) >= 0) {
        digitalWrite(LED_BUILTIN, LOW);
        g_led_on = false;
    }
}

void reportConfiguration()
{
    Stream &out = Debug::out();
    const RadioSettings &radio = Radio::settings();

    out.println();
    out.println(F("lora modem up"));
    out.print(F("  radio      SF"));
    out.print(radio.spreading_factor);
    out.print(F("/BW"));
    out.print(radio.bandwidth_hz / 1000);
    out.print(F("k/CR4-"));
    out.print(radio.coding_rate);
    out.print(F(" @ "));
    out.print(LORA_FREQUENCY_HZ / 1000000L);
    out.println(F(" MHz"));

    out.print(F("  sync word  0x"));
    out.println(LORA_SYNC_WORD, HEX);
    out.print(F("  tx power   "));
    out.print(LORA_TX_POWER_DBM);
    out.println(F(" dBm"));
    out.print(F("  ldro       "));
    out.println(lowDataRateOptimize(radio) ? F("on") : F("off"));

    /* The one number that has to be chosen rather than defaulted. Nothing in
     * the code will tell you when you have broken it, so it gets said out loud
     * at every boot. */
    const uint16_t max_payload = LORA_MAX_PAYLOAD_BYTES;
    const uint16_t legal = maxPayloadForDwell(radio, DWELL_LIMIT_SEC, FRAME_MAX_PAYLOAD);
    const float worst_ms = airtimeSec(max_payload, radio) * 1000.0f;

    out.print(F("  payload    "));
    out.print(max_payload);
    out.print(F(" B max, "));
    out.print(worst_ms, 1);
    out.println(F(" ms on air"));

    out.print(F("  dwell      "));
    if (worst_ms <= DWELL_LIMIT_SEC * 1000.0f) {
        out.print(F("ok, "));
        out.print(legal);
        out.println(F(" B fits the 400 ms budget"));
    } else if (legal == 0) {
        out.println(F("ILLEGAL: no payload fits 400 ms at this SF/BW."));
        out.println(F("             widen the bandwidth (BW500 has no dwell limit)"));
        out.println(F("             or drop the spreading factor."));
    } else {
        out.print(F("ILLEGAL: over the 400 ms budget. Largest legal payload is "));
        out.print(legal);
        out.println(F(" B."));
    }
}

void reportStats()
{
    const FrameStats &host = HostLink::parserStats();
    Stream &out = Debug::out();

    out.print(F("stats host_rx="));
    out.print(host.frames);
    out.print(F(" crc="));
    out.print(host.crc_errors);
    out.print(F(" cobs="));
    out.print(host.cobs_errors);
    out.print(F(" len="));
    out.print(host.length_errors);
    out.print(F(" oversize="));
    out.print(host.oversize_errors);
    out.print(F(" resync="));
    out.print(host.resyncs);
    out.print(F(" tx_queued="));
    out.print(g_tx_queue.count());
    out.print(F(" tx_dropped="));
    out.print(g_tx_queue.dropped());
    out.print(F(" air_tx="));
    out.print(g_air_tx);
    out.print(F(" air_rx="));
    out.print(g_air_rx);
    out.print(F(" air_oversize="));
    out.print(Radio::oversizeDropped());
    out.print(F(" host_tx_dropped="));
    out.println(HostLink::outboundDropped());
}

void serviceRadio()
{
    uint8_t len = 0;
    const uint8_t *frame = g_tx_queue.peek(&len);

    if (frame != NULL) {
        /* One host frame is one radio packet (R1). If the radio is still on
         * air the frame stays queued and we come back to it on the next pass.
         *
         * A backlog does keep the modem from listening, and that is not a
         * scheduling bug to fix: the radio is half-duplex, so while it is
         * transmitting there is nothing to hear either way. */
        if (Radio::trySend(frame, len)) {
            g_air_tx++;
            flashLed();
            g_tx_queue.pop();
        }
        return;
    }

    int16_t rssi = 0;
    int8_t snr_q = 0;
    const int received =
        Radio::tryReceive(g_rx_payload, sizeof(g_rx_payload), &rssi, &snr_q);
    if (received < 0) {
        return;
    }
    g_air_rx++;
    flashLed();

    /* R3: every inbound frame carries the link-stats header. Not conditional,
     * not configurable -- a bare frame and a stats-bearing one are
     * indistinguishable on the wire, because the payload is opaque and no byte
     * pattern can tell them apart. A host configured the other way would hand
     * three bytes of radio telemetry to its subscribers as data, with nothing
     * on either side able to notice. */
    const size_t wire = frameEncodeWithLinkStats(g_rx_payload, (uint8_t)received, rssi,
                                                 snr_q, g_rx_wire, sizeof(g_rx_wire));
    if (wire > 0) {
        HostLink::send(g_rx_wire, wire);
    }
}

} // namespace

void setup()
{
    pinMode(LED_BUILTIN, OUTPUT); /* TX/RX activity indicator, see flashLed() */
    digitalWrite(LED_BUILTIN, LOW);

    HostLink::begin();
    Debug::begin();

    /* A wrong CRC looks exactly like a bad cable from the host's side, and the
     * Dallas/Maxim CRC-8 in many Arduino libraries is a plausible wrong answer
     * here. Check the documented value once rather than debug it in the field. */
    const uint8_t check = crc8((const uint8_t *)"123456789", 9);
    if (check != 0xF4) {
        Debug::out().print(F("FATAL: crc8 check value is 0x"));
        Debug::out().println(check, HEX);
        Debug::out().println(F("       expected 0xF4 (CRC-8/ATM, poly 0x07)"));
        while (true) {
        }
    }

    if (!Radio::begin()) {
        Debug::out().println(F("FATAL: radio did not respond"));
        while (true) {
        }
    }

    reportConfiguration();
    g_last_stats_ms = millis();
}

void loop()
{
    /* First and unconditionally, including while the radio is mid-transmit
     * (R7). Everything below it is allowed to do nothing. */
    HostLink::drain(g_tx_queue);
    HostLink::flush();

    serviceRadio();
    serviceLed();

#if LORA_SELFTEST_MS > 0
    if (millis() - g_last_selftest_ms >= LORA_SELFTEST_MS) {
        g_last_selftest_ms = millis();
        uint8_t msg[24];
        const int n = snprintf((char *)msg, sizeof(msg), "selftest #%lu",
                               (unsigned long)++g_selftest_seq);
        if (n > 0) {
            g_tx_queue.push(msg, (uint8_t)n);
        }
    }
#endif

#if LORA_STATS_PERIOD_MS > 0
    const uint32_t now = millis();
    if (now - g_last_stats_ms >= LORA_STATS_PERIOD_MS) {
        g_last_stats_ms = now;
        reportStats();
    }
#endif
}
