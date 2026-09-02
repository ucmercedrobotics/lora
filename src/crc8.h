#pragma once

#include <stddef.h>
#include <stdint.h>

/* CRC-8/ATM: poly 0x07, init 0x00, no reflection, no final XOR.
 *
 * Check value for "123456789" is 0xF4, which is asserted at startup because a
 * wrong CRC is otherwise indistinguishable from a bad cable. Note this is NOT
 * the Dallas/Maxim CRC-8 that ships in many Arduino OneWire libraries (poly
 * 0x31, reflected) -- the two are not interchangeable.
 *
 * The LoRa PHY already CRCs the air link, so this one exists for the UART hop
 * between this board and the host, which nothing else protects. Its only job is
 * to turn a flipped bit into a dropped frame rather than a delivered bad one.
 */
uint8_t crc8(const uint8_t *data, size_t len);
