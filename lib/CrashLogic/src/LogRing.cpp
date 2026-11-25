#include "LogRing.h"

#include <algorithm>
#include <cstring>

#include "CrashTypes.h"

namespace SQM
{
    namespace Crash
    {
        uint32_t check(const void *bytes, size_t length)
        {
            const uint8_t *p = static_cast<const uint8_t *>(bytes);
            uint32_t hash = 0x811C9DC5u;
            for (size_t i = 0; i < length; ++i)
            {
                hash ^= p[i];
                hash *= 0x01000193u;
            }
            return hash;
        }

        void copyText(char *out, size_t size, const char *text)
        {
            if (size == 0)
                return;
            size_t i = 0;
            if (text != nullptr)
                for (; i + 1 < size && text[i] != '\0'; ++i)
                    out[i] = text[i];
            out[i] = '\0';
        }

        namespace
        {
            constexpr char ESC = '\033';

            void sealHeader(LogRing &ring)
            {
                ring.magic = LogRing::MAGIC;
                ring.check = check(&ring, offsetof(LogRing, check));
            }

            bool headerIntact(const LogRing &ring)
            {
                return ring.magic == LogRing::MAGIC && ring.check == check(&ring, offsetof(LogRing, check)) && ring.head < RING_LINES &&
                       ring.count <= RING_LINES;
            }

            bool lineIntact(const LogLine &line)
            {
                return line.length < LINE_CHARS && line.text[line.length] == '\0' && line.check == check(&line, offsetof(LogLine, check));
            }

            // The slot of the i-th oldest line.
            size_t slotOf(const LogRing &ring, size_t i)
            {
                return (ring.head + RING_LINES - ring.count + i) % RING_LINES;
            }

            // Skips an escape sequence starting at text[i] (ESC [ ... final letter).
            size_t skipEscape(const char *text, size_t i)
            {
                ++i;
                if (text[i] != '[')
                    return text[i] == '\0' ? i : i + 1;
                ++i;
                while (text[i] != '\0' && !((text[i] >= 'A' && text[i] <= 'Z') || (text[i] >= 'a' && text[i] <= 'z')))
                    ++i;
                return text[i] == '\0' ? i : i + 1;
            }
        } // namespace

        size_t cleanText(char *out, size_t size, const char *text)
        {
            if (size == 0)
                return 0;
            size_t n = 0;
            for (size_t i = 0; text != nullptr && text[i] != '\0' && n + 1 < size;)
            {
                const unsigned char c = static_cast<unsigned char>(text[i]);
                if (c == ESC)
                {
                    i = skipEscape(text, i);
                    continue;
                }
                if (c >= 0x20 && c != 0x7F)
                    out[n++] = static_cast<char>(c);
                ++i;
            }
            while (n > 0 && out[n - 1] == ' ')
                --n;
            out[n] = '\0';
            return n;
        }

        void start(LogRing &ring, bool keep)
        {
            if (keep && headerIntact(ring))
            {
                ring.boot = static_cast<uint16_t>(ring.boot + 1 == 0 ? 1 : ring.boot + 1);
                sealHeader(ring);
                return;
            }
            std::memset(&ring, 0, sizeof(ring));
            ring.boot = 1;
            sealHeader(ring);
        }

        void push(LogRing &ring, uint32_t ms, Level level, const char *text)
        {
            LogLine &line = ring.lines[ring.head];
            line.ms = ms;
            line.boot = ring.boot;
            line.level = static_cast<uint8_t>(level);
            std::memset(line.text, 0, sizeof(line.text));
            line.length = static_cast<uint8_t>(cleanText(line.text, sizeof(line.text), text));
            line.check = check(&line, offsetof(LogLine, check));
            ring.head = static_cast<uint16_t>((ring.head + 1) % RING_LINES);
            if (ring.count < RING_LINES)
                ++ring.count;
            sealHeader(ring);
        }

        size_t copy(const LogRing &ring, LogLine *out, size_t max)
        {
            if (!headerIntact(ring))
                return 0;
            size_t n = 0;
            if (out == ring.lines)
            {
                // In place (a copy of the ring read into its own lines, spec 031):
                // rotate the oldest line to the front first, or a wrapped ring's
                // later lines would read slots already overwritten.
                std::rotate(out, out + slotOf(ring, 0), out + RING_LINES);
                for (size_t i = 0; i < ring.count && n < max; ++i)
                    if (lineIntact(out[i]))
                        out[n++] = out[i];
                return n;
            }
            for (size_t i = 0; i < ring.count && n < max; ++i)
            {
                const LogLine &line = ring.lines[slotOf(ring, i)];
                if (lineIntact(line))
                    out[n++] = line;
            }
            return n;
        }

        const char *levelName(uint8_t level)
        {
            static const char *const NAMES[] = {"debug", "info", "warn", "error", "fatal"};
            return level < sizeof(NAMES) / sizeof(NAMES[0]) ? NAMES[level] : "info";
        }
    } // namespace Crash
} // namespace SQM
