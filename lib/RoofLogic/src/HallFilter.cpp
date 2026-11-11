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
