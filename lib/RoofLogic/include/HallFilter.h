#pragma once

// The Hall reading turned into a level (research R5): thresholds inclusive, a
// hysteresis band for leaving a magnet, and a change confirmed only after
// HALL_CONFIRM_READINGS equal readings in a row. Rail readings (a broken sensor)
// are counted separately: HALL_RAIL_LATCH_READINGS of them among the last
// HALL_RAIL_WINDOW_READINGS confirm a rail, consecutive or not, and they never
// count toward (or interrupt) a magnet's confirmation.

#include <cstdint>

#include "RoofTypes.h"

namespace SQM
{
    namespace Roof
    {
        // Thresholds the filter classifies against (raw ADC counts).
        struct HallThresholds
        {
            uint16_t highCounts = 3000;
            uint16_t lowCounts = 1100;
        };

        // The confirmed Hall level, one reading at a time.
        class HallFilter
        {
        public:
            // Classifies `counts` and returns the confirmed level.
            HallLevel update(uint16_t counts, const HallThresholds &thresholds);

            HallLevel level() const { return confirmed; }

            // The level `counts` would be on its own, given the confirmed level (for
            // the hysteresis).
            static HallLevel classify(uint16_t counts, const HallThresholds &thresholds, HallLevel current);

        private:
            HallLevel confirmed = HallLevel::Unsettled;
            HallLevel candidate = HallLevel::Unsettled;
            uint8_t candidateCount = 0;
            uint8_t railHistory = 0; // one bit per recent reading, 1 = rail
        };
    } // namespace Roof
} // namespace SQM
