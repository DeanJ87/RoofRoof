#pragma once

// The watchdog's rule (spec 003 FR-007, research R3): the driver may run only
// while the control task keeps stepping. The firmware's timer asks this every
// WATCHDOG_CHECK_MS and drops EN when it says so.

#include <cstdint>

#include "RoofReadings.h"

namespace SQM
{
    namespace RoofIo
    {
        // Milliseconds since the last step, right across the 32-bit wrap. A
        // step stamped after `nowMs` (the other task stepped between the two
        // reads) is a gap of 0, never a huge one.
        inline uint32_t stepGapMs(uint32_t nowMs, uint32_t lastStepMs)
        {
            const int32_t gap = static_cast<int32_t>(nowMs - lastStepMs);
            return gap < 0 ? 0u : static_cast<uint32_t>(gap);
        }

        // The gap is over the limit (at the limit is still fine, as in spec 002's late-step check).
        inline bool watchdogExpired(uint32_t nowMs, uint32_t lastStepMs, uint32_t limitMs = WATCHDOG_LIMIT_MS)
        {
            return stepGapMs(nowMs, lastStepMs) > limitMs;
        }
    } // namespace RoofIo
} // namespace SQM
