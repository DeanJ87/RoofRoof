#include "CloseSequence.h"

#include <algorithm>

// What outlives a restart (spec 005, spec 010): the sequence's saved record
// and putting it back on the first step.

namespace SQM
{
    namespace CloseSeq
    {
        namespace
        {
            // Time from `sinceMs` to `nowMs`; 0 if the clock went backwards.
            uint64_t elapsed(uint64_t sinceMs, uint64_t nowMs)
            {
                return nowMs > sinceMs ? nowMs - sinceMs : 0;
            }
        } // namespace

        Sequence::Saved Sequence::saved() const
        {
            Saved out;
            // The park request spacing is kept in any phase, while it matters.
            out.requested = requested && elapsed(lastRequestMs, lastNowMs) < policy.parkRequestGapMs;
            out.requestAgeMs = out.requested ? elapsed(lastRequestMs, lastNowMs) : 0;
            out.daylight = daylightOnly;
            // Asked to close but not yet answered: nothing has moved, so a
            // restart now carries on from the step before (spec 010).
            const bool unanswered = current == Phase::Closing && closeUnanswered;
            const Phase phase = unanswered ? beforeClose : current;
            const uint64_t sinceMs = unanswered ? beforeCloseSinceMs : phaseSinceMs;
            const uint64_t wallStartMs = unanswered ? beforeCloseWallStartMs : phaseWallStartMs;
            const bool standDown = phase == Phase::HeldByPerson || phase == Phase::Halted;
            const bool waitingNow = phase == Phase::WaitPark || phase == Phase::RetryWait;
            // A close under way: a restart ends it part-way (a brown-out at the
            // motor's start, say), which restore() turns into a failed close.
            if (phase == Phase::Closing)
            {
                out.phase = Phase::Closing;
                return out;
            }
            if (!standDown && !waitingNow)
                return out;
            out.phase = phase;
            out.event = lastCritical.has ? lastCritical.event : Event::None;
            out.cause = lastCritical.has ? lastCritical.cause : Cause::None;
            if (waitingNow)
            {
                out.unsafeMs = elapsed(sinceMs, lastNowMs);
                out.wallMs = elapsed(wallStartMs, lastNowMs);
                out.limitMs = attemptLimitMs;
            }
            else
                out.unsafeMs = daylightCount(); // spec 013: the weather's count against a daylight stand-down
            return out;
        }

        void Sequence::restore(const Saved &saved)
        {
            // Read back from memory that outlived a restart: only sane values.
            const bool standDown = saved.phase == Phase::HeldByPerson || saved.phase == Phase::Halted;
            const bool waitingThen = saved.phase == Phase::WaitPark || saved.phase == Phase::RetryWait;
            const bool idleThen = saved.phase == Phase::Idle; // only the request spacing
            const bool closingThen = saved.phase == Phase::Closing;
            const bool eventOk = isCritical(saved.event) || (!standDown && saved.event == Event::None);
            if ((!standDown && !waitingThen && !idleThen && !closingThen) || !eventOk ||
                static_cast<size_t>(saved.cause) > static_cast<size_t>(Cause::RoofBetweenEnds))
                return;
            restoring = saved;
            if (idleThen)
                restoring.event = Event::None;
            restorePending = true;
            current = saved.phase;
        }

        // On the first step, once the time is known.
        void Sequence::applyRestore()
        {
            restorePending = false;
            current = restoring.phase;
            // Counted back from now, with the restart's own time added; a time
            // base too young to hold it is clamped (the caller offsets it).
            const uint64_t extra = restoring.phase == Phase::WaitPark || restoring.phase == Phase::RetryWait ? RESTART_ALLOWANCE_MS : 0;
            const uint64_t unsafeMs = restoring.unsafeMs + extra;
            const uint64_t wallMs = restoring.wallMs + extra;
            phaseSinceMs = lastNowMs - (unsafeMs < lastNowMs ? unsafeMs : lastNowMs);
            phaseWallStartMs = lastNowMs - (wallMs < lastNowMs ? wallMs : lastNowMs);
            // Never longer than the longest limit the settings allow.
            const uint64_t longestMs = 1800000;
            attemptLimitMs = restoring.limitMs != 0 && restoring.limitMs <= longestMs ? restoring.limitMs : parkTimeoutMs;
            requested = restoring.requested;
            daylightOnly = restoring.daylight && current != Phase::Idle;
            lastRequestMs = lastNowMs - (restoring.requestAgeMs < lastNowMs ? restoring.requestAgeMs : lastNowMs);
            lastCritical = LastCritical{};
            lastCritical.has = isCritical(restoring.event);
            lastCritical.event = restoring.event;
            lastCritical.cause = restoring.cause;
            lastCritical.limitMs = restoring.event == Event::ParkNotConfirmed ? attemptLimitMs : 0;
            remindNow = current == Phase::HeldByPerson || current == Phase::Halted;
            // Spec 013: a daylight stand-down keeps the weather's count across the restart.
            heldUnsafeMs = daylightOnly && remindNow ? std::min<uint64_t>(restoring.unsafeMs, WEATHER_REARM_CONFIRM_MS) : 0;
            if (current == Phase::Closing)
            {
                // A restart during a close: the close failed part-way. Back off
                // and alert, so a restart loop at every close start isn't silent.
                current = Phase::RetryWait;
                phaseSinceMs = lastNowMs;
                phaseWallStartMs = lastNowMs;
                closeFailedOnce = true;
                closeFailedAtMs = lastNowMs;
                closeFailedInDaylight = daylightOnly;
                lastCritical.has = true;
                lastCritical.event = Event::CloseFailed;
                lastCritical.cause = Cause::Interrupted;
                remindNow = true;
            }
        }
    } // namespace CloseSeq
} // namespace SQM
