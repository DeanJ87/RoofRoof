#include "CloseSequence.h"

#include "MountMove.h"

// Spec 043 (constitution 11.1.0, II): the telescope moves only with the roof at
// an end. A park is a slew, so the sequence asks for one only then; with the
// roof between its ends and the mount not parked it moves nothing (no park
// request, no close onto an unparked mount) and a person must check
// (CannotMove, CannotClose / RoofBetweenEnds, reminded). Also what ends any
// CannotMove (a fault cleared, the controller started, the roof at an end).

namespace SQM
{
    namespace CloseSeq
    {
        namespace
        {
            uint64_t elapsed(uint64_t sinceMs, uint64_t nowMs)
            {
                return nowMs > sinceMs ? nowMs - sinceMs : 0;
            }
        } // namespace

        // Spec 043: a slew (the park included) is safe only with the roof at an
        // end; a close is safe only onto a parked mount. Neither holds here.
        bool Sequence::betweenEndsUnparked(const Inputs &in) const
        {
            // Moving: where it stops is still to come (an open stops by itself
            // when unsafe; a close, or one waiting to start, is watched): judged
            // once it has stopped. Nothing is asked meanwhile.
            const bool moving = in.roof == Roof::State::Opening || in.roof == Roof::State::Closing || in.closePending;
            return !moving && !RoofIo::mountMayMove(in.roof) && !in.parked && !ninaLostNow;
        }

        Decision Sequence::betweenEnds()
        {
            return enter(Phase::CannotMove, Event::CannotClose, Cause::RoofBetweenEnds);
        }

        // CannotMove, after the stand-down's own exits (closed, the re-arm spell,
        // a person's stop).
        Decision Sequence::cannotMove(const Inputs &in)
        {
            const bool betweenCause = cannotCause == Cause::RoofBetweenEnds;
            // Nothing wants the roof closed: nothing to do (and nothing to remind
            // of) while the roof is between its ends; unsafe again, idle() judges afresh.
            if (betweenCause && in.weatherSafe)
                return enter(Phase::Idle, Event::EndedSafe);
            // A fault, or a controller that didn't start, still holds it.
            if (blocksAttempt(in))
                return Decision{};
            // Still between the ends with the mount not parked (a person cleared a
            // fault, say): say so, and stay.
            if (betweenEndsUnparked(in))
                return betweenCause ? Decision{} : betweenEnds();
            lastCritical.has = false; // "can't close by itself" no longer holds: nothing to remind of
            // A person made the position known or cleared the fault (spec 030: or a
            // blind close may run): resume without a new park request until the
            // back-off ends. Spec 043: at an end now, or the mount parked: nothing
            // failed here, so no new back-off (one a failed close started still runs).
            const bool backingOff = closeFailedOnce && elapsed(closeFailedAtMs, in.nowMs) < policy.retryAfterMs;
            if (betweenCause && !backingOff)
                return attempt(in);
            return enter(Phase::RetryWait);
        }
    } // namespace CloseSeq
} // namespace SQM
