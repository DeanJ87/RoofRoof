#include "RoofController.h"

#include <algorithm>
#include <cmath>

#include "MotionProfile.h"

// One control step of a move (research R7): wait out the dead time, then the
// magnets, the times, the electrical checks, and finally the profile's duty.
// Any check that fails latches a fault and switches the output off on this step.

namespace SQM
{
    namespace Roof
    {
        void Controller::stepMove(const Readings &readings)
        {
            if (!move.driving)
            {
                End at = End::Closed;
                if (magnetEnd(at) && at == move.target)
                {
                    arrive();
                    return;
                }
                if (!deadTimeDone)
                    return;
                startDriving(readings.nowMs);
            }
            if (checkMagnets())
                return;
            if (checkTimes(readings.nowMs) || checkElectrical(readings))
                return;
            driveProfile(readings.nowMs);
        }

        void Controller::startDriving(uint32_t nowMs)
        {
            End at = End::Closed;
            move.driving = true;
            move.driveStartMs = nowMs;
            move.startedAtMagnet = magnetEnd(at) && at != move.target;
            move.leftStart = false;
            move.timeoutMs = move.fromUnknown ? unknownMoveTimeoutMs(config) : moveTimeoutMs(config, remainingMm());
            current.moveTimeoutMs = move.timeoutMs;
            resetPending();
        }

        // constitution I: off for the dead time between any two drives, and after
        // start-up. Both ends of the interval are step times (driveEndMs is the
        // first step after the driver went off), so no caller's clock is involved.
        // Called every step: once the dead time has elapsed it is latched as done,
        // so the stamp is only ever compared within a few steps of being taken.
        void Controller::updateDeadTime(uint32_t nowMs)
        {
            if (driveEndPending)
            {
                driveEndMs = nowMs;
                driveEndPending = false;
                deadTimeDone = false;
            }
            if (deadTimeDone)
                return;
            // Signed, so a step clock that went backwards (an integration bug) reads
            // as still waiting, never as long past.
            const int32_t sinceEndMs = static_cast<int32_t>(nowMs - driveEndMs);
            deadTimeDone = sinceEndMs >= static_cast<int32_t>(DEAD_TIME_MS);
        }

        // True when the move ended: at its magnet, or on a wrong one (a fault).
        bool Controller::checkMagnets()
        {
            End at = End::Closed;
            if (!magnetEnd(at))
            {
                if (current.hall == HallLevel::Between)
                    move.leftStart = true;
                return false;
            }
            if (at == move.target)
            {
                arrive();
                return true;
            }
            // Still on the start magnet is expected; the other end's magnet, or the
            // start magnet again after leaving it, means the roof runs backwards.
            if (move.startedAtMagnet && !move.leftStart)
                return false;
            latch(Fault{FaultKind::WrongMagnet, static_cast<float>(at), static_cast<float>(move.target)});
            return true;
        }

        // The leave time and the hard timeout (constitution I), `>=` the limit.
        bool Controller::checkTimes(uint32_t nowMs)
        {
            const uint32_t elapsedMs = nowMs - move.driveStartMs;
            if (move.startedAtMagnet && !move.leftStart && elapsedMs >= config.leaveMagnetMs)
            {
                latch(Fault{FaultKind::DidNotLeave, static_cast<float>(elapsedMs), static_cast<float>(config.leaveMagnetMs)});
                return true;
            }
            if (elapsedMs >= move.timeoutMs)
            {
                latch(Fault{FaultKind::Timeout, static_cast<float>(elapsedMs), static_cast<float>(move.timeoutMs)});
                return true;
            }
            // Repeated short moves that never reach a magnet (a jam, with current
            // sense off) can't each restart the clock forever (FR-025).
            // Settings taken at rest can make the budget smaller than what is already
            // used; the next drive then faults on its first step (fail-safe).
            const uint32_t budgetMs = driveBudgetMs(config);
            const uint32_t usedMs = driveBudgetUsedMs[static_cast<int>(move.target)];
            if (usedMs >= budgetMs)
            {
                latch(Fault{FaultKind::DriveBudget, static_cast<float>(usedMs), static_cast<float>(budgetMs)});
                return true;
            }
            return false;
        }

        // Supply and, when fitted, current: each must be wrong for CONFIRM_MS.
        bool Controller::checkElectrical(const Readings &readings)
        {
            const uint32_t nowMs = readings.nowMs;
            // Unreadable (NaN, infinite, implausibly high) counts as low (ERR-04).
            const bool supplyBad = !supplyReadable(readings.supplyVolts) || readings.supplyVolts < config.minSupplyVolts;
            if (confirmed(supplyLow, supplyBad, nowMs))
            {
                latch(Fault{FaultKind::SupplyLow, readings.supplyVolts, config.minSupplyVolts});
                return true;
            }
            if (!config.currentSenseEnabled)
                return false;
            const bool unreadable = !currentReadable(readings.motorAmps);
            if (confirmed(currentUnreadable, unreadable, nowMs))
            {
                latch(Fault{FaultKind::CurrentUnreadable, readings.motorAmps, config.maxCurrentAmps});
                return true;
            }
            if (confirmed(overCurrent, !unreadable && readings.motorAmps >= config.maxCurrentAmps, nowMs))
            {
                latch(Fault{FaultKind::OverCurrent, readings.motorAmps, config.maxCurrentAmps});
                return true;
            }
            return false;
        }

        // Whether `condition` has now held for CONFIRM_MS; it restarts when it lapses.
        bool Controller::confirmed(Pending &pending, bool condition, uint32_t nowMs) const
        {
            if (!condition)
            {
                pending = Pending{};
                return false;
            }
            if (!pending.active)
            {
                pending.active = true;
                pending.sinceMs = nowMs;
            }
            return nowMs - pending.sinceMs >= CONFIRM_MS;
        }

        void Controller::driveProfile(uint32_t nowMs)
        {
            const float fraction =
                move.fromUnknown ? dutyFraction(config.slowDutyPercent) : profileFraction(config, nowMs - move.driveStartMs, remainingMm());
            const Driver::Command closeDirection =
                config.openDirection == Driver::Command::Forward ? Driver::Command::Reverse : Driver::Command::Forward;
            out.command = move.target == End::Open ? config.openDirection : closeDirection;
            out.duty = toDuty(fraction);
        }

        // The estimate moves with the duty applied since the last step (research R2).
        void Controller::advanceEstimate(uint32_t elapsedMs)
        {
            if (!current.positionKnown || move.fromUnknown)
                return;
            const float stepMm = advanceMm(config, out.duty, elapsedMs);
            const float travel = static_cast<float>(config.travelMm);
            const float next = current.positionMm + (move.target == End::Open ? stepMm : -stepMm);
            current.positionMm = std::min(travel, std::max(0.0f, next));
        }

        void Controller::arrive()
        {
            stopDriving();
            move = Move{};
            current.fromUnknown = false;
            current.moveTimeoutMs = 0;
            followHall();
        }
    } // namespace Roof
} // namespace SQM
