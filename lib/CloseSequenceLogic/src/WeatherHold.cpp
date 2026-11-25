#include "WeatherHold.h"

#include <algorithm>

namespace SQM
{
    namespace CloseSeq
    {
        void WeatherHold::reset()
        {
            countMs = 0;
            safeMs = 0;
            isFired = false;
        }

        void WeatherHold::startFired()
        {
            isFired = true;
            safeMs = 0;
        }

        uint64_t WeatherHold::leftMs() const
        {
            if (isFired || !counting || holdMs == 0)
                return NOT_HOLDING;
            return countMs >= holdMs ? 0 : holdMs - countMs;
        }

        bool WeatherHold::update(const Step &step)
        {
            // The time since the last step, capped, counts for this step's
            // reading; the first step (and a clock going backwards) counts nothing.
            const uint64_t dt = stepped && step.nowMs > lastMs ? std::min(step.nowMs - lastMs, HOLD_MAX_STEP_MS) : 0;
            stepped = true;
            lastMs = step.nowMs;
            holdMs = std::min(step.holdMs, UNSAFE_HOLD_MAX_MS);
            const bool unsafe = !step.weatherSafe || step.rain;
            counting = unsafe;
            // Spec 027: the overridden reasons never start a close behind the person's back.
            if (holdMs == 0 || (step.overrideActive && !unsafe))
            {
                reset();
                isFired = holdMs == 0 && unsafe; // the old behaviour: no hold at all
                return !unsafe;
            }
            if (unsafe)
            {
                safeMs = 0;
                countMs += dt;
                if (step.rain || countMs >= holdMs)
                    isFired = true;
            }
            else
            {
                safeMs += dt;
                if (safeMs >= holdMs)
                    reset();
            }
            return !isFired;
        }
    } // namespace CloseSeq
} // namespace SQM
