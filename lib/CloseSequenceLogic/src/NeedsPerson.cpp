#include "NeedsPerson.h"

#include <cstdio>

#include "RoofText.h"

namespace SQM
{
    namespace CloseSeq
    {
        namespace
        {
            constexpr const char *TITLE = "Roof needs a person";

            Alerts::Alert make(const std::string &message, const std::string &reason, bool reminder)
            {
                Alerts::Alert alert;
                alert.type = Alerts::AlertType::NeedsPerson;
                alert.level = Alerts::AlertLevel::Wake;
                alert.title = reminder ? std::string("Still: ") + TITLE : TITLE;
                alert.message = message;
                alert.vars = {{"reason", reason}, {"limit", ""}, {"details", message}, {"reminder", reminder ? "yes" : "no"}};
                return alert;
            }

            std::string hallText(const Roof::Status &status)
            {
                char text[96];
                std::snprintf(
                    text,
                    sizeof(text),
                    "the Hall sensor reads %s (%u counts), not a magnet",
                    Roof::hallLevelName(status.hall),
                    static_cast<unsigned>(status.hallCounts));
                return text;
            }

            bool hallRail(const RoofIo::Snapshot &roof)
            {
                return roof.fault.fault.kind == Roof::FaultKind::HallRail;
            }

            // Spec 036: an end magnet read too early (the roof isn't where it read).
            bool endTooEarly(const RoofIo::Snapshot &roof)
            {
                return roof.fault.fault.kind == Roof::FaultKind::EndTooEarly;
            }

            // Spec 036: the roof didn't do what its estimate says.
            bool motionDoubt(const RoofIo::Snapshot &roof)
            {
                const Roof::FaultKind kind = roof.fault.fault.kind;
                return kind == Roof::FaultKind::EndTooEarly || kind == Roof::FaultKind::NoEndMagnet || kind == Roof::FaultKind::NoMovement;
            }

            // A fault that needs a person at once, whatever held before.
            bool sensorFault(const RoofIo::Snapshot &roof)
            {
                return hallRail(roof) || motionDoubt(roof);
            }

            std::string notClosedText(const RoofIo::Snapshot &roof)
            {
                const Roof::Status &status = roof.status;
                std::string where = Roof::stateName(status.state);
                if (status.positionKnown && status.state == Roof::State::Stopped)
                {
                    char mm[32];
                    std::snprintf(mm, sizeof(mm), " at about %.0f mm", static_cast<double>(status.positionMm));
                    where += mm;
                }
                return "The device restarted with the roof " + where +
                       ", not closed. Check that it is meant to be open; the automatic close still closes it when the weather "
                       "turns unsafe.";
            }
        } // namespace

        bool roofStuck(const RoofIo::Snapshot &roof, bool roofStarted)
        {
            return !roofStarted || roof.fault.fault.kind != Roof::FaultKind::None || !roof.status.positionKnown;
        }

        std::string stuckText(const RoofIo::Snapshot &roof, bool roofStarted)
        {
            if (!roofStarted)
                return "The roof controller didn't start, so the motor stays off. Check the pin settings and restart the device.";
            if (hallRail(roof) && roof.fault.atRest)
                return "Hall sensor fault: " + RoofIo::describeFault(roof.fault) +
                       ". It clears itself after a minute of good readings at rest; if it doesn't, check the Hall sensor and its "
                       "cable, then clear the fault.";
            if (hallRail(roof))
                return "Hall sensor fault: " + RoofIo::describeFault(roof.fault) +
                       ". Check the roof, the Hall sensor and its cable, then clear the fault.";
            if (endTooEarly(roof))
                return RoofIo::describeFault(roof.fault) +
                       ". Check where the roof really is, the magnets and the Hall thresholds (teach them again), then clear the fault. "
                       "The roof must reach the other end before it moves toward this one again.";
            if (motionDoubt(roof))
                return RoofIo::describeFault(roof.fault) +
                       ". Check where the roof really is, the magnets, the speed and travel settings and the slow duty, then clear "
                       "the fault.";
            if (roof.fault.fault.kind != Roof::FaultKind::None)
                return "Fault latched: " + RoofIo::describeFault(roof.fault) + ". Check the roof, then clear the fault.";
            if (!roof.status.positionKnown)
                return "Position unknown: " + hallText(roof.status) +
                       ". Check the roof, then move it to a magnet (Open or Close) or teach the position.";
            return "";
        }

        bool NeedsPerson::judgeRestart(const NeedsPersonInputs &in, Alerts::Alert &out)
        {
            const RoofIo::Snapshot &roof = *in.roof;
            const bool settled = roof.status.hall != Roof::HallLevel::Unsettled || roof.fault.fault.kind != Roof::FaultKind::None;
            if (!settled && in.nowMs < NEEDS_PERSON_BOOT_MS)
                return false;
            judged = true;
            alertedAtMs = in.nowMs;
            if (roofStuck(roof, in.roofStarted))
            {
                condition = Condition::Restart; // until the roof isn't stuck any more
                hallRailSeen = sensorFault(roof);
                line = stuckText(roof, in.roofStarted);
                out = make("The device restarted. " + line, line, false);
                return true;
            }
            if (roof.status.state == Roof::State::Closed)
                return false;
            out = make(notClosedText(roof), std::string("Roof ") + Roof::stateName(roof.status.state) + " after a restart", false);
            return true; // once: an open roof at a known position isn't a fault
        }

        bool NeedsPerson::remind(const NeedsPersonInputs &in, Alerts::Alert &out)
        {
            // The close sequence reminds while it can't move: one reminder, not two.
            if (cannotMove || in.nowMs - alertedAtMs < in.remindMs)
                return false;
            alertedAtMs = in.nowMs;
            out = make(line, line, true);
            return true;
        }

        bool NeedsPerson::step(const NeedsPersonInputs &in, Alerts::Alert &out)
        {
            if (in.roof == nullptr)
                return false;
            const RoofIo::Snapshot &roof = *in.roof;
            cannotMove = in.phase == Phase::CannotMove;
            if (!judged)
                return judgeRestart(in, out);
            const bool rail = sensorFault(roof);
            const bool railNew = rail && !hallRailSeen;
            hallRailSeen = rail;
            if (railNew)
            {
                // A Hall sensor failing (spec 036: or an end read too early) at
                // any time: at once, whatever held before.
                condition = Condition::HallRail;
                alertedAtMs = in.nowMs;
                line = stuckText(roof, in.roofStarted);
                out = make(hallRail(roof) ? "The Hall sensor failed. " + line : line, line, false);
                return true;
            }
            const bool holds = condition == Condition::HallRail ? rail : roofStuck(roof, in.roofStarted);
            if (condition != Condition::None && !holds)
            {
                condition = Condition::None;
            }
            if (condition == Condition::None)
            {
                line = cannotMove ? "The automatic close can't move the roof. " + stuckText(roof, in.roofStarted) : std::string();
                return false;
            }
            line = stuckText(roof, in.roofStarted);
            return remind(in, out);
        }
    } // namespace CloseSeq
} // namespace SQM
