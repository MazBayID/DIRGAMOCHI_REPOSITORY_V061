#include "text_utils.h"

String sanitizeAsciiOled(const String &in)
{
    String out;
    out.reserve(in.length());

    size_t i = 0;
    size_t n = in.length();
    while (i < n)
    {
        uint8_t b = (uint8_t)in[i];

        if (b < 0x80)
        {
            // Plain ASCII. Keep printable characters; map stray control
            // characters (newlines snuck into a title, etc.) to a space
            // so they don't confuse the line-wrapping logic downstream.
            out += (b >= 0x20 && b < 0x7F) ? (char)b : ' ';
            i += 1;
            continue;
        }

        // UTF-8 lead byte: figure out how many continuation bytes this
        // codepoint uses from the high bits, so we can skip the *whole*
        // sequence and emit exactly one placeholder for it.
        size_t seqLen;
        if ((b & 0xE0) == 0xC0)
            seqLen = 2; // 110xxxxx
        else if ((b & 0xF0) == 0xE0)
            seqLen = 3; // 1110xxxx (covers the BMP: most emoji punctuation,
                        // smart quotes U+2019/201C/201D, ellipsis U+2026)
        else if ((b & 0xF8) == 0xF0)
            seqLen = 4; // 11110xxx (covers most emoji, which live above
                        // the BMP)
        else
        {
            // Invalid lead byte (or a stray continuation byte) - drop just
            // this one byte rather than mis-parsing everything after it.
            out += '?';
            i += 1;
            continue;
        }

        // Validate that the continuation bytes are actually there and look
        // like continuation bytes (10xxxxxx). If not, treat as malformed
        // and only skip the lead byte, so we resync on the next byte
        // instead of eating good ASCII that happened to follow.
        size_t j = 1;
        for (; j < seqLen && (i + j) < n; ++j)
        {
            uint8_t cb = (uint8_t)in[i + j];
            if ((cb & 0xC0) != 0x80)
                break;
        }

        if (j == seqLen)
        {
            out += '?';
            i += seqLen;
        }
        else
        {
            out += '?';
            i += 1;
        }
    }

    return out;
}
