#pragma once

// Spec 016 (research R6): the statistics a calibration run keeps. Plain
// accumulators, no allocation: the run adds one reading per control step.

#include <cstdint>

namespace SQM
{
    namespace Calib
    {
        // Count, mean, spread and range of raw Hall counts.
        struct Stats
        {
            uint32_t count = 0;
            double sum = 0.0;
            double sumSq = 0.0;
            uint16_t min = UINT16_MAX;
            uint16_t max = 0;

            void add(uint16_t counts);
            void merge(const Stats &other);
            bool empty() const { return count == 0; }
            double mean() const;
            // The standard deviation (0 with fewer than two readings).
            double sigma() const;
        };

        // A leg's Hall readings in time buckets, so that the middle half of the
        // leg can be taken once its length is known. The bucket width doubles
        // (pairs merged) whenever the leg outgrows the buckets.
        class LegSamples
        {
        public:
            static constexpr uint8_t BUCKETS = 64;
            static constexpr uint32_t FIRST_WIDTH_MS = 250;

            void clear();
            void add(uint32_t drivenMs, uint16_t counts);
            // The readings of the buckets that lie wholly inside the middle half of
            // a leg `totalMs` long; for a leg too short for that, the bucket
            // holding its midpoint.
            Stats middle(uint32_t totalMs) const;
            uint32_t widthMs() const { return width; }

        private:
            void widen();

            Stats buckets[BUCKETS];
            uint32_t width = FIRST_WIDTH_MS;
        };

        // The current and the supply while a leg drives.
        struct Electrical
        {
            uint32_t count = 0; // current readings taken (with current sense, after the start-up surge)
            double ampsSum = 0.0;
            float ampsPeak = 0.0f;
            bool supplyRead = false;
            float supplyMin = 0.0f;

            void addSupply(float volts);
            void addAmps(float amps);
            float ampsMean() const;
        };
    } // namespace Calib
} // namespace SQM
