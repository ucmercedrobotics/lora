#include "cobs.h"

size_t cobsMaxEncodedLen(size_t len)
{
    return len + len / 254 + 1;
}

size_t cobsEncode(const uint8_t *in, size_t len, uint8_t *out, size_t out_cap)
{
    if (out_cap < cobsMaxEncodedLen(len)) {
        return 0;
    }

    size_t code_index = 0; /* where this block's length byte will go */
    size_t out_index = 1;  /* first payload byte, past that reserved slot */
    uint8_t code = 1;

    for (size_t i = 0; i < len; i++) {
        if (in[i] == 0x00) {
            out[code_index] = code;
            code_index = out_index++;
            code = 1;
        } else {
            out[out_index++] = in[i];
            code++;
            /* A full 254-byte block. The 0xFF code says "no zero follows", so
             * the next block continues rather than restarting after a zero. */
            if (code == 0xFF) {
                out[code_index] = code;
                code_index = out_index++;
                code = 1;
            }
        }
    }
    out[code_index] = code;
    return out_index;
}

CobsStatus cobsDecode(const uint8_t *in, size_t len, uint8_t *out, size_t out_cap,
                      size_t *out_len)
{
    size_t i = 0;
    size_t o = 0;

    while (i < len) {
        const uint8_t code = in[i];
        if (code == 0x00) {
            return COBS_ERR_ZERO_CODE;
        }
        i++;

        const size_t block_end = i + code - 1;
        if (block_end > len) {
            return COBS_ERR_OVERRUN;
        }
        while (i < block_end) {
            if (o >= out_cap) {
                return COBS_ERR_CAPACITY;
            }
            out[o++] = in[i++];
        }

        /* A 0xFF block is a continuation, not a zero that was stuffed out, and
         * a block ending exactly at the frame end has no zero after it either. */
        if (code != 0xFF && i < len) {
            if (o >= out_cap) {
                return COBS_ERR_CAPACITY;
            }
            out[o++] = 0x00;
        }
    }

    *out_len = o;
    return COBS_OK;
}
