#pragma once

// Spec 045: how the heap has done over the last hour, a minute at a time.
//
// The largest free block decides whether a request, a frame or a save can be
// had, and it moves by the second: one reading of /api/status says little
// about whether the heap is being cut up over a session. The main loop feeds a
// reading about once a second; each minute keeps the lowest largest block and
// the lowest free heap seen in it. Fixed room, nothing allocated.

#include <cstddef>
#include <cstdint>

namespace SQM
{
    namespace HeapSafe
    {
        class HeapTrend
        {
        public:
            static constexpr size_t MINUTES = 60;
            static constexpr uint32_t MINUTE_MS = 60000;

            struct Minute
            {
                uint32_t index = 0;         // minutes since start (uptime / 60 s)
                uint32_t lowestLargest = 0; // bytes
                uint32_t lowestFree = 0;    // bytes
            };

            // A reading at `nowMs` (millis()); readings of a minute already
            // passed over are ignored.
            void sample(uint32_t nowMs, uint32_t largestBlock, uint32_t freeBytes);

            // The lowest largest block over the last `minutes` minutes,
            // the current one included; 0 before the first reading.
            uint32_t lowestLargest(size_t minutes) const;
            uint32_t lowestFree(size_t minutes) const;

            // Minutes held, newest first: at(0) is the current minute.
            size_t size() const { return count; }
            const Minute &at(size_t newestFirst) const;

        private:
            Minute minutes[MINUTES] = {};
            size_t newest = 0; // slot of the current minute
            size_t count = 0;
            uint32_t minuteStartMs = 0; // when the current minute began (millis())
        };

        // The firmware's trend (the main loop feeds it; any task reads it:
        // a reading may be a pass old).
        HeapTrend &trend();
    } // namespace HeapSafe
} // namespace SQM
