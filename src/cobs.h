#pragma once

#include <stddef.h>
#include <stdint.h>

/* Consistent Overhead Byte Stuffing, standard formulation.
 *
 * The encoded form contains no 0x00 bytes, which is what makes 0x00 an
 * unambiguous frame delimiter and the stream self-resynchronising: a receiver
 * that joins mid-frame, or that loses one, recovers at the next 0x00 with
 * nothing to guess about. Overhead is one byte per 254 bytes of input.
 *
 * Free of Arduino and of the radio, so it builds and can be exercised anywhere.
 */

enum CobsStatus {
    COBS_OK = 0,
    COBS_ERR_ZERO_CODE, /* a 0x00 inside the frame; encoded output never has one */
    COBS_ERR_OVERRUN,   /* code byte points past the end of the frame */
    COBS_ERR_CAPACITY   /* decoded output would not fit the caller's buffer */
};

/* Upper bound on the encoded size of `len` bytes, delimiter excluded. */
size_t cobsMaxEncodedLen(size_t len);

/* Returns bytes written to `out`, or 0 if `out_cap` is too small. */
size_t cobsEncode(const uint8_t *in, size_t len, uint8_t *out, size_t out_cap);

/* Decodes one frame (delimiter already stripped). On COBS_OK, *out_len holds
 * the decoded byte count. */
CobsStatus cobsDecode(const uint8_t *in, size_t len, uint8_t *out, size_t out_cap,
                      size_t *out_len);
