#include "HeapTrend.h"

#include <algorithm>

namespace SQM
{
    namespace HeapSafe
    {
        void HeapTrend::sample(uint32_t nowMs, uint32_t largestBlock, uint32_t freeBytes)
        {
            if (count == 0)
            {
                newest = 0;
                count = 1;
                minuteStartMs = nowMs - nowMs % MINUTE_MS;
                minutes[0] = Minute{nowMs / MINUTE_MS, largestBlock, freeBytes};
                return;
            }
            // Wrap-safe (millis() wraps after 49 days): time since the current
            // minute began; a reading from before it is ignored.
            const uint32_t since = nowMs - minuteStartMs;
            if (since >= 0x80000000u)
                return;
            const uint32_t passed = since / MINUTE_MS;
            if (passed == 0)
            {
                Minute &m = minutes[newest];
                m.lowestLargest = std::min(m.lowestLargest, largestBlock);
                m.lowestFree = std::min(m.lowestFree, freeBytes);
                return;
            }
            // Minutes with no reading at all (a long pass) are skipped, not invented.
            const uint32_t index = minutes[newest].index + passed;
            minuteStartMs += passed * MINUTE_MS;
            newest = (newest + 1) % MINUTES;
            count = std::min(count + 1, MINUTES);
            minutes[newest] = Minute{index, largestBlock, freeBytes};
        }

        const HeapTrend::Minute &HeapTrend::at(size_t newestFirst) const
        {
            const size_t i = std::min(newestFirst, count == 0 ? 0 : count - 1);
            return minutes[(newest + MINUTES - i) % MINUTES];
        }

        uint32_t HeapTrend::lowestLargest(size_t span) const
        {
            if (count == 0)
                return 0;
            span = std::max<size_t>(span, 1);
            const uint32_t current = minutes[newest].index;
            uint32_t lowest = UINT32_MAX;
            for (size_t i = 0; i < count; ++i)
            {
                const Minute &m = at(i);
                if (current - m.index >= span)
                    break;
                lowest = std::min(lowest, m.lowestLargest);
            }
            return lowest;
        }

        uint32_t HeapTrend::lowestFree(size_t span) const
        {
            if (count == 0)
                return 0;
            span = std::max<size_t>(span, 1);
            const uint32_t current = minutes[newest].index;
            uint32_t lowest = UINT32_MAX;
            for (size_t i = 0; i < count; ++i)
            {
                const Minute &m = at(i);
                if (current - m.index >= span)
                    break;
                lowest = std::min(lowest, m.lowestFree);
            }
            return lowest;
        }

        HeapTrend &trend()
        {
            static HeapTrend instance;
            return instance;
        }
    } // namespace HeapSafe
} // namespace SQM
