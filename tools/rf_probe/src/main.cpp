/* RF probe for the MKR WAN 1310.
 *
 * Flash to BOTH boards. Each beacons every 3 s and listens the rest of the
 * time, printing to USB serial -- no USB-TTL adapter, no framing, nothing
 * shared with the modem firmware.
 *
 * It answers three questions the modem firmware cannot:
 *
 *   1. Is the radio really configured the way the code thinks? It dumps the
 *      SX1276's own registers at boot, read back over SPI, rather than echoing
 *      the constants that were written.
 *   2. Does a transmission actually complete? endPacket() here is blocking, so
 *      it spins on TxDone instead of returning the moment TX mode is set. The
 *      modem firmware's async endPacket(true) proves only that the radio was
 *      told to transmit.
 *   3. Is anything arriving? It snapshots RegIrqFlags before every parsePacket()
 *      and keeps a sticky OR, so ValidHeader / RxDone / PayloadCrcError are
 *      visible even for packets the library silently discards.
 *
 * The two on-air settings that differ from the known-good IoT4Ag code are build
 * flags, so they can be bisected one at a time:
 *
 *   -D PROBE_CRC=0          receive corrupt packets instead of dropping them
 *   -D PROBE_SYNC_WORD=0x12 the library default, as the IoT4Ag code used
 */
#include <Arduino.h>
#include <SPI.h>
#include <LoRa.h>

#define FREQ_HZ   915000000L
#define SF        7
#define BW_HZ     125000L
#define CR        5
#define PREAMBLE  8
#define TX_DBM    17
#define BEACON_MS 3000UL
#define STATUS_MS 5000UL

#ifndef PROBE_SYNC_WORD
#define PROBE_SYNC_WORD 0x2B
#endif
#ifndef PROBE_CRC
#define PROBE_CRC 1
#endif

/* SX1276 registers, read directly so the report is what the chip holds rather
 * than what the code believes it wrote. Same bus the library uses: SPI1 at
 * 200 kHz, chip select on LORA_IRQ_DUMB. */
#define R_OP_MODE      0x01
#define R_FRF_MSB      0x06
#define R_PA_CONFIG    0x09
#define R_LNA          0x0C
#define R_IRQ_FLAGS    0x12
#define R_MODEM_CFG_1  0x1D
#define R_MODEM_CFG_2  0x1E
#define R_SYMB_TIMEOUT 0x1F
#define R_PREAMBLE_MSB 0x20
#define R_MODEM_CFG_3  0x26
#define R_SYNC_WORD    0x39
#define R_VERSION      0x42

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

static uint32_t g_id;
static uint32_t g_next_beacon;
static uint32_t g_next_status;
static uint32_t g_sent, g_heard;
static uint8_t g_irq_sticky;

static void printReg(const char *name, uint8_t addr)
{
    Serial.print("  ");
    Serial.print(name);
    Serial.print(" = 0x");
    const uint8_t v = rawRead(addr);
    if (v < 0x10) {
        Serial.print("0");
    }
    Serial.println(v, HEX);
}

static void dumpRegisters()
{
    Serial.println("registers, read back off the chip:");
    printReg("version     (0x42)", R_VERSION);
    printReg("op_mode     (0x01)", R_OP_MODE);
    printReg("pa_config   (0x09)", R_PA_CONFIG);
    printReg("lna         (0x0c)", R_LNA);
    printReg("modem_cfg_1 (0x1d)", R_MODEM_CFG_1);
    printReg("modem_cfg_2 (0x1e)", R_MODEM_CFG_2);
    printReg("modem_cfg_3 (0x26)", R_MODEM_CFG_3);
    printReg("symb_timeout(0x1f)", R_SYMB_TIMEOUT);
    printReg("preamble_msb(0x20)", R_PREAMBLE_MSB);
    printReg("sync_word   (0x39)", R_SYNC_WORD);

    /* Carrier frequency, decoded, because a wrong Frf is the one setting that
     * looks fine in every log line and still guarantees silence. */
    const uint32_t frf = ((uint32_t)rawRead(R_FRF_MSB) << 16) |
                         ((uint32_t)rawRead(R_FRF_MSB + 1) << 8) |
                         (uint32_t)rawRead(R_FRF_MSB + 2);
    const double mhz = (double)frf * 32.0 / 524288.0;
    Serial.print("  carrier           = ");
    Serial.print(mhz, 3);
    Serial.println(" MHz");

    const uint8_t cfg1 = rawRead(R_MODEM_CFG_1);
    const uint8_t cfg2 = rawRead(R_MODEM_CFG_2);
    Serial.print("  decoded           = SF");
    Serial.print(cfg2 >> 4);
    Serial.print(", bw_idx ");
    Serial.print(cfg1 >> 4);
    Serial.print(" (7=125k), CR4-");
    Serial.print(((cfg1 >> 1) & 0x07) + 4);
    Serial.print(", ");
    Serial.print((cfg1 & 0x01) ? "implicit hdr" : "explicit hdr");
    Serial.print(", crc ");
    Serial.println((cfg2 & 0x04) ? "on" : "off");
}

void setup()
{
    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_BUILTIN, LOW);
    Serial.begin(115200);

    /* Bounded, so the probe still runs off a battery with nothing attached. */
    const uint32_t deadline = millis() + 5000;
    while (!Serial && millis() < deadline) {
    }

    g_id = boardId();
    Serial.println();
    Serial.print("rf probe, board id ");
    Serial.println(g_id, HEX);

    if (!LoRa.begin(FREQ_HZ)) {
        Serial.println("FATAL: LoRa.begin() failed -- the SX1276 did not answer on SPI.");
        while (true) {
            digitalWrite(LED_BUILTIN, HIGH);
            delay(100);
            digitalWrite(LED_BUILTIN, LOW);
            delay(100);
        }
    }

    LoRa.setSpreadingFactor(SF);
    LoRa.setSignalBandwidth(BW_HZ);
    LoRa.setCodingRate4(CR);
    LoRa.setPreambleLength(PREAMBLE);
    LoRa.setSyncWord(PROBE_SYNC_WORD);
    LoRa.setTxPower(TX_DBM, PA_OUTPUT_PA_BOOST_PIN);
#if PROBE_CRC
    LoRa.enableCrc();
#else
    LoRa.disableCrc();
#endif

    dumpRegisters();
    Serial.println("beaconing every 3 s, listening in between");

    g_next_beacon = millis();
    g_next_status = millis() + STATUS_MS;
}

void loop()
{
    /* Sampled before parsePacket(), which clears these. Without it a packet
     * that arrives and fails its CRC is indistinguishable from silence -- and
     * telling those two apart is the whole point of the exercise. */
    g_irq_sticky |= rawRead(R_IRQ_FLAGS);

    const int size = LoRa.parsePacket();
    if (size > 0) {
        char buf[64];
        int n = 0;
        while (LoRa.available() && n < (int)sizeof(buf) - 1) {
            buf[n++] = (char)LoRa.read();
        }
        buf[n] = '\0';
        g_heard++;
        Serial.print("HEARD  \"");
        Serial.print(buf);
        Serial.print("\"  rssi=");
        Serial.print(LoRa.packetRssi());
        Serial.print(" dBm snr=");
        Serial.print(LoRa.packetSnr(), 2);
        Serial.print(" dB  (heard=");
        Serial.print(g_heard);
        Serial.println(")");
        digitalWrite(LED_BUILTIN, HIGH);
        delay(50);
        digitalWrite(LED_BUILTIN, LOW);
    }

    if ((int32_t)(millis() - g_next_beacon) >= 0) {
        g_next_beacon = millis() + BEACON_MS;
        char msg[32];
        snprintf(msg, sizeof(msg), "probe %08lX #%lu",
                 (unsigned long)g_id, (unsigned long)++g_sent);
        LoRa.beginPacket();
        LoRa.write((const uint8_t *)msg, strlen(msg));
        /* Blocking: it does not return until TxDone. If this line ever stops
         * printing, the transmission is not completing -- which the modem
         * firmware's endPacket(true) could never have told you. */
        const int ok = LoRa.endPacket();
        Serial.print("SENT   \"");
        Serial.print(msg);
        Serial.println(ok == 1 ? "\"  ok" : "\"  FAILED");
    }

    if ((int32_t)(millis() - g_next_status) >= 0) {
        g_next_status = millis() + STATUS_MS;
        Serial.print("idle   rssi=");
        Serial.print(LoRa.rssi());
        Serial.print(" dBm  op_mode=0x");
        Serial.print(rawRead(R_OP_MODE), HEX);
        Serial.print("  sent=");
        Serial.print(g_sent);
        Serial.print(" heard=");
        Serial.print(g_heard);
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
