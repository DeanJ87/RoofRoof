#include "RoofReadings.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace SQM
{
    namespace RoofIo
    {
        uint16_t medianCounts(const uint16_t *samples, uint8_t count)
        {
            if (samples == nullptr || count == 0)
                return 0;
            const uint8_t used = std::min(count, MAX_SAMPLES);
            uint16_t sorted[MAX_SAMPLES];
            std::copy(samples, samples + used, sorted);
            std::sort(sorted, sorted + used);
            return sorted[(used - 1) / 2];
        }

        float averageMillivolts(const uint32_t *samples, uint8_t count)
        {
            if (samples == nullptr || count == 0)
                return std::numeric_limits<float>::quiet_NaN();
            const uint8_t used = std::min(count, MAX_SAMPLES);
            uint64_t sum = 0;
            for (uint8_t i = 0; i < used; ++i)
                sum += samples[i];
            return static_cast<float>(sum) / static_cast<float>(used);
        }

        float supplyVolts(float pinMillivolts)
        {
            return pinMillivolts / 1000.0f * SUPPLY_DIVIDER;
        }

        float motorAmps(float rightMillivolts, float leftMillivolts, float ampsPerVolt)
        {
            float larger = rightMillivolts;
            if (std::isnan(larger) || (!std::isnan(leftMillivolts) && leftMillivolts > larger))
                larger = leftMillivolts;
            return larger / 1000.0f * CURRENT_DIVIDER * ampsPerVolt;
        }
    } // namespace RoofIo
} // namespace SQM
