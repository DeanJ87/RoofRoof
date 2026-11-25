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

            // Constitution I: only a person moves a roof whose position is
            // unknown; 8.0.0 (spec 030): also the automatic close, towards
            // closed only, at the slow duty to the first magnet like a person's.
            bool mayMoveFromUnknown(End target, Origin origin)
            {
                return origin == Origin::Person || (origin == Origin::Automatic && target == End::Closed);
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
            const bool late = stepped && (move.driving || blind.driving) && gapMs > MAX_STEP_GAP_MS;
            if (move.driving && stepped)
                accountDrive(gapMs);
            stepped = true;
            lastStepMs = readings.nowMs;
            last = readings;
            current.hallCounts = readings.hallCounts;
            noteHall(readings.hallCounts);
            current.hall = hall.update(readings.hallCounts, HallThresholds{config.hallHighCounts, config.hallLowCounts}, readings.nowMs);
            updateDeadTime(readings.nowMs);
            if (current.hall == HallLevel::High || current.hall == HallLevel::Low)
                driveBudgetUsedMs[0] = driveBudgetUsedMs[1] = 0;

            // constitution I: a late step means the motor ran unsupervised.
            if (late)
                latch(Fault{FaultKind::LateStep, static_cast<float>(gapMs), static_cast<float>(MAX_STEP_GAP_MS)});
            checkHallRail(readings.hallCounts, readings.nowMs);
            // Spec 030: with the Hall rail fault latched; spec 033: a person's
            // blind jog, also with the Hall reading not settled or at a rail.
            if (blind.active)
                stepBlind(readings);
            else if (current.fault.kind == FaultKind::None)
            {
                if (move.active)
                    stepMove(readings);
                else
                    followHall();
            }
            noteMove(readings.nowMs);
            return out;
        }

        // The move's part of the status, after a step.
        void Controller::noteMove(uint32_t nowMs)
        {
            current.driving = move.driving || blind.driving;
            const uint32_t startMs = move.driving ? move.driveStartMs : blind.driveStartMs;
            current.moveElapsedMs = current.driving ? nowMs - startMs : 0;
            current.calibrating = move.active && move.calibration;
            current.jogging = (move.active && move.jog) || (blind.active && blind.jog);
            current.blind = blind.active && !blind.jog;
            current.blindReady = blindCloseReady();
        }

        // The gap just driven: the estimate, the drive budget and (spec 016) the
        // duty a calibration leg applied over it, for its speed.
        void Controller::accountDrive(uint32_t gapMs)
        {
            advanceEstimate(gapMs);
            uint32_t &used = driveBudgetUsedMs[static_cast<int>(move.target)];
            used = gapMs > UINT32_MAX - used ? UINT32_MAX : used + gapMs;
            if (move.calibration)
                move.dutyMs += static_cast<float>(out.duty) / 255.0f * static_cast<float>(gapMs);
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
                if (blind.active)
                    endBlind(BlindEnd::Stopped);
                stopMove(false);
                return CommandResult{};
            case Command::Stop:
            default:
                // Anything unrecognised stops: the safe reading of a bad command.
                if (blind.active)
                    endBlind(BlindEnd::Stopped);
                stopMove(true);
                return CommandResult{};
            }
        }

        // A Hall reading at a rail latches its fault (constitution I); spec 027:
        // one latched at rest clears itself after a minute of good readings.
        void Controller::checkHallRail(uint16_t counts, uint32_t nowMs)
        {
            if (!bootSeen)
            {
                bootSeen = true;
                bootMs = nowMs;
            }
            // Spec 030: the blind close runs because the sensor is known dead.
            if (blind.active)
            {
                goodAtRest.active = false;
                return;
            }
            // A rail in the first moments after start-up, at rest, is the sensor
            // and the ADC settling: not judged (no move can start meanwhile: the
            // Hall reads not settled).
            const bool settling = !move.active && nowMs - bootMs < HALL_BOOT_SETTLE_MS;
            if (current.hall == HallLevel::Rail && !settling)
            {
                if (current.fault.kind == FaultKind::None)
                    hallRailAtRest = !move.active; // before the latch ends the move
                const float rail = counts <= HALL_RAIL_LOW_COUNTS ? HALL_RAIL_LOW_COUNTS : HALL_RAIL_HIGH_COUNTS;
                latch(Fault{FaultKind::HallRail, static_cast<float>(counts), rail});
            }
            clearRestRail(nowMs);
        }

        bool Controller::atEnd(Command command) const
        {
            if ((command != Command::Open && command != Command::Close) || move.active || current.fault.kind != FaultKind::None)
                return false;
            End at = End::Closed;
            return magnetEnd(at) && at == (command == Command::Open ? End::Open : End::Closed);
        }

        // Spec 027 (constitution 7.0.0, I): a Hall rail fault latched with no
        // move under way clears after HALL_REST_CLEAR_MS of readings off the
        // rails, still at rest; the position is then read afresh from the Hall.
        void Controller::clearRestRail(uint32_t nowMs)
        {
            const bool good = current.hall != HallLevel::Rail && current.hall != HallLevel::Unsettled;
            if (!faultClearsAtRest() || move.active || !good)
            {
                goodAtRest.active = false;
                return;
            }
            if (!goodAtRest.active)
            {
                goodAtRest.active = true;
                goodAtRest.sinceMs = nowMs;
                return;
            }
            if (nowMs - goodAtRest.sinceMs < HALL_REST_CLEAR_MS)
                return;
            goodAtRest.active = false;
            hallRailAtRest = false;
            blindRan = false;
            current.fault = Fault{};
            current.positionKnown = false;
            current.state = State::Unknown;
            followHall();
        }

        CommandResult Controller::check(Command command, Origin origin) const
        {
            if (command != Command::Open && command != Command::Close)
                return CommandResult{};
            if (current.fault.kind != FaultKind::None)
                return refused(Refusal::FaultLatched);
            if (blind.active || (move.active && move.jog))
                return refused(Refusal::Moving); // spec 019: a jog is never taken over (spec 033: a blind one too)
            const End target = command == Command::Open ? End::Open : End::Closed;
            End at = End::Closed;
            if (move.active ? move.target == target : (magnetEnd(at) && at == target))
                return CommandResult{};
            const CommandResult notReady = readinessRefusal();
            if (!notReady.accepted)
                return notReady;
            if (!current.positionKnown && !mayMoveFromUnknown(target, origin))
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
            return blindReadiness();
        }

        CommandResult Controller::requestMove(End target, Origin origin)
        {
            if (current.fault.kind != FaultKind::None)
                return refused(Refusal::FaultLatched);
            if (blind.active || (move.active && move.jog))
                return refused(Refusal::Moving); // spec 019: a jog is never taken over (spec 033: a blind one too)
            End at = End::Closed;
            if (move.active ? move.target == target : (magnetEnd(at) && at == target))
                return CommandResult{}; // already there, or already on its way
            const CommandResult notReady = readinessRefusal();
            if (!notReady.accepted)
                return notReady;
            // constitution I: never by itself from an unknown position, except
            // (8.0.0) the automatic close's Close.
            if (!current.positionKnown && !mayMoveFromUnknown(target, origin))
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
            if (blind.active)
                endBlind(BlindEnd::Fault, fault);
            endLeg(false);
            stopDriving();
            move = Move{};
            current.positionKnown = false;
            current.fromUnknown = false;
            current.state = State::Error;
            if (current.fault.kind != FaultKind::None)
                return; // the first fault is the one to report
            current.fault = fault;
            faultTrail = recentHall;
            if (current.fault.kind == FaultKind::None)
                current.fault.kind = FaultKind::External;
            ++faultsLatched;
        }

        void Controller::noteHall(uint16_t counts)
        {
            HallTrail &t = recentHall;
            if (t.count == HALL_TRAIL_READINGS)
            {
                for (uint8_t i = 1; i < HALL_TRAIL_READINGS; ++i)
                    t.counts[i - 1] = t.counts[i];
                --t.count;
            }
            t.counts[t.count++] = counts;
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
            if (blind.active)
                endBlind(BlindEnd::Stopped);
            current.fault = Fault{};
            hallRailAtRest = false;
            blindRan = false;
            driveBudgetUsedMs[0] = driveBudgetUsedMs[1] = 0; // a person has looked at the roof
            current.positionKnown = false;
            current.state = State::Unknown;
            followHall();
            return CommandResult{};
        }

        CommandResult Controller::applySettings(const Settings &settings)
        {
            if (move.active || blind.active)
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
