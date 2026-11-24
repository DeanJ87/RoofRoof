#pragma once

// The Hall reading turned into a level (research R5): thresholds inclusive, a
// hysteresis band for leaving a magnet, a median over the last
// HALL_MEDIAN_READINGS control steps, and a change confirmed only once the
// median has classified the same way for HALL_PERSIST_MS (and at least
// HALL_CONFIRM_READINGS steps) without a break. Motor PWM noise that couples
// into the sensor line reads as short bursts; the median drops bursts of up to
// two steps, and the persistence anything shorter than HALL_PERSIST_MS. Rail
// readings (a broken sensor) are counted separately on the raw readings:
// HALL_RAIL_LATCH_READINGS of them among the last HALL_RAIL_WINDOW_READINGS
// confirm a rail, consecutive or not; they never enter the median, count
// toward or interrupt a magnet's confirmation.

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

        // The steps the median covers (odd).
        constexpr uint8_t HALL_MEDIAN_READINGS = 5;

        // The confirmed Hall level, one reading (control step) at a time.
        class HallFilter
        {
        public:
            // Takes the step's reading at `nowMs` (the step clock) and returns the confirmed level.
            HallLevel update(uint16_t counts, const HallThresholds &thresholds, uint32_t nowMs);

            HallLevel level() const { return confirmed; }
            // The median the level was last classified from (0 before any reading).
            uint16_t filtered() const { return median; }

            // The level `counts` would be on its own, given the confirmed level (for
            // the hysteresis).
            static HallLevel classify(uint16_t counts, const HallThresholds &thresholds, HallLevel current);

        private:
            bool railConfirmed(bool rail);
            uint16_t windowMedian() const;

            HallLevel confirmed = HallLevel::Unsettled;
            HallLevel candidate = HallLevel::Unsettled;
            uint8_t candidateCount = 0;
            uint32_t candidateSinceMs = 0;
            uint8_t railHistory = 0; // one bit per recent reading, 1 = rail
            uint16_t window[HALL_MEDIAN_READINGS] = {};
            uint8_t filled = 0;
            uint8_t next = 0;
            uint16_t median = 0;
        };
    } // namespace Roof
} // namespace SQM
