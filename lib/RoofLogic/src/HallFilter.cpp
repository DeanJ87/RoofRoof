#include "HallFilter.h"

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

        HallLevel HallFilter::update(uint16_t counts, const HallThresholds &thresholds)
        {
            const HallLevel seen = classify(counts, thresholds, confirmed);
            constexpr uint8_t WINDOW_MASK = static_cast<uint8_t>((1u << HALL_RAIL_WINDOW_READINGS) - 1u);
            railHistory = static_cast<uint8_t>(((railHistory << 1) | (seen == HallLevel::Rail ? 1u : 0u)) & WINDOW_MASK);
            int rails = 0;
            for (uint8_t bits = railHistory; bits != 0; bits = static_cast<uint8_t>(bits & (bits - 1)))
                ++rails;
            if (rails >= HALL_RAIL_LATCH_READINGS)
            {
                confirmed = HallLevel::Rail;
                candidateCount = 0;
                return confirmed;
            }
            if (seen == HallLevel::Rail)
                return confirmed; // too few to confirm; doesn't break a magnet's run either
            if (seen == confirmed)
            {
                candidateCount = 0;
                return confirmed;
            }
            if (seen != candidate)
            {
                candidate = seen;
                candidateCount = 0;
            }
            if (++candidateCount >= HALL_CONFIRM_READINGS)
            {
                confirmed = seen;
                candidateCount = 0;
            }
            return confirmed;
        }
    } // namespace Roof
} // namespace SQM
