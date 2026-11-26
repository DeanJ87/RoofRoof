#include "HoldLapse.h"

#include <algorithm>

namespace SQM
{
    namespace CloseSeq
    {
        void HoldLapse::reset()
        {
            countMs = 0;
            breakMs = 0;
        }

        uint64_t HoldLapse::leftMs() const
        {
            if (!counting)
                return NOT_LAPSING;
            return countMs >= lapseMs ? 0 : lapseMs - countMs;
        }

        bool HoldLapse::update(const Step &step)
        {
            // The time since the last held step, capped, counts for this step's
            // reading; the first held step (and a clock going backwards) counts nothing.
            const uint64_t dt = wasHeld && step.nowMs > lastMs ? std::min(step.nowMs - lastMs, MAX_STEP_MS) : 0;
            lastMs = step.nowMs;
            lapseMs = step.lapseMs;
            const bool on = step.held && !step.newStop && step.lapseMs > 0;
            wasHeld = on;
            counting = on && step.counts;
            if (!on)
            {
                reset();
                return false;
            }
            if (step.counts)
            {
                breakMs = 0;
                countMs += dt;
                return countMs >= lapseMs;
            }
            breakMs += dt;
            if (breakMs >= lapseMs)
                reset();
            return false;
        }
    } // namespace CloseSeq
} // namespace SQM
