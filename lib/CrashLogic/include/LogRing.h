#pragma once

// The last log lines (spec 029 FR-005, FR-006), kept in RTC memory so the
// lines before a crash are still there after it. The firmware writes one line
// at a time under a short lock; nothing here allocates.

#include <cstddef>
#include <cstdint>

namespace SQM
{
    namespace Crash
    {
        constexpr size_t RING_LINES = 50;
        constexpr size_t LINE_CHARS = 72; // 71 characters and the terminator

        // The logger's levels (src/Logger.h's LogLevel numbers).
        enum class Level : uint8_t
        {
            Debug = 0,
            Info = 1,
            Warn = 2,
            Error = 3,
            Fatal = 4,
        };

        // One line: when, which boot, how loud, the text and its check.
        struct LogLine
        {
            uint32_t ms; // since that boot
            uint16_t boot;
            uint8_t level;
            uint8_t length;
            char text[LINE_CHARS];
            uint32_t check;
        };

        // The ring: the boot number, the next slot and how many lines it holds.
        struct LogRing
        {
            static constexpr uint32_t MAGIC = 0xC2A5C003u;
            uint32_t magic;
            uint16_t boot;
            uint16_t head;
            uint16_t count;
            uint16_t pad;
            uint32_t check; // over the header above
            LogLine lines[RING_LINES];
        };

        // At start: with `keep` and an intact header the lines stay and the
        // boot number goes up; otherwise the ring is emptied (boot 1).
        void start(LogRing &ring, bool keep);

        // Adds a line for this boot, cleaned and cut to 71 characters.
        void push(LogRing &ring, uint32_t ms, Level level, const char *text);

        // The intact lines, oldest first; at most `max`. Returns how many.
        size_t copy(const LogRing &ring, LogLine *out, size_t max);

        // `text` without colour codes (ESC [ ... letter), control characters
        // and trailing spaces, cut to fit `size`. Returns the length written.
        size_t cleanText(char *out, size_t size, const char *text);

        // "debug", "info", "warn", "error", "fatal".
        const char *levelName(uint8_t level);
    } // namespace Crash
} // namespace SQM
