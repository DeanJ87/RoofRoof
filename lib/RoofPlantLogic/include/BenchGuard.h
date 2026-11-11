#pragma once

// Bench mode's check that it isn't on a roof (spec 011 FR-023, research R3).
// A bare board has no 12 V supply; the roof always has. A real supply reading
// of LIMIT_VOLTS or more, or no reading, lasting CONFIRM_MS without a break
// trips the guard, and it stays tripped until the board restarts: the bench
// firmware then latches a fault and simulates nothing. Pure: the firmware
// hands it the real supply input's reading every step.

#include <cstdint>

namespace SQM
{
    namespace RoofPlant
    {
        class BenchSupplyGuard
        {
        public:
            static constexpr float LIMIT_VOLTS = 6.0f;
            static constexpr uint32_t CONFIRM_MS = 1000;

            // One reading of the real supply input (NaN: no reading); returns tripped().
            bool update(float volts, uint32_t nowMs);
            bool tripped() const { return isTripped; }
            // The reading that tripped it (NaN for no reading); 0 before.
            float measured() const { return trippedVolts; }

        private:
            bool seen = false; // a powered reading is running...
            uint32_t sinceMs = 0;
            bool isTripped = false;
            float trippedVolts = 0.0f;
        };

        // A reading that counts as a powered roof: NaN, or LIMIT_VOLTS or more.
        bool supplyPresent(float volts);
    } // namespace RoofPlant
} // namespace SQM
