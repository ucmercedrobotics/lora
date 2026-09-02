#include "debug.h"

namespace {

/* Everything written here goes nowhere, at the cost of one vtable. Lets the
 * call sites print unconditionally instead of guarding every line. */
class NullStream : public Stream {
public:
    int available() { return 0; }
    int read() { return -1; }
    int peek() { return -1; }
    size_t write(uint8_t) { return 1; }
    size_t write(const uint8_t *, size_t size) { return size; }
};

NullStream g_null;

#ifndef LORA_NO_DEBUG
bool g_enabled = false;
#endif

} // namespace

void Debug::begin()
{
#ifndef LORA_NO_DEBUG
    /* Same object for both roles means every log line would be injected into
     * the frame stream. Drop the logs, keep the link. */
    if ((void *)&LORA_DEBUG_PORT == (void *)&LORA_HOST_PORT) {
        return;
    }
    LORA_DEBUG_PORT.begin(LORA_DEBUG_BAUD);
    g_enabled = true;
#endif
}

Stream &Debug::out()
{
#ifndef LORA_NO_DEBUG
    if (g_enabled) {
        return LORA_DEBUG_PORT;
    }
#endif
    return g_null;
}
