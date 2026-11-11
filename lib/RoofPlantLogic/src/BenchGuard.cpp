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
    } // namespace RoofPlant
} // namespace SQM
