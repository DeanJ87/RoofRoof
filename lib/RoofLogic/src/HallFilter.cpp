#include "HallFilter.h"

#include <algorithm>

#include "RoofSettings.h"

namespace SQM
{
    namespace Roof
    {
        HallLevel HallFilter::classify(uint16_t counts, const HallThresholds &thresholds, HallLevel current)
        {
            if (counts <= HALL_RAIL_LOW_COUNTS || counts >= HALL_RAIL_HIGH_COUNTS)
                return HallLevel::Rail;
            // Leaving a confirmed magnet needs the reading past its threshold by the
            // hysteresis band; entering one needs only the threshold.
            const int high = current == HallLevel::High ? thresholds.highCounts - HALL_HYSTERESIS_COUNTS : thresholds.highCounts;
            const int low = current == HallLevel::Low ? thresholds.lowCounts + HALL_HYSTERESIS_COUNTS : thresholds.lowCounts;
            if (counts >= high)
                return HallLevel::High;
            if (counts <= low)
                return HallLevel::Low;
            return HallLevel::Between;
        }

        // Whether the rail readings among the recent ones (this one included) confirm a rail.
        bool HallFilter::railConfirmed(bool rail)
        {
            constexpr uint8_t WINDOW_MASK = static_cast<uint8_t>((1u << HALL_RAIL_WINDOW_READINGS) - 1u);
            railHistory = static_cast<uint8_t>(((railHistory << 1) | (rail ? 1u : 0u)) & WINDOW_MASK);
            int rails = 0;
            for (uint8_t bits = railHistory; bits != 0; bits = static_cast<uint8_t>(bits & (bits - 1)))
                ++rails;
            return rails >= HALL_RAIL_LATCH_READINGS;
        }

        uint16_t HallFilter::windowMedian() const
        {
            uint16_t sorted[HALL_MEDIAN_READINGS];
            std::copy(window, window + filled, sorted);
            std::sort(sorted, sorted + filled);
            return sorted[filled / 2];
        }

        HallLevel HallFilter::update(uint16_t counts, const HallThresholds &thresholds, uint32_t nowMs)
        {
            const bool rail = classify(counts, thresholds, confirmed) == HallLevel::Rail;
            if (railConfirmed(rail))
            {
                confirmed = HallLevel::Rail;
                candidateCount = 0;
                return confirmed;
            }
            if (rail)
                return confirmed; // too few to confirm; doesn't break a magnet's run either
            window[next] = counts;
            next = static_cast<uint8_t>((next + 1) % HALL_MEDIAN_READINGS);
            if (filled < HALL_MEDIAN_READINGS)
                ++filled;
            median = windowMedian();
            const HallLevel seen = classify(median, thresholds, confirmed);
            if (seen == confirmed)
            {
                candidateCount = 0;
                return confirmed;
            }
            if (seen != candidate || candidateCount == 0)
            {
                candidate = seen;
                candidateCount = 0;
                candidateSinceMs = nowMs;
            }
            if (candidateCount < UINT8_MAX)
                ++candidateCount;
            // Signed: a step clock that went backwards reads as not yet persisted.
            const int32_t heldMs = static_cast<int32_t>(nowMs - candidateSinceMs);
            if (candidateCount >= HALL_CONFIRM_READINGS && heldMs >= static_cast<int32_t>(HALL_PERSIST_MS))
            {
                confirmed = seen;
                candidateCount = 0;
            }
            return confirmed;
        }
    } // namespace Roof
} // namespace SQM
