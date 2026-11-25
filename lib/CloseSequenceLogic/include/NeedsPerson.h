#pragma once

// Spec 030 (FR-007): "the roof needs a person", never silent. Watches the
// roof's snapshot and the close sequence on the main loop and raises the
// critical needs_person alert for: a restart with the position unknown, a
// fault latched or the roof controller not started (reminded until the
// condition ends); a restart with the roof not reading closed (once); a Hall
// sensor at a rail at any time (at once, reminded while it stays). The close
// sequence unable to move raises its own CannotClose (also needs_person, with
// its own reminders), so this watcher only counts it as "active" for the
// status. Pure: time and the snapshot come in, at most one alert goes out.

#include <cstdint>
#include <string>

#include "AlertEngine.h"
#include "CloseSequence.h"
#include "RoofStatus.h"

namespace SQM
{
    namespace CloseSeq
    {
        // The Hall reading settles within a few readings of a start; the restart
        // is judged once it has, or after this long at the latest.
        constexpr uint64_t NEEDS_PERSON_BOOT_MS = 5000;

        struct NeedsPersonInputs
        {
            uint64_t nowMs = 0; // since start
            const RoofIo::Snapshot *roof = nullptr;
            bool roofStarted = true; // the roof's control task runs
            Phase phase = Phase::Idle;
            uint64_t remindMs = REMIND_MS; // autoClose.remindMinutes
        };

        class NeedsPerson
        {
        public:
            // One step: true with `out` set when an alert must be raised now.
            bool step(const NeedsPersonInputs &in, Alerts::Alert &out);
            // A condition that needs a person holds (the close sequence unable to
            // move included), and what for, in a line (the status and the banner).
            bool active() const { return condition != Condition::None || cannotMove; }
            // The watcher's own condition holds (its alert is dropped once it ends;
            // the one about a restart with the roof open is kept until sent).
            bool conditionHolds() const { return condition != Condition::None; }
            const std::string &text() const { return line; }

        private:
            enum class Condition : uint8_t
            {
                None,
                Restart, // a restart with the position unknown, a fault or the controller not started
                HallRail,
            };
            bool judgeRestart(const NeedsPersonInputs &in, Alerts::Alert &out);
            bool remind(const NeedsPersonInputs &in, Alerts::Alert &out);

            bool judged = false;
            Condition condition = Condition::None;
            bool cannotMove = false;
            bool hallRailSeen = false; // the previous step's fault was a Hall rail
            uint64_t alertedAtMs = 0;
            std::string line;
        };

        // Whether the roof needs a person now: the controller didn't start, a
        // fault is latched, or the position is unknown.
        bool roofStuck(const RoofIo::Snapshot &roof, bool roofStarted);
        // What is wrong, with the measured value (constitution III), and what a
        // person must do. "" when nothing is.
        std::string stuckText(const RoofIo::Snapshot &roof, bool roofStarted);
    } // namespace CloseSeq
} // namespace SQM
