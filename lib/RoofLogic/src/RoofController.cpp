#include "RoofController.h"

#include <cmath>

#include "MotionProfile.h"

// Commands, the idle state and the fault latch. The per-step move is in
// RoofControllerStep.cpp.

namespace SQM
{
    namespace Roof
    {
        namespace
        {
            CommandResult refused(Refusal reason, float measured = 0.0f, float limit = 0.0f)
            {
                CommandResult result;
                result.accepted = false;
                result.reason = reason;
                result.measured = measured;
                result.limit = limit;
                return result;
            }

            State endState(End end)
            {
                return end == End::Open ? State::Open : State::Closed;
            }
        } // namespace

        Controller::Controller(const Settings &settings)
            : config(settings),
              settingsValid(validSettings(settings))
        {
        }

        Output Controller::tick(const Readings &readings)
        {
            const uint32_t gapMs = readings.nowMs - lastStepMs;
            const bool late = stepped && move.driving && gapMs > MAX_STEP_GAP_MS;
            if (move.driving && stepped)
            {
                advanceEstimate(gapMs);
                uint32_t &used = driveBudgetUsedMs[static_cast<int>(move.target)];
                used = gapMs > UINT32_MAX - used ? UINT32_MAX : used + gapMs;
                // Spec 016: the duty applied over the gap, for the leg's speed.
                if (move.calibration)
                    move.dutyMs += static_cast<float>(out.duty) / 255.0f * static_cast<float>(gapMs);
            }
            stepped = true;
            lastStepMs = readings.nowMs;
            last = readings;
            current.hallCounts = readings.hallCounts;
            current.hall = hall.update(readings.hallCounts, HallThresholds{config.hallHighCounts, config.hallLowCounts});
            updateDeadTime(readings.nowMs);
            if (current.hall == HallLevel::High || current.hall == HallLevel::Low)
                driveBudgetUsedMs[0] = driveBudgetUsedMs[1] = 0;

            // constitution I: a late step means the motor ran unsupervised.
            if (late)
                latch(Fault{FaultKind::LateStep, static_cast<float>(gapMs), static_cast<float>(MAX_STEP_GAP_MS)});
            if (current.hall == HallLevel::Rail)
            {
                const float rail = readings.hallCounts <= HALL_RAIL_LOW_COUNTS ? HALL_RAIL_LOW_COUNTS : HALL_RAIL_HIGH_COUNTS;
                latch(Fault{FaultKind::HallRail, static_cast<float>(readings.hallCounts), rail});
            }
            if (current.fault.kind == FaultKind::None)
            {
                if (move.active)
                    stepMove(readings);
                else
                    followHall();
            }
            current.driving = move.driving;
            current.moveElapsedMs = move.driving ? readings.nowMs - move.driveStartMs : 0;
            current.calibrating = move.active && move.calibration;
            return out;
        }

        CommandResult Controller::command(Command command, Origin origin)
        {
            switch (command)
            {
            case Command::Open:
                return requestMove(End::Open, origin);
            case Command::Close:
                return requestMove(End::Closed, origin);
            case Command::Abort:
                stopMove(false);
                return CommandResult{};
            case Command::Stop:
            default:
                // Anything unrecognised stops: the safe reading of a bad command.
                stopMove(true);
                return CommandResult{};
            }
        }

        CommandResult Controller::check(Command command, Origin origin) const
        {
            if (command != Command::Open && command != Command::Close)
                return CommandResult{};
            if (current.fault.kind != FaultKind::None)
                return refused(Refusal::FaultLatched);
            const End target = command == Command::Open ? End::Open : End::Closed;
            End at = End::Closed;
            if (move.active ? move.target == target : (magnetEnd(at) && at == target))
                return CommandResult{};
            const CommandResult notReady = readinessRefusal();
            if (!notReady.accepted)
                return notReady;
            if (!current.positionKnown && origin != Origin::Person)
                return refused(Refusal::PositionUnknown);
            return CommandResult{};
        }

        // Why an open or close can't start now, whatever its origin (FR-004).
        CommandResult Controller::readinessRefusal() const
        {
            if (current.fault.kind != FaultKind::None)
                return refused(Refusal::FaultLatched);
            if (!settingsValid)
                return refused(Refusal::SettingsInvalid);
            if (current.hall == HallLevel::Unsettled || current.hall == HallLevel::Rail)
                return refused(Refusal::HallNotSettled);
            if (!supplyReadable(last.supplyVolts))
                return refused(Refusal::SupplyUnreadable, last.supplyVolts, config.minSupplyVolts);
            if (!(last.supplyVolts >= config.minSupplyVolts))
                return refused(Refusal::SupplyLow, last.supplyVolts, config.minSupplyVolts);
            if (config.currentSenseEnabled && !currentReadable(last.motorAmps))
                return refused(Refusal::CurrentUnreadable, last.motorAmps, config.maxCurrentAmps);
            return CommandResult{};
        }

        CommandResult Controller::requestMove(End target, Origin origin)
        {
            if (current.fault.kind != FaultKind::None)
                return refused(Refusal::FaultLatched);
            End at = End::Closed;
            if (move.active ? move.target == target : (magnetEnd(at) && at == target))
                return CommandResult{}; // already there, or already on its way
            const CommandResult notReady = readinessRefusal();
            if (!notReady.accepted)
                return notReady;
            // constitution I: never by itself from an unknown position.
            if (!current.positionKnown && origin != Origin::Person)
                return refused(Refusal::PositionUnknown);

            // A reversal stops first; the dead time runs before the new drive.
            // A calibration leg replaced by a normal move ends there (spec 016).
            endLeg(false);
            stopDriving();
            move = Move{};
            move.active = true;
            move.target = target;
            move.fromUnknown = !current.positionKnown;
            current.target = target;
            current.state = target == End::Open ? State::Opening : State::Closing;
            current.fromUnknown = move.fromUnknown;
            current.moveTimeoutMs = 0;
            return CommandResult{};
        }

        void Controller::stopMove(bool keepEstimate)
        {
            if (!move.active)
                return;
            endLeg(false);
            stopDriving();
            // constitution I: an abort part-way leaves the position unknown.
            if (!keepEstimate || move.fromUnknown)
                current.positionKnown = false;
            move = Move{};
            settleIdle();
        }

        void Controller::stopDriving()
        {
            if (move.driving)
            {
                driveEndPending = true; // timed from the next step, never from a caller's clock
                deadTimeDone = false;
            }
            move.driving = false;
            current.driving = false;
            current.moveElapsedMs = 0;
            out = Output{};
            resetPending();
        }

        void Controller::latch(const Fault &fault)
        {
            endLeg(false);
            stopDriving();
            move = Move{};
            current.positionKnown = false;
            current.fromUnknown = false;
            current.state = State::Error;
            if (current.fault.kind != FaultKind::None)
                return; // the first fault is the one to report
            current.fault = fault;
            if (current.fault.kind == FaultKind::None)
                current.fault.kind = FaultKind::External;
        }

        void Controller::raiseFault(const Fault &fault)
        {
            latch(fault);
        }

        CommandResult Controller::clearFault(Origin origin)
        {
            if (origin != Origin::Person)
                return refused(Refusal::NotAPerson);
            if (current.fault.kind == FaultKind::None)
                return CommandResult{};
            current.fault = Fault{};
            driveBudgetUsedMs[0] = driveBudgetUsedMs[1] = 0; // a person has looked at the roof
            current.positionKnown = false;
            current.state = State::Unknown;
            followHall();
            return CommandResult{};
        }

        CommandResult Controller::applySettings(const Settings &settings)
        {
            if (move.active)
                return refused(Refusal::Moving);
            config = settings;
            settingsValid = validSettings(settings);
            // The estimate was made with the old travel and speed.
            if (current.state == State::Stopped)
            {
                current.positionKnown = false;
                current.state = State::Unknown;
            }
            if (current.fault.kind == FaultKind::None)
                followHall();
            return CommandResult{};
        }

        // Idle: the magnets are the truth; leaving one at rest loses the position.
        void Controller::followHall()
        {
            End at = End::Closed;
            if (magnetEnd(at))
            {
                current.positionKnown = true;
                current.positionMm = at == End::Open ? static_cast<float>(config.travelMm) : 0.0f;
                current.state = endState(at);
                return;
            }
            if (current.hall != HallLevel::Between)
                return;
            if (current.state == State::Open || current.state == State::Closed)
            {
                current.positionKnown = false;
                current.state = State::Unknown;
            }
        }

        // The idle state after a move ends without arriving.
        void Controller::settleIdle()
        {
            current.fromUnknown = false;
            current.moveTimeoutMs = 0;
            End at = End::Closed;
            if (magnetEnd(at))
                followHall();
            else
                current.state = current.positionKnown ? State::Stopped : State::Unknown;
        }

        bool Controller::magnetEnd(End &end) const
        {
            const End other = config.hallHighEnd == End::Open ? End::Closed : End::Open;
            if (current.hall == HallLevel::High)
                end = config.hallHighEnd;
            else if (current.hall == HallLevel::Low)
                end = other;
            else
                return false;
            return true;
        }

        float Controller::remainingMm() const
        {
            const float travel = static_cast<float>(config.travelMm);
            return move.target == End::Open ? travel - current.positionMm : current.positionMm;
        }

        void Controller::resetPending()
        {
            supplyLow = Pending{};
            overCurrent = Pending{};
            currentUnreadable = Pending{};
        }
    } // namespace Roof
} // namespace SQM
