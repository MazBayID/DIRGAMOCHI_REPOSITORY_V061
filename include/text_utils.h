#ifndef TEXT_UTILS_H
#define TEXT_UTILS_H

#include <Arduino.h>

// Adafruit_GFX's built-in font only has glyphs for 7-bit ASCII (0x20-0x7E).
// Text coming from Chronos (WhatsApp notifications, track titles, city
// names, ...) is UTF-8 and very often contains multi-byte sequences -
// emoji, curly "smart quotes", ellipsis characters, accented letters, etc.
//
// Printing those bytes straight to Adafruit_GFX does two bad things at
// once:
//   1. Each raw byte is drawn as whatever undefined-glyph box the font
//      falls back to, so the message *looks* corrupted.
//   2. Anything that later truncates/wraps the string by byte count (as
//      chronos_ui.cpp's line wrapper does) can cut a multi-byte sequence
//      in half, which is what makes long messages look especially broken
//      compared to short ones.
//
// sanitizeAsciiOled() walks the UTF-8 byte stream and replaces each
// non-ASCII *codepoint* (however many bytes it takes) with a single '?'
// placeholder, so the string that reaches the wrapping/truncation logic
// is guaranteed to be one byte per on-screen character again. Call this
// on every String coming from ChronosESP32 before printing it.
String sanitizeAsciiOled(const String &in);

#endif // TEXT_UTILS_H
