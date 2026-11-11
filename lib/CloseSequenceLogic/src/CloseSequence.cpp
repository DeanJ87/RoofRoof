#include "CloseSequence.h"

#include "MotionGuards.h"

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
            static_assert(static_cast<size_t>(Event::StoppedByPerson) < 8, "alertedAtMs has a row for every event");
            static_assert(static_cast<size_t>(Cause::RoofNotStarted) < 10, "alertedAtMs has a column for every cause");
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

        Decision Sequence::step(const Inputs &given)
        {
            lastNowMs = given.nowMs;
            if (restorePending)
                applyRestore();
            // A change since the last step. Only the running phases act on it,
            // so stops while idle (and the count at start-up) are history.
            const bool personStopped = given.personStops != seenStops;
            seenStops = given.personStops;
            trackEpisode(given, personStopped);
            // Spec 013: daylight wants the roof closed as unsafe weather does.
            // From here on "weather safe" reads "nothing wants the roof
            // closed": with daylight false this is spec 005, step for step.
            Inputs in = given;
            in.weatherSafe = given.weatherSafe && !given.daylight;
            // Tracked every step, so each count runs without a break whatever the phase.
            parkConfirmed = parkHeld.lasted(in.parked, in.nowMs, PARK_SETTLE_MS);
            unknownConfirmed = unknown.lasted(in.roof == Roof::State::Unknown, in.nowMs, UNKNOWN_CONFIRM_MS);
            safeLong = safe.lasted(in.weatherSafe, in.nowMs, REARM_SAFE_MS);
            // A close (anyone's) reached the closed magnet: the trouble is over,
            // so a new failure on the next spell alerts at once. A reading that
            // flickers back to closed at rest doesn't count: no alert storm.
            if (lastRoof == Roof::State::Closing && in.roof == Roof::State::Closed)
            {
                for (auto &row : alertedEver)
                    for (bool &ever : row)
                        ever = false;
            }
            lastRoof = in.roof;

            Decision out = withAlert(remindIfDue(decide(in, personStopped)));
            out.daylight = daylightOnly;
            return out;
        }

        // Spec 013 (research R4). An episode is daylight-only from the idle
        // step that starts it with the weather safe until the weather first
        // stops being safe. At that moment a daylight-only stand-down (a
        // person's stop, a halted close) or back-off must not hold the
        // weather close back: the sequence re-arms and the weather attempt
        // starts on this same step, as if the daylight episode hadn't been.
        // A person's stop seen on this very step wins (it stands down below).
        // A person's stand-down waits for the weather to have been not safe
        // for WEATHER_REARM_CONFIRM_MS (a new stop restarts the wait); a halt
        // or a back-off re-arms at once. The critical alerts' throttle starts
        // afresh, so the weather close's failures are announced in their own
        // words even when the daylight close failed the same way just before.
        void Sequence::trackEpisode(const Inputs &given, bool personStopped)
        {
            const bool unsafeLong = weatherUnsafe.lasted(!given.weatherSafe, given.nowMs, WEATHER_REARM_CONFIRM_MS);
            if (current == Phase::Idle)
            {
                daylightOnly = given.daylight && given.weatherSafe;
                return;
            }
            if (given.weatherSafe || !daylightOnly)
                return;
            if (current == Phase::HeldByPerson && personStopped)
                weatherUnsafe.sinceMs = given.nowMs; // the wait starts again from this stop
            if (current == Phase::HeldByPerson && (!unsafeLong || personStopped))
                return;
            daylightOnly = false;
            for (auto &row : alertedEver)
                for (bool &ever : row)
                    ever = false;
            const bool heldBack = current == Phase::HeldByPerson || current == Phase::Halted || current == Phase::RetryWait;
            if (heldBack && !personStopped)
                enter(Phase::Idle);
        }

        Decision Sequence::decide(const Inputs &in, bool personStopped)
        {
            switch (current)
            {
            case Phase::Idle:
                return idle(in);
            case Phase::WaitPark:
            case Phase::RetryWait:
                return waiting(in, personStopped);
            case Phase::Closing:
                return closing(in, personStopped);
            case Phase::HeldByPerson:
            case Phase::CannotMove:
            case Phase::Halted:
                break;
            }
            return stoodDown(in, personStopped);
        }

        // While the roof stays open after a critical event, the event is
        // raised again every REMIND_MS: a lost or missed alert is not the last word.
        Decision Sequence::remindIfDue(Decision decision)
        {
            const bool open =
                current == Phase::RetryWait || current == Phase::HeldByPerson || current == Phase::CannotMove || current == Phase::Halted;
            if (decision.event != Event::None || !open || !lastCritical.has)
                return decision;
            // Spec 013: a person who stopped a daylight-only close meant it; no
            // wake-me reminder all day (the first alert went out).
            if (current == Phase::HeldByPerson && daylightOnly)
                return decision;
            if (!remindNow && elapsed(lastCritical.alertedAtMs, lastNowMs) < REMIND_MS)
                return decision;
            Decision again;
            again.action = decision.action;
            again.event = lastCritical.event;
            again.cause = lastCritical.cause;
            again.limitMs = lastCritical.limitMs;
            again.reminder = true;
            remindNow = false;
            return again;
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
                return enter(Phase::CannotMove, Event::CannotClose, in.roofStarted ? Cause::PositionUnknown : Cause::RoofNotStarted);
            }
            if (in.roof == Roof::State::Closing || in.closePending)
                return enter(Phase::Closing, Event::Started); // a close already running: watch it
            if (paused.has && elapsed(paused.endedAtMs, in.nowMs) < REARM_SAFE_MS)
                return resume(in);
            return attempt(in);
        }

        // Unsafe again after a short safe spell: carry on where the attempt
        // (or its back-off) stood, its clock counting the unsafe time only, so
        // a flapping verdict can't restart the park time limit for ever.
        Decision Sequence::resume(const Inputs &in)
        {
            enter(paused.phase);
            // Only unsafe time counts against the limit: the safe spell is left out.
            phaseSinceMs = paused.sinceMs + elapsed(paused.endedAtMs, in.nowMs);
            phaseWallStartMs = paused.wallStartMs;
            attemptLimitMs = paused.limitMs;
            lastCritical = paused.critical; // reminders carry on through a flapping spell
            paused.has = false;
            // Straight on with the paused phase (its wall-clock cap may have run out).
            Decision out = waiting(in, false);
            if (out.event == Event::None)
                out.event = Event::Started;
            return out;
        }

        Decision Sequence::attempt(const Inputs &in)
        {
            Decision out = enter(Phase::WaitPark, Event::Started);
            parkRefusals = 0;
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
                out = unknownConfirmed
                          ? enter(Phase::CannotMove, Event::CannotClose, in.roofStarted ? Cause::PositionUnknown : Cause::RoofNotStarted)
                          : Decision{};
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
            {
                const Pause keep{true, current, phaseSinceMs, phaseWallStartMs, attemptLimitMs, in.nowMs, lastCritical};
                out = enter(Phase::Idle, Event::EndedSafe);
                paused = keep;
                return out;
            }
            if (in.roof == Roof::State::Closing || in.closePending)
                return enter(Phase::Closing); // a person's close: watch it
            // After a refused or interrupted close, no close until the back-off
            // ends, even across a brief safe spell (a flapping verdict).
            if (parkConfirmed)
            {
                if (!closeFailedOnce || elapsed(closeFailedAtMs, in.nowMs) >= RETRY_AFTER_MS)
                    return closeNow();
                // Parked, held back only by the back-off: not a park failure,
                // so no "mount not parked" (its alert would name the wrong input).
                return out;
            }
            const uint64_t since = elapsed(phaseSinceMs, in.nowMs);
            // Both waits count unsafe time, with a wall-clock cap for one
            // stretched by many short safe spells.
            const uint64_t wall = elapsed(phaseWallStartMs, in.nowMs);
            if (current == Phase::RetryWait)
                return since >= RETRY_AFTER_MS || wall >= RETRY_AFTER_MS + REARM_SAFE_MS ? attempt(in) : Decision{};
            if (!in.ninaConfigured)
                return failAttempt(Event::ParkNotConfirmed, Cause::NinaNotConfigured, Action::None);
            const uint64_t capMs = 2 * attemptLimitMs + REARM_SAFE_MS;
            if (since >= attemptLimitMs || wall >= capMs)
            {
                out = failAttempt(Event::ParkNotConfirmed, Cause::ParkTimeout, Action::None);
                out.limitMs = since >= attemptLimitMs ? attemptLimitMs : capMs; // the limit that ran out
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
                return halt(limitMs);
            }
            if (in.closePending)
            {
                sawPending = true;
                if (elapsed(phaseSinceMs, in.nowMs) < CLOSE_START_MS)
                    return out;
                return halt(CLOSE_START_MS);
            }
            // Neither closing nor waiting: the close ended part-way (a person's
            // open after the weather cleared is not a failure).
            if (in.weatherSafe)
                return enter(Phase::Idle, Event::EndedSafe);
            return failAttempt(Event::CloseFailed, Cause::Interrupted, Action::None);
        }

        // A close that outlasted its guard: stopped where it is, and nothing
        // more until a person closes the roof or it has been safe for a while
        // (constitution III: a timed-out step stops the sequence where it is).
        Decision Sequence::halt(uint64_t limitMs)
        {
            Decision out = enter(Phase::Halted, Event::CloseFailed, Cause::TooLong);
            out.action = Action::Stop;
            out.limitMs = limitMs;
            return out;
        }

        Decision Sequence::stoodDown(const Inputs &in, bool personStopped)
        {
            if (in.roof == Roof::State::Closed)
                return enter(Phase::Idle, Event::Closed);
            // Safe for REARM_SAFE_MS since standing down, not since before it.
            if (safeLong && elapsed(phaseSinceMs, in.nowMs) >= REARM_SAFE_MS)
                return enter(Phase::Idle, Event::EndedSafe);
            // A stop while the roof can't move (before a person clears a fault,
            // say) still stands the sequence down once it can.
            if (personStopped && current == Phase::CannotMove)
                return enter(Phase::HeldByPerson, Event::StoppedByPerson);
            // A person made the position known or cleared the fault: resume
            // without a new park request until the back-off ends.
            if (current == Phase::CannotMove && in.roof != Roof::State::Unknown && in.roof != Roof::State::Error)
            {
                lastCritical.has = false; // "can't close by itself" no longer holds: nothing to remind of
                return enter(Phase::RetryWait);
            }
            return Decision{};
        }

        Decision Sequence::closeAnswered(CloseAnswer answer)
        {
            if (current != Phase::Closing)
                return Decision{};
            closeUnanswered = false;
            if (answer == CloseAnswer::Started)
                return Decision{};
            // The gate keeps disagreeing with the park result: a refusal like any other.
            if (answer == CloseAnswer::ParkRefused && ++parkRefusals >= MAX_PARK_REFUSALS)
                answer = CloseAnswer::Refused;
            if (answer == CloseAnswer::ParkRefused || answer == CloseAnswer::Withdrawn)
            {
                // Not run: back to where the close came from, within the same
                // time limit; the park must hold for the full time again. A
                // withdrawn close's stop is seen on the next step.
                current = beforeClose;
                phaseSinceMs = beforeCloseSinceMs;
                phaseWallStartMs = beforeCloseWallStartMs;
                parkHeld.on = false;
                parkConfirmed = false;
                return Decision{};
            }
            Decision out = withAlert(failAttempt(Event::CloseFailed, Cause::Refused, Action::None));
            out.daylight = daylightOnly;
            return out;
        }

        Sequence::Saved Sequence::saved() const
        {
            Saved out;
            // The park request spacing is kept in any phase, while it matters.
            out.requested = requested && elapsed(lastRequestMs, lastNowMs) < PARK_REQUEST_GAP_MS;
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
                static_cast<size_t>(saved.cause) > static_cast<size_t>(Cause::RoofNotStarted))
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
            if (current == Phase::Closing)
            {
                // A restart during a close: the close failed part-way. Back off
                // and alert, so a restart loop at every close start isn't silent.
                current = Phase::RetryWait;
                phaseSinceMs = lastNowMs;
                phaseWallStartMs = lastNowMs;
                closeFailedOnce = true;
                closeFailedAtMs = lastNowMs;
                lastCritical.has = true;
                lastCritical.event = Event::CloseFailed;
                lastCritical.cause = Cause::Interrupted;
                remindNow = true;
            }
        }

        RoofIo::SequenceState platformState(Phase phase)
        {
            switch (phase)
            {
            case Phase::WaitPark:
            case Phase::Closing:
                return RoofIo::SequenceState::Working;
            case Phase::HeldByPerson:
                return RoofIo::SequenceState::HeldByPerson;
            case Phase::Idle:
                return RoofIo::SequenceState::Idle;
            default:
                return RoofIo::SequenceState::Waiting;
            }
        }

        CloseAnswer answerFor(bool accepted, const std::string &reason)
        {
            if (accepted)
                return CloseAnswer::Started;
            if (reason == RoofIo::gateRefusalName(RoofIo::GateRefusal::MountNotParked))
                return CloseAnswer::ParkRefused;
            // A person's stop since the decision, or a restart about to happen
            // (spec 010): not run, and not a failed close; the sequence goes back
            // a step (after a restart it carries on from its RTC record).
            if (reason == PERSON_STOP_REASON || reason == RoofIo::RESTARTING_REASON)
                return CloseAnswer::Withdrawn;
            return CloseAnswer::Refused;
        }

        Decision Sequence::closeNow()
        {
            beforeClose = current;
            beforeCloseSinceMs = phaseSinceMs;
            beforeCloseWallStartMs = phaseWallStartMs;
            Decision out = enter(Phase::Closing);
            out.action = Action::Close;
            closeUnanswered = true;
            return out;
        }

        Decision Sequence::failAttempt(Event event, Cause cause, Action action)
        {
            Decision out = enter(Phase::RetryWait, event, cause);
            out.action = action;
            if (event == Event::CloseFailed)
            {
                closeFailedOnce = true;
                closeFailedAtMs = lastNowMs;
            }
            return out;
        }

        Decision Sequence::enter(Phase next, Event event, Cause cause)
        {
            current = next;
            closeUnanswered = false;
            phaseSinceMs = lastNowMs;
            phaseWallStartMs = lastNowMs;
            closingSeen = false;
            sawPending = false;
            if (next == Phase::Idle)
            {
                lastCritical.has = false; // the episode is over: nothing to remind of
                paused.has = false;       // waiting() keeps it for a short safe spell
                parkRefusals = 0;
            }
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
            // Each event and cause alerts at most once per REMIND_MS, across
            // episodes, so a flapping verdict can't turn a lasting fault into
            // an alert storm, while a new cause (a fault after a refusal) still alerts.
            const size_t e = static_cast<size_t>(decision.event);
            const size_t c = static_cast<size_t>(decision.cause);
            decision.alert = !alertedEver[e][c] || elapsed(alertedAtMs[e][c], lastNowMs) >= REMIND_MS;
            if (decision.alert)
            {
                alertedEver[e][c] = true;
                alertedAtMs[e][c] = lastNowMs;
            }
            lastCritical.has = true;
            lastCritical.event = decision.event;
            lastCritical.cause = decision.cause;
            lastCritical.limitMs = decision.limitMs;
            if (decision.alert)
                lastCritical.alertedAtMs = lastNowMs;
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
