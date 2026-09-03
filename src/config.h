#pragma once

/* Every knob the modem has, in one place.
 *
 * Nothing here is specific to a payload, a fleet or a project: the modem moves
 * opaque bytes, so the only things it can be configured for are the radio, the
 * two serial ports and how much it is willing to buffer. Each default can be
 * overridden with -D in platformio.ini without touching a source file.
 */

/* ---------------------------------------------------------------- serial ---
 * R6: nothing but frames on the framed port. Debug text goes somewhere else,
 * because one stray println on the host link is a guaranteed CRC error at the
 * far end -- and a plausible-looking one, since ASCII decodes as valid COBS
 * often enough to waste an afternoon.
 */
#ifndef LORA_HOST_PORT
#define LORA_HOST_PORT Serial
#endif

#ifndef LORA_DEBUG_PORT
#define LORA_DEBUG_PORT Serial1
#endif

/* Both ends of the serial hop must agree; the contract fixes this at 115200,
 * 8N1, no flow control. It is ~2 orders of magnitude faster than the air rate,
 * so backpressure comes from the radio and never from the UART. */
#ifndef LORA_HOST_BAUD
#define LORA_HOST_BAUD 115200
#endif

#ifndef LORA_DEBUG_BAUD
#define LORA_DEBUG_BAUD 115200
#endif

/* Define LORA_NO_DEBUG to compile the debug port out of the build entirely. */

/* ----------------------------------------------------------------- radio ---
 * R4: every one of these is assigned at init, never inherited. LoRa.begin()
 * sets none of them, so a sketch that omits them runs on SX127x reset defaults
 * -- a configuration nobody chose, nobody wrote down, and which silently
 * decides the legal payload size.
 */
#ifndef LORA_FREQUENCY_HZ
#define LORA_FREQUENCY_HZ 915000000L /* US 915 MHz ISM. 868E6 in the EU. */
#endif

#ifndef LORA_SPREADING_FACTOR
#define LORA_SPREADING_FACTOR 7
#endif

#ifndef LORA_BANDWIDTH_HZ
#define LORA_BANDWIDTH_HZ 125000L
#endif

/* Denominator of the 4/N coding rate, so 5..8 for 4/5..4/8. */
#ifndef LORA_CODING_RATE
#define LORA_CODING_RATE 5
#endif

#ifndef LORA_PREAMBLE_SYMBOLS
#define LORA_PREAMBLE_SYMBOLS 8
#endif

/* R8: explicit and shared fleet-wide. Deliberately not the library's 0x12 --
 * on that value every LoRa device in earshot shares the channel. Both ends of
 * a link must be built with the same number. */
#ifndef LORA_SYNC_WORD
#define LORA_SYNC_WORD 0x2B
#endif

#ifndef LORA_TX_POWER_DBM
#define LORA_TX_POWER_DBM 17
#endif

/* PA_OUTPUT_PA_BOOST_PIN for modules wired to PA_BOOST (almost all of them,
 * including the Murata module on the MKR WAN); PA_OUTPUT_RFO_PIN otherwise. */
#ifndef LORA_PA_OUTPUT_PIN
#define LORA_PA_OUTPUT_PIN PA_OUTPUT_PA_BOOST_PIN
#endif

/* Pin overrides for boards whose radio is not on the arduino-LoRa defaults.
 * Left undefined the library's own per-board defaults apply, which are already
 * correct for the MKR WAN (SPI1, SS = LORA_IRQ_DUMB, DIO0 = LORA_IRQ).
 *   -D LORA_SS_PIN=10 -D LORA_RESET_PIN=9 -D LORA_DIO0_PIN=2
 */
#if defined(LORA_SS_PIN) || defined(LORA_RESET_PIN) || defined(LORA_DIO0_PIN)
#if !defined(LORA_SS_PIN) || !defined(LORA_RESET_PIN) || !defined(LORA_DIO0_PIN)
#error "define all three of LORA_SS_PIN, LORA_RESET_PIN, LORA_DIO0_PIN, or none"
#endif
#endif

/* --------------------------------------------------------------- payload ---
 * R2: the ceiling, and oversize is dropped rather than truncated.
 *
 * 200 is a policy choice, not a limit. It sits below every ceiling that
 * applies -- the 252 the frame format allows, the FIFO, and the FCC Part
 * 15.247 400 ms dwell budget -- and leaves 52 bytes of headroom. The binding
 * one is dwell, and it collapses as the spreading factor rises: 255 B at SF7,
 * 138 B at SF8, 66 B at SF9, 24 B at SF10, and nothing at all at SF11/SF12 on
 * a 125 kHz channel. Startup recomputes the number for whatever this build is
 * configured for and says so on the debug port.
 */
#ifndef LORA_MAX_PAYLOAD_BYTES
#define LORA_MAX_PAYLOAD_BYTES 200
#endif

/* ---------------------------------------------------------------- queues ---
 * The radio is slower than the host by orders of magnitude, so the host can
 * hand over frames faster than they can go out. These bound what that costs in
 * RAM. Overflow is drop-oldest and counted: a stale frame is worth less than a
 * fresh one, and either way the loop never blocks (R7).
 */
#ifndef LORA_TX_QUEUE_DEPTH
#define LORA_TX_QUEUE_DEPTH 6
#endif

/* Bytes of encoded frames held for the host port while it is not accepting
 * writes. One frame is at most 260 bytes on the wire. */
#ifndef LORA_HOST_TX_BUFFER_BYTES
#define LORA_HOST_TX_BUFFER_BYTES 1024
#endif

/* --------------------------------------------------------------- reports ---
 * Counter line on the debug port. 0 disables it.
 */
#ifndef LORA_STATS_PERIOD_MS
#define LORA_STATS_PERIOD_MS 30000UL
#endif

/* -------------------------------------------------------------------- led ---
 * LED_BUILTIN pulses on every host-to-air send and every air-to-host
 * receive, long enough to see, short enough not to blur two events
 * together.
 */
#ifndef LORA_LED_FLASH_MS
#define LORA_LED_FLASH_MS 30UL
#endif
