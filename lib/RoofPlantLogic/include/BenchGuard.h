#pragma once

// Bench mode's check that it isn't on a roof (spec 011 FR-023, research R3).
// A bare board has no 12 V supply; the roof always has. A real supply reading
// of LIMIT_VOLTS or more, or no reading, lasting CONFIRM_MS without a break
// trips the guard, and it stays tripped until the board restarts: the bench
// firmware then latches a fault and simulates nothing. Pure: the firmware
// hands it the real supply input's reading every step.

#include <cstdint>

#include "DriverLogic.h"
#include "RoofPlant.h"
#include "RoofSettings.h"
#include "RoofTypes.h"

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

        // Where the bench roof starts after a restart: half-way, so it reads
        // unknown (the Dome: an error) until a person moves it, and a bench
        // board never starts by reporting a closed roof.
        float benchStartMm(const Roof::Settings &settings);

        // The longest step the simulated roof takes at once: a late step is
        // the motion logic's to fault on, not a jump across the travel.
        constexpr uint32_t MAX_PLANT_STEP_MS = 100;

        // One bench control step's inputs: what the ADC read (its supply is
        // the real supply input), the time since the last step, what the
        // motor driver would apply (MotorDriver::logicalOutputs) and whether
        // current sense is on.
        struct BenchStepIn
        {
            Roof::Readings real;
            uint32_t gapMs = 0;
            Driver::Outputs logical;
            bool currentSense = false;
        };

        struct BenchStepOut
        {
            Roof::Readings readings; // the simulated roof's, for the motion logic
            bool latch = false;      // the guard is tripped: latch the benchSupply fault now
            float measured = 0.0f;   // its measured value (BenchSupplyGuard::measured)
        };

        // The bench build's step (spec 011, contracts/bench-mode.md): the guard
        // sees the real supply first; the roof moves under the logical driver
        // for the step's time (at most MAX_PLANT_STEP_MS); its Hall, supply and
        // current sense (NaN with current sense off) replace the ADC's.
        // `latch` stays true for every step after a trip, so a cleared fault
        // comes straight back.
        BenchStepOut benchStep(BenchSupplyGuard &guard, Plant &plant, const BenchStepIn &in);
    } // namespace RoofPlant
} // namespace SQM
