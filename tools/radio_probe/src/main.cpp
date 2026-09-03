/* Exercises the modem's real Radio layer with its output on USB.
 *
 * tools/rf_probe talks to the SX1276 through the library directly and the two
 * boards hear each other. This one calls src/radio.cpp -- the same file the
 * modem firmware links, symlinked rather than copied -- through the same
 * peek/trySend/tryReceive sequence main.cpp uses, including the early return
 * when the queue is not empty. Everything else is stripped: no COBS, no CRC, no
 * host link, no debug UART.
 *
 * So: probe hears, this does not  ->  the bug is in radio.cpp.
 *     both hear                   ->  the bug is above it, in main.cpp/hostlink.
 */
#include <Arduino.h>
#include <SPI.h>
#include <LoRa.h>

#include "config.h"
#include "frame.h"
#include "radio.h"

#define BEACON_MS 3000UL
#define STATUS_MS 5000UL

#define R_OP_MODE   0x01
#define R_IRQ_FLAGS 0x12

#define IRQ_RX_TIMEOUT   0x80
#define IRQ_RX_DONE      0x40
#define IRQ_CRC_ERROR    0x20
#define IRQ_VALID_HEADER 0x10
#define IRQ_TX_DONE      0x08

static uint8_t rawRead(uint8_t addr)
{
    SPI1.beginTransaction(SPISettings(200000, MSBFIRST, SPI_MODE0));
    digitalWrite(LORA_IRQ_DUMB, LOW);
    SPI1.transfer(addr & 0x7F);
    const uint8_t v = SPI1.transfer(0x00);
    digitalWrite(LORA_IRQ_DUMB, HIGH);
    SPI1.endTransaction();
    return v;
}

static uint32_t boardId()
{
    return *(volatile uint32_t *)0x0080A00C ^ *(volatile uint32_t *)0x0080A040;
}

/* Stands in for main.cpp's TxQueue: one slot, so the peek/trySend/pop sequence
 * and its early return are reproduced without dragging in the host link. */
static uint8_t g_pending[64];
static uint8_t g_pending_len = 0;

static uint32_t g_id;
static uint32_t g_next_beacon, g_next_status;
static uint32_t g_sent, g_heard, g_send_retries;
static uint8_t g_irq_sticky;

void setup()
{
    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_BUILTIN, LOW);
    Serial.begin(115200);

    const uint32_t deadline = millis() + 5000;
    while (!Serial && millis() < deadline) {
    }

    g_id = boardId();
    Serial.println();
    Serial.print("radio-layer probe, board id ");
    Serial.println(g_id, HEX);

    /* The modem firmware's own init, unmodified. */
    if (!Radio::begin()) {
        Serial.println("FATAL: Radio::begin() returned false.");
        while (true) {
            digitalWrite(LED_BUILTIN, HIGH);
            delay(100);
            digitalWrite(LED_BUILTIN, LOW);
            delay(100);
        }
    }
    Serial.println("Radio::begin() ok -- driving src/radio.cpp, output on USB");

    g_next_beacon = millis();
    g_next_status = millis() + STATUS_MS;
}

void loop()
{
    g_irq_sticky |= rawRead(R_IRQ_FLAGS);

    if ((int32_t)(millis() - g_next_beacon) >= 0) {
        g_next_beacon = millis() + BEACON_MS;
        g_pending_len = (uint8_t)snprintf((char *)g_pending, sizeof(g_pending),
                                          "radio %08lX #%lu", (unsigned long)g_id,
                                          (unsigned long)(g_sent + 1));
    }

    /* main.cpp's serviceRadio(), including the early return: a queued frame
     * blocks receiving until the radio accepts it. */
    if (g_pending_len > 0) {
        if (Radio::trySend(g_pending, g_pending_len)) {
            g_sent++;
            Serial.print("SENT   \"");
            Serial.print((const char *)g_pending);
            Serial.println("\"  (trySend accepted)");
            g_pending_len = 0;
        } else {
            g_send_retries++;
        }
        return;
    }

    uint8_t buf[FRAME_MAX_PAYLOAD];
    int16_t rssi = 0;
    int8_t snr_q = 0;
    const int n = Radio::tryReceive(buf, sizeof(buf), &rssi, &snr_q);
    if (n >= 0) {
        g_heard++;
        buf[n < (int)sizeof(buf) ? n : (int)sizeof(buf) - 1] = '\0';
        Serial.print("HEARD  \"");
        Serial.print((const char *)buf);
        Serial.print("\"  rssi=");
        Serial.print(rssi);
        Serial.print(" dBm snr=");
        Serial.print(snr_q / 4.0f, 2);
        Serial.print(" dB  (heard=");
        Serial.print(g_heard);
        Serial.println(")");
        digitalWrite(LED_BUILTIN, HIGH);
        delay(50);
        digitalWrite(LED_BUILTIN, LOW);
    }

    if ((int32_t)(millis() - g_next_status) >= 0) {
        g_next_status = millis() + STATUS_MS;
        Serial.print("idle   op_mode=0x");
        Serial.print(rawRead(R_OP_MODE), HEX);
        Serial.print("  sent=");
        Serial.print(g_sent);
        Serial.print(" heard=");
        Serial.print(g_heard);
        Serial.print(" tx_retries=");
        Serial.print(g_send_retries);
        Serial.print(" air_oversize=");
        Serial.print(Radio::oversizeDropped());
        Serial.print("  seen:");
        if (g_irq_sticky & IRQ_VALID_HEADER) Serial.print(" ValidHeader");
        if (g_irq_sticky & IRQ_RX_DONE)      Serial.print(" RxDone");
        if (g_irq_sticky & IRQ_CRC_ERROR)    Serial.print(" CrcError");
        if (g_irq_sticky & IRQ_RX_TIMEOUT)   Serial.print(" RxTimeout");
        if (g_irq_sticky & IRQ_TX_DONE)      Serial.print(" TxDone");
        if (g_irq_sticky == 0)               Serial.print(" (nothing)");
        Serial.println();
        g_irq_sticky = 0;
    }
}
