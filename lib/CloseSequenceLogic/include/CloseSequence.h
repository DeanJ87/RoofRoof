#pragma once

// The automatic close on unsafe weather (spec 005; constitution II and III):
// when the verdict is not safe and the roof is not closed, ask N.I.N.A. to
// park, wait for the park to be confirmed, close, and confirm closed. Every
// step has a time limit; a step that runs out stops the sequence where it is
// and raises a critical event naming it. The roof never opens from here, and
// nothing is sent while the position is unknown, a fault is latched or a
// person has stopped the sequence. Pure: time and inputs come in, one action
// and one event go out (data-model.md has the transition table). The close
// itself goes through spec 003's gates like every other close.

#include <cstdint>
#include <string>

#include "ParkRule.h"
#include "RoofGates.h"
#include "RoofTypes.h"
#include "SafetyInput.h"

namespace SQM
{
    namespace CloseSeq
    {
        // Two park requests never closer than this (research R5).
        constexpr uint64_t PARK_REQUEST_GAP_MS = 30000;
        // The park result must say parked this long without a break (R4).
        constexpr uint64_t PARK_SETTLE_MS = 4000;
        // After a failed attempt, the wait before the next park request (R6).
        constexpr uint64_t RETRY_AFTER_MS = 300000;
        // A stood-down sequence re-arms after this long safe without a break (R7).
        constexpr uint64_t REARM_SAFE_MS = 600000;
        // The position must be unknown this long before standing down (R8).
        constexpr uint64_t UNKNOWN_CONFIRM_MS = 2000;
        // A waiting close (spec 003 closePending) must start within this (R6).
        constexpr uint64_t CLOSE_START_MS = 30000;
        // A moving close may take its own timeout plus this (R6).
        constexpr uint64_t CLOSE_GUARD_SLACK_MS = 30000;
        // A critical event alerts at most this often, and is raised again this
        // often while the roof stays open after it (research R10).
        constexpr uint64_t REMIND_MS = 1800000;
        // RoofControl's refusal reason when a person stopped the roof since
        // the sequence decided to close.
        constexpr const char *PERSON_STOP_REASON = "personStop";
        // The gate refusing the close for the park this many times in a row
        // counts as a refusal (back-off and alert).
        constexpr uint8_t MAX_PARK_REFUSALS = 3;
        // A restarted attempt or back-off counts this much more than it kept:
        // the restart itself took time no clock ran through (start-up, WiFi).
        constexpr uint64_t RESTART_ALLOWANCE_MS = 10000;
        // park.timeoutSeconds' default, in ms.
        constexpr uint32_t DEFAULT_PARK_TIMEOUT_MS = 240000;

        // One step's inputs (data-model.md).
        struct Inputs
        {
            uint64_t nowMs = 0;       // milliseconds since start, never wraps
            bool weatherSafe = false; // the verdict is Safe (unknown and unsafe are not)
            Roof::State roof = Roof::State::Unknown;
            bool closePending = false;  // spec 003: a close waits for an upload or MQTT connect
            uint32_t moveTimeoutMs = 0; // the move under way's own timeout (0: none)
            bool parked = false;        // spec 004's park result
            bool ninaConfigured = true; // the park result doesn't say "N.I.N.A. not configured"
            uint32_t personStops = 0;   // a person's accepted stops and aborts since start
            bool roofStarted = true;    // the roof's control task runs (else it reads Unknown for ever)
        };

        enum class Phase : uint8_t
        {
            Idle,         // safe, or the roof is closed
            WaitPark,     // an attempt: park requested, waiting for it to hold
            Closing,      // the close was commanded, or one was already running
            RetryWait,    // an attempt failed: no request until the back-off ends
            HeldByPerson, // a person stopped the sequence
            CannotMove,   // the position is unknown or a fault is latched
            Halted,       // a close outlasted its guard and was stopped: waits for a person
        };

        // What the caller must do now. There is no open (constitution III).
        enum class Action : uint8_t
        {
            None,
            RequestPark, // SafetyInputs::requestPark()
            Close,       // RoofControl::command(Close, Automatic), then closeAnswered()
            Stop,        // RoofControl::command(Stop, Automatic)
        };

        enum class Event : uint8_t
        {
            None,
            Started,          // an attempt began
            EndedSafe,        // the verdict is safe: nothing more to do
            Closed,           // the roof reads closed
            ParkNotConfirmed, // critical: the roof stays open
            CloseFailed,      // critical: the close didn't end closed
            CannotClose,      // critical: position unknown or fault latched
            StoppedByPerson,  // critical: open while unsafe because a person stopped it
        };

        enum class Cause : uint8_t
        {
            None,
            ParkTimeout,
            NinaNotConfigured,
            Fault,       // the roof faulted during the close
            Refused,     // the motion logic refused the close
            Interrupted, // the close ended neither closed nor in error
            TooLong,     // the close outlasted its guard
            PositionUnknown,
            FaultLatched,
            RoofNotStarted, // the roof controller didn't start: nothing can move
        };

        // What one step decided.
        struct Decision
        {
            Action action = Action::None;
            Event event = Event::None;
            Cause cause = Cause::None;
            bool alert = false;    // a critical event, first of its kind this episode
            uint64_t limitMs = 0;  // the time limit that ran out (ParkTimeout, TooLong)
            bool reminder = false; // the episode's last critical event, raised again (REMIND_MS)
        };

        // The roof's answer to the Close action.
        enum class CloseAnswer : uint8_t
        {
            Started,     // accepted: moving, or waiting for an upload or MQTT connect
            ParkRefused, // spec 003's gate: the park no longer holds
            Withdrawn,   // a person stopped the roof since the decision: not run
            Refused,     // any other refusal (fault, supply, settings, not started)
        };

        // The answer for RoofControl's outcome (its accepted flag and reason name).
        CloseAnswer answerFor(bool accepted, const std::string &reason);

        // An alert's wording (spec 007 replaces it with its own events).
        struct AlertText
        {
            std::string title;
            std::string message;
        };

        // The sequence. One instance, stepped by the main loop.
        class Sequence
        {
        public:
            // park.timeoutSeconds in ms; applies from the next attempt.
            void setParkTimeoutMs(uint32_t ms) { parkTimeoutMs = ms; }
            // One step: at most one action and one event.
            Decision step(const Inputs &in);
            // The roof's answer to the Close action of the last step.
            Decision closeAnswered(CloseAnswer answer);
            Phase phase() const { return current; }
            // What must outlive a restart (a reset, a crash, a brown-out): a
            // stand-down, and an attempt or back-off with the time it has run,
            // so a board that keeps restarting in the rain still reaches its
            // time limits and alerts. Phase Idle when there is nothing to keep.
            struct Saved
            {
                Phase phase = Phase::Idle;
                Event event = Event::None; // the episode's last critical event
                Cause cause = Cause::None;
                uint64_t unsafeMs = 0;     // how long the phase has run (unsafe time)
                uint64_t wallMs = 0;       // and in wall time
                uint64_t limitMs = 0;      // the attempt's park limit
                bool requested = false;    // a park request was sent...
                uint64_t requestAgeMs = 0; // ...this long ago (kept spacing across a restart)
            };
            Saved saved() const;
            // After a restart, before the first step: puts `saved` back, applied
            // on the first step. Values that don't make sense are ignored. A
            // stand-down's event is raised again at once.
            void restore(const Saved &saved);

        private:
            // A condition that must hold without a break for some time.
            struct Run
            {
                bool on = false;
                uint64_t sinceMs = 0;
                // Whether it has held for `ms` as of `nowMs`, given `holds` now.
                bool lasted(bool holds, uint64_t nowMs, uint64_t ms);
            };

            Decision decide(const Inputs &in, bool personStopped);
            Decision remindIfDue(Decision decision);
            Decision halt(uint64_t limitMs);
            Decision resume(const Inputs &in);
            Decision idle(const Inputs &in);
            Decision waiting(const Inputs &in, bool personStopped);
            Decision attempt(const Inputs &in);
            Decision closing(const Inputs &in, bool personStopped);
            Decision stoodDown(const Inputs &in, bool personStopped);
            bool interrupted(const Inputs &in, bool personStopped, Decision &out);
            Decision closeNow();
            Decision failAttempt(Event event, Cause cause, Action action);
            Decision enter(Phase next, Event event = Event::None, Cause cause = Cause::None);
            Action requestIfDue(uint64_t nowMs);
            Decision withAlert(Decision decision);

            Phase current = Phase::Idle;
            uint32_t parkTimeoutMs = DEFAULT_PARK_TIMEOUT_MS;
            uint64_t lastNowMs = 0;
            uint64_t phaseSinceMs = 0;                   // when the current phase (or attempt) began
            uint64_t attemptLimitMs = 0;                 // the park time limit taken when the attempt began
            uint64_t phaseWallStartMs = 0;               // when the phase began, wall time (kept across a resume)
            Roof::State lastRoof = Roof::State::Unknown; // the previous step's roof state
            bool requested = false;                      // a park request was ever sent
            uint64_t lastRequestMs = 0;
            uint32_t seenStops = 0; // personStops at the last step
            Run parkHeld;           // the park result says parked
            Run unknown;            // the roof's position is unknown
            Run safe;               // the verdict is safe
            // This step's results of the three runs.
            bool parkConfirmed = false;    // parked for PARK_SETTLE_MS
            bool unknownConfirmed = false; // unknown for UNKNOWN_CONFIRM_MS
            bool safeLong = false;         // safe for REARM_SAFE_MS
            // Where the close came from, to go back to if the gate refuses it.
            Phase beforeClose = Phase::WaitPark;
            uint64_t beforeCloseSinceMs = 0;
            uint64_t beforeCloseWallStartMs = 0;
            bool closingSeen = false; // the roof has read Closing since the close
            bool sawPending = false;  // the close waited (spec 003 closePending) first
            uint64_t closingSinceMs = 0;
            // The episode's last critical event, for the reminder.
            struct LastCritical
            {
                bool has = false;
                Event event = Event::None;
                Cause cause = Cause::None;
                uint64_t limitMs = 0;
                uint64_t alertedAtMs = 0;
            };
            LastCritical lastCritical;
            // An attempt ended by a safe verdict, kept so a short safe spell
            // doesn't restart its clock (resume()).
            struct Pause
            {
                bool has = false;
                Phase phase = Phase::WaitPark;
                uint64_t sinceMs = 0;
                uint64_t wallStartMs = 0;
                uint64_t limitMs = 0;
                uint64_t endedAtMs = 0;
                LastCritical critical;
            } paused;
            uint8_t parkRefusals = 0;     // the gate refused the close for the park, in a row
            bool closeFailedOnce = false; // a close was refused or interrupted...
            uint64_t closeFailedAtMs = 0; // ...here: no close for RETRY_AFTER_MS
            // When each event and cause last alerted (index: Event, Cause).
            bool alertedEver[8][10] = {};
            uint64_t alertedAtMs[8][10] = {};
            bool remindNow = false; // restored after a restart: remind on the first step
            bool restorePending = false;
            Saved restoring;
            void applyRestore();
        };

        // The verdict and park result into `in`: only Safe is safe (unknown is
        // doubt, constitution III); N.I.N.A. counts as configured unless the
        // park result says it isn't.
        void applySafety(Inputs &in, Safety::Verdict verdict, const Safety::ParkResult &park);
        // The roof's status into `in`.
        void applyRoof(Inputs &in, const Roof::Status &status, bool closePending);

        // ParkNotConfirmed, CloseFailed, CannotClose and StoppedByPerson.
        bool isCritical(Event event);
        // The texts an event's reason can come from (constitution III).
        struct Reasons
        {
            std::string park;    // Safety::describe(park result)
            std::string fault;   // RoofIo::describeFault(latched fault)
            std::string refusal; // the refused close's text
            Roof::State roof = Roof::State::Unknown;
        };
        // Which of `reasons` explains `decision`'s event ("" when the event says it all).
        std::string reasonFor(const Decision &decision, const Reasons &reasons);

        // The title and message for a critical event, with the time limit
        // that ran out; `why` is the park result's, fault's or refusal's own
        // text (constitution III: the value and the limit, or the input that failed).
        AlertText alertFor(const Decision &decision, const std::string &why);

        // Stable machine names for logs.
        const char *phaseName(Phase phase);
        const char *eventName(Event event);
        const char *causeName(Cause cause);
    } // namespace CloseSeq
} // namespace SQM
