#include "FixedRing.h"

namespace SQM
{
    namespace HeapSafe
    {
        void copyCut(char *out, size_t size, const char *text)
        {
            copyCut(out, size, text, text == nullptr ? 0 : std::strlen(text));
        }

        void copyCut(char *out, size_t size, const char *text, size_t length)
        {
            if (out == nullptr || size == 0)
                return;
            if (text == nullptr)
                length = 0;
            if (length < size)
            {
                std::memcpy(out, text, length);
                out[length] = '\0';
                return;
            }
            // Cut: room for "..." when the buffer can show anything besides it.
            constexpr size_t MARK = 3;
            const bool marked = size > MARK + 1;
            size_t keep = size - 1 - (marked ? MARK : 0);
            // Never split a character: back off continuation bytes (10xxxxxx).
            while (keep > 0 && (static_cast<uint8_t>(text[keep]) & 0xC0) == 0x80)
                --keep;
            std::memcpy(out, text, keep);
            if (marked)
            {
                std::memcpy(out + keep, "...", MARK);
                keep += MARK;
            }
            out[keep] = '\0';
        }
    } // namespace HeapSafe
} // namespace SQM
