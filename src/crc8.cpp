#include "crc8.h"

/* Bitwise rather than table-driven: 256 bytes of flash is a real cost on the
 * smaller targets this has to stay portable to, and the frames are short. */
uint8_t crc8(const uint8_t *data, size_t len)
{
    uint8_t crc = 0x00;
    while (len--) {
        crc ^= *data++;
        for (uint8_t bit = 0; bit < 8; bit++) {
            crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x07) : (uint8_t)(crc << 1);
        }
    }
    return crc;
}
