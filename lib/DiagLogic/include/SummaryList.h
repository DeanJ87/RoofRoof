#pragma once

// The last MAX_SUMMARIES move summaries, newest first (spec 017 FR-011, FR-015;
// data-model.md). Kept on the main loop; the firmware guards it with its own
// mutex. `revision` changes on every change, for the kept file's write decision.

#include <cstdint>

#include "DiagTypes.h"

namespace SQM
{
    namespace Diag
    {
        constexpr uint8_t MAX_SUMMARIES = 32;

        class SummaryList
        {
        public:
            // A finished move, first in the list; the oldest past MAX_SUMMARIES goes.
            void add(const Summary &summary);
            // Summaries read from the kept file (newest first), replacing the list.
            void load(const Summary *summaries, uint8_t count);
            void clear();

            uint8_t size() const { return count; }
            // 0 is the newest.
            const Summary &at(uint8_t index) const { return items[index]; }
            // The highest id in the list (0 when empty).
            uint32_t highestId() const;
            uint32_t revision() const { return changes; }

        private:
            Summary items[MAX_SUMMARIES];
            uint8_t count = 0;
            uint32_t changes = 0;
        };
    } // namespace Diag
} // namespace SQM
