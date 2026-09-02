#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* Two bounded buffers, both header-only and both free of Arduino.
 *
 * Bounded is the point. The host talks over a UART two orders of magnitude
 * faster than the radio, so without a ceiling a busy minute is an out-of-memory
 * reset. Overflow here is a stated policy -- drop the oldest, count it -- not
 * an accident, and neither of these ever blocks the caller.
 */

/* Frames waiting for the channel. Fixed slots, so no allocation and no
 * fragmentation across days of uptime. */
template <size_t DEPTH, size_t MAX_LEN>
class FrameQueue {
public:
    FrameQueue() : _head(0), _count(0), _dropped(0) {}

    bool empty() const { return _count == 0; }
    size_t count() const { return _count; }
    uint32_t dropped() const { return _dropped; }

    /* Always accepts, by evicting the head when full. A coordination frame
     * held behind a full queue is stale by the time it would go out, so the
     * oldest is the cheapest thing to lose. */
    void push(const uint8_t *data, uint8_t len)
    {
        if (len > MAX_LEN) {
            return;
        }
        if (_count == DEPTH) {
            _head = (_head + 1) % DEPTH;
            _count--;
            _dropped++;
        }
        const size_t slot = (_head + _count) % DEPTH;
        memcpy(_data[slot], data, len);
        _len[slot] = len;
        _count++;
    }

    const uint8_t *peek(uint8_t *len) const
    {
        if (_count == 0) {
            return NULL;
        }
        *len = _len[_head];
        return _data[_head];
    }

    void pop()
    {
        if (_count > 0) {
            _head = (_head + 1) % DEPTH;
            _count--;
        }
    }

private:
    uint8_t _data[DEPTH][MAX_LEN];
    uint8_t _len[DEPTH];
    size_t _head;
    size_t _count;
    uint32_t _dropped;
};

/* Encoded bytes waiting for the host port to accept them.
 *
 * Whole frames go in or none of one does: half a frame in the buffer is a
 * guaranteed parse error at the far end, where a cleanly dropped frame is just
 * a frame the host never hears about.
 */
template <size_t CAPACITY>
class ByteRing {
public:
    ByteRing() : _head(0), _count(0), _dropped(0) {}

    bool empty() const { return _count == 0; }
    uint32_t dropped() const { return _dropped; }

    bool pushAll(const uint8_t *data, size_t len)
    {
        if (len > CAPACITY - _count) {
            _dropped++;
            return false;
        }
        for (size_t i = 0; i < len; i++) {
            _buf[(_head + _count + i) % CAPACITY] = data[i];
        }
        _count += len;
        return true;
    }

    uint8_t peek() const { return _buf[_head]; }

    void pop()
    {
        _head = (_head + 1) % CAPACITY;
        _count--;
    }

private:
    uint8_t _buf[CAPACITY];
    size_t _head;
    size_t _count;
    uint32_t _dropped;
};
