#include "BenchGuard.h"

#include <cmath>

namespace SQM
{
    namespace RoofPlant
    {
        bool supplyPresent(float volts)
        {
            return !std::isfinite(volts) || volts >= BenchSupplyGuard::LIMIT_VOLTS;
        }

        bool BenchSupplyGuard::update(float volts, uint32_t nowMs)
        {
            if (isTripped)
                return true;
            if (!supplyPresent(volts))
            {
                seen = false;
                return false;
            }
            if (!seen)
            {
                seen = true;
                sinceMs = nowMs;
            }
            // Unsigned difference: correct across a millis() wrap.
            if (nowMs - sinceMs >= CONFIRM_MS)
            {
                isTripped = true;
                trippedVolts = volts;
            }
            return isTripped;
        }

        float benchStartMm(const Roof::Settings &settings)
        {
            return static_cast<float>(settings.travelMm) / 2.0f;
        }

        BenchStepOut benchStep(BenchSupplyGuard &guard, Plant &plant, const BenchStepIn &in)
        {
            BenchStepOut out;
            out.latch = guard.update(in.real.supplyVolts, in.real.nowMs);
            out.measured = guard.measured();
            const uint32_t elapsed = in.gapMs < MAX_PLANT_STEP_MS ? in.gapMs : MAX_PLANT_STEP_MS;
            plant.drive(outputOf(in.logical), elapsed);
            out.readings = in.real;
            out.readings.hallCounts = plant.hallCounts();
            out.readings.supplyVolts = plant.supplyNow();
            out.readings.motorAmps = in.currentSense ? plant.senseAmps() : std::nanf("");
            return out;
        }
    } // namespace RoofPlant
} // namespace SQM
