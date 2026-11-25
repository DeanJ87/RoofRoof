#include "NinaLost.h"

#include "ParkRule.h"

namespace SQM
{
    namespace CloseSeq
    {
        bool ninaLost(uint32_t parkReasons)
        {
            constexpr uint32_t LOST =
                Safety::ParkNinaUnreachable | Safety::ParkNinaUnreadable | Safety::ParkNinaApiError | Safety::ParkNinaStale;
            return (parkReasons & LOST) != 0;
        }

        bool NinaLostWatch::update(const Step &step)
        {
            lastMs = step.nowMs;
            if (!ninaLost(step.parkReasons))
            {
                lost = false;
                return false;
            }
            if (!lost)
            {
                lost = true;
                sinceMs = step.nowMs;
            }
            return step.enabled && step.nowMs - sinceMs >= step.afterMs;
        }
    } // namespace CloseSeq
} // namespace SQM
