#pragma once

#include <Arduino.h>

#include "config.h"

/* The debug port, which is emphatically not the framed port (R6).
 *
 * Returns a sink that swallows everything when debug is compiled out, or when
 * the build has been configured with both roles pointing at the same object --
 * that mistake is silent otherwise, and it corrupts the link rather than the
 * logs, so it is checked once at startup and the debug side is the one that
 * loses.
 */
namespace Debug {
void begin();
Stream &out();
} // namespace Debug
