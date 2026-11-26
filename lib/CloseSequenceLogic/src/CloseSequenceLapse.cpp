#include "CloseSequence.h"

// Spec 044: a person's stop lapsing (autoClose.personStopLapse; HoldLapse.h),
// kept apart from spec 005's transition table in CloseSequence.cpp.

namespace SQM
{
    namespace CloseSeq
    {
        // Spec 044: the weather autoClose.personStopLapse counts (the weather's own
        // view, daylight left out: a stop of a daylight-only close never lapses).
        void Sequence::trackLapse(const Inputs &given, bool personStopped)
        {
            const bool unsafe = !given.weatherSafe;
            const bool rain = given.raining && unsafe;
            HoldLapse::Step step;
            step.nowMs = given.nowMs;
            step.held = current == Phase::HeldByPerson;
            step.newStop = personStopped;
            step.counts = policy.stopLapse == StopLapse::Always ? unsafe : policy.stopLapse == StopLapse::Rain && rain;
            step.lapseMs = policy.stopLapse == StopLapse::Never ? 0 : policy.stopHoldMs;
            lapseDue = holdLapse.update(step);
            lapseInRain = rain;
        }

        bool Sequence::standDownLapses() const
        {
            return current == Phase::HeldByPerson && holdLapse.leftMs() != HoldLapse::NOT_LAPSING;
        }
    } // namespace CloseSeq
} // namespace SQM
