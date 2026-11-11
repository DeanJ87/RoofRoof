#include "CloseSequence.h"

// The transitions (specs/005-close-on-unsafe/data-model.md). Each phase's
// function checks its conditions in the table's order.

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

        bool Sequence::Run::lasted(bool holds, uint64_t nowMs, uint64_t ms)
        {
            if (!holds)
            {
                on = false;
                return false;
            }
            if (!on)
            {
                on = true;
                sinceMs = nowMs;
            }
            return elapsed(sinceMs, nowMs) >= ms;
        }

        Decision Sequence::step(const Inputs &in)
        {
            lastNowMs = in.nowMs;
            // A change since the last step. Only the running phases act on it,
            // so stops while idle (and the count at start-up) are history.
            const bool personStopped = in.personStops != seenStops;
            seenStops = in.personStops;
            // Tracked every step, so each count runs without a break whatever the phase.
            parkConfirmed = parkHeld.lasted(in.parked, in.nowMs, PARK_SETTLE_MS);
            unknownConfirmed = unknown.lasted(in.roof == Roof::State::Unknown, in.nowMs, UNKNOWN_CONFIRM_MS);
            safeLong = safe.lasted(in.weatherSafe, in.nowMs, REARM_SAFE_MS);

            switch (current)
            {
            case Phase::Idle:
                return withAlert(idle(in));
            case Phase::WaitPark:
            case Phase::RetryWait:
                return withAlert(waiting(in, personStopped));
            case Phase::Closing:
                return withAlert(closing(in, personStopped));
            case Phase::HeldByPerson:
            case Phase::CannotMove:
                break;
            }
            return withAlert(stoodDown(in));
        }

        Decision Sequence::idle(const Inputs &in)
        {
            if (in.weatherSafe || in.roof == Roof::State::Closed)
                return Decision{};
            if (in.roof == Roof::State::Error)
                return enter(Phase::CannotMove, Event::CannotClose, Cause::FaultLatched);
            if (in.roof == Roof::State::Unknown)
            {
                // Just after a restart the Hall reading hasn't settled yet.
                if (!unknownConfirmed)
                    return Decision{};
                return enter(Phase::CannotMove, Event::CannotClose, Cause::PositionUnknown);
            }
            if (in.roof == Roof::State::Closing || in.closePending)
                return enter(Phase::Closing, Event::Started); // a close already running: watch it
            return attempt(in);
        }

        Decision Sequence::attempt(const Inputs &in)
        {
            Decision out = enter(Phase::WaitPark, Event::Started);
            attemptLimitMs = parkTimeoutMs; // a settings change applies from the next attempt
            out.action = requestIfDue(in.nowMs);
            return out;
        }

        // What ends or pauses any running phase, in the table's order.
        bool Sequence::interrupted(const Inputs &in, bool personStopped, Decision &out)
        {
            if (in.roof == Roof::State::Closed)
                out = enter(Phase::Idle, Event::Closed);
            else if (personStopped)
                out = enter(Phase::HeldByPerson, Event::StoppedByPerson);
            else if (in.roof == Roof::State::Error && current == Phase::Closing)
                out = enter(Phase::CannotMove, Event::CloseFailed, Cause::Fault);
            else if (in.roof == Roof::State::Error)
                out = enter(Phase::CannotMove, Event::CannotClose, Cause::FaultLatched);
            else if (in.roof == Roof::State::Unknown)
                out = unknownConfirmed ? enter(Phase::CannotMove, Event::CannotClose, Cause::PositionUnknown) : Decision{};
            else
                return false;
            return true;
        }

        Decision Sequence::waiting(const Inputs &in, bool personStopped)
        {
            Decision out;
            if (interrupted(in, personStopped, out))
                return out;
            if (in.weatherSafe)
                return enter(Phase::Idle, Event::EndedSafe);
            if (in.roof == Roof::State::Closing || in.closePending)
                return enter(Phase::Closing); // a person's close: watch it
            if (parkConfirmed)
                return closeNow();
            const uint64_t since = elapsed(phaseSinceMs, in.nowMs);
            if (current == Phase::RetryWait)
                return since >= RETRY_AFTER_MS ? attempt(in) : Decision{};
            if (!in.ninaConfigured)
                return failAttempt(Event::ParkNotConfirmed, Cause::NinaNotConfigured, Action::None);
            if (since >= attemptLimitMs)
            {
                out = failAttempt(Event::ParkNotConfirmed, Cause::ParkTimeout, Action::None);
                out.limitMs = attemptLimitMs;
                return out;
            }
            out.action = requestIfDue(in.nowMs);
            return out;
        }

        Decision Sequence::closing(const Inputs &in, bool personStopped)
        {
            Decision out;
            if (interrupted(in, personStopped, out))
                return out;
            if (in.roof == Roof::State::Closing)
            {
                if (!closingSeen)
                {
                    // From the command (or the watch's start), so a late step
                    // can't hide the time already spent; after a waiting close,
                    // from when it started.
                    closingSeen = true;
                    closingSinceMs = sawPending ? in.nowMs : phaseSinceMs;
                }
                const uint64_t limitMs = in.moveTimeoutMs + CLOSE_GUARD_SLACK_MS;
                if (elapsed(closingSinceMs, in.nowMs) < limitMs)
                    return out;
                out = failAttempt(Event::CloseFailed, Cause::TooLong, Action::Stop);
                out.limitMs = limitMs;
                return out;
            }
            if (in.closePending)
            {
                sawPending = true;
                if (elapsed(phaseSinceMs, in.nowMs) < CLOSE_START_MS)
                    return out;
                out = failAttempt(Event::CloseFailed, Cause::TooLong, Action::Stop);
                out.limitMs = CLOSE_START_MS;
                return out;
            }
            // Neither closing nor waiting: the close ended part-way (a person's
            // open after the weather cleared is not a failure).
            if (in.weatherSafe)
                return enter(Phase::Idle, Event::EndedSafe);
            return failAttempt(Event::CloseFailed, Cause::Interrupted, Action::None);
        }

        Decision Sequence::stoodDown(const Inputs &in)
        {
            if (in.roof == Roof::State::Closed)
                return enter(Phase::Idle, Event::Closed);
            if (safeLong)
                return enter(Phase::Idle, Event::EndedSafe);
            // A person made the position known or cleared the fault: resume
            // without a new park request until the back-off ends.
            if (current == Phase::CannotMove && in.roof != Roof::State::Unknown && in.roof != Roof::State::Error)
                return enter(Phase::RetryWait);
            return Decision{};
        }

        Decision Sequence::closeAnswered(CloseAnswer answer)
        {
            if (current != Phase::Closing)
                return Decision{};
            if (answer == CloseAnswer::Started)
                return Decision{};
            if (answer == CloseAnswer::ParkRefused)
            {
                // Back to where the close came from, within the same time
                // limit; the park must hold for the full time again.
                current = beforeClose;
                phaseSinceMs = beforeCloseSinceMs;
                parkHeld.on = false;
                parkConfirmed = false;
                return Decision{};
            }
            return withAlert(failAttempt(Event::CloseFailed, Cause::Refused, Action::None));
        }

        Decision Sequence::closeNow()
        {
            beforeClose = current;
            beforeCloseSinceMs = phaseSinceMs;
            Decision out = enter(Phase::Closing);
            out.action = Action::Close;
            return out;
        }

        Decision Sequence::failAttempt(Event event, Cause cause, Action action)
        {
            Decision out = enter(Phase::RetryWait, event, cause);
            out.action = action;
            return out;
        }

        Decision Sequence::enter(Phase next, Event event, Cause cause)
        {
            current = next;
            phaseSinceMs = lastNowMs;
            closingSeen = false;
            sawPending = false;
            if (next == Phase::Idle)
                alerted = 0; // the episode is over
            Decision out;
            out.event = event;
            out.cause = cause;
            return out;
        }

        Action Sequence::requestIfDue(uint64_t nowMs)
        {
            if (requested && elapsed(lastRequestMs, nowMs) < PARK_REQUEST_GAP_MS)
                return Action::None;
            requested = true;
            lastRequestMs = nowMs;
            return Action::RequestPark;
        }

        Decision Sequence::withAlert(Decision decision)
        {
            if (!isCritical(decision.event))
                return decision;
            const uint8_t bit = static_cast<uint8_t>(1u << static_cast<uint8_t>(decision.event));
            decision.alert = (alerted & bit) == 0;
            alerted = static_cast<uint8_t>(alerted | bit);
            return decision;
        }

        void applySafety(Inputs &in, Safety::Verdict verdict, const Safety::ParkResult &park)
        {
            in.weatherSafe = verdict == Safety::Verdict::Safe;
            in.parked = park.parked;
            in.ninaConfigured = (park.reasons & Safety::ParkNinaNotConfigured) == 0;
        }

        void applyRoof(Inputs &in, const Roof::Status &status, bool closePending)
        {
            in.roof = status.state;
            in.moveTimeoutMs = status.moveTimeoutMs;
            in.closePending = closePending;
        }

        bool isCritical(Event event)
        {
            return event == Event::ParkNotConfirmed || event == Event::CloseFailed || event == Event::CannotClose ||
                   event == Event::StoppedByPerson;
        }
    } // namespace CloseSeq
} // namespace SQM
