#include "RoofController.h"

#include "MotionProfile.h"

// Spec 030 (constitution 8.0.0, I): the blind close. With the Hall sensor
// dead (a Hall rail fault latched) the automatic close may drive the roof
// towards closed once, on time alone, at the slow duty. No magnet can end it:
// the time limit, a stall seen as supply sag, a stop or a fault does.
// Spec 033 (constitution 9.0.0, I): a person's jog with the Hall sensor
// failed runs the same way, toward either end, while it is held.

namespace SQM
{
    namespace Roof
    {
        // Spec 033: a person's blind jog doesn't stand in the way: the close
        // takes over from it, as it does from a jog (spec 019).
        bool Controller::blindCloseReady() const
        {
            return config.blindClose && current.fault.kind == FaultKind::HallRail && !blindRan && !(blind.active && !blind.jog);
        }

        // What a move needs besides the Hall sensor: valid settings and power.
        CommandResult Controller::blindReadiness() const
        {
            CommandResult result;
            if (!settingsValid)
                result.reason = Refusal::SettingsInvalid;
            else if (!supplyReadable(last.supplyVolts))
                result = CommandResult{false, Refusal::SupplyUnreadable, last.supplyVolts, config.minSupplyVolts};
            else if (!(last.supplyVolts >= config.minSupplyVolts))
                result = CommandResult{false, Refusal::SupplyLow, last.supplyVolts, config.minSupplyVolts};
            else if (config.currentSenseEnabled && !currentReadable(last.motorAmps))
                result = CommandResult{false, Refusal::CurrentUnreadable, last.motorAmps, config.maxCurrentAmps};
            result.accepted = result.reason == Refusal::None;
            return result;
        }

        CommandResult Controller::blindClose(Origin origin)
        {
            if (origin == Origin::Automatic && blind.active && !blind.jog)
                return CommandResult{}; // already under way
            CommandResult result;
            if (origin != Origin::Automatic || !blindCloseReady())
            {
                result.accepted = false;
                result.reason = Refusal::BlindUnavailable;
                return result;
            }
            result = blindReadiness();
            if (!result.accepted)
                return result;
            if (blind.active)
                endBlind(BlindEnd::Stopped); // a person's blind jog (spec 033)
            startBlind(End::Closed, unknownMoveTimeoutMs(config));
            // Counted from the request: a restart in the middle runs no second one.
            blindRan = true;
            current.blind = true;
            current.blindReady = false;
            return result;
        }

        void Controller::startBlind(End target, uint32_t limitMs)
        {
            blind = Blind{};
            blind.active = true;
            blind.target = target;
            blind.limitMs = limitMs;
            // A full dead time from the next step, whatever ran before: the
            // owner saves the done flag meanwhile (nothing drives yet).
            driveEndPending = true;
            deadTimeDone = false;
            current.state = target == End::Open ? State::Opening : State::Closing;
            current.target = target;
            current.fromUnknown = true;
            current.moveTimeoutMs = limitMs;
        }

        void Controller::stepBlind(const Readings &readings)
        {
            // Spec 033: a blind jog not renewed in time stops, in the dead time too.
            if (blind.jog && jogExpired(readings.nowMs))
            {
                endBlind(BlindEnd::Stopped);
                return;
            }
            if (!blind.driving)
            {
                if (!deadTimeDone)
                    return;
                blind.driving = true;
                blind.driveStartMs = readings.nowMs;
                blind.restVolts = readings.supplyVolts; // read with the driver off
                resetPending();
            }
            const uint32_t drivenMs = readings.nowMs - blind.driveStartMs;
            if (drivenMs >= blind.limitMs)
            {
                endBlind(BlindEnd::Limit);
                return;
            }
            if (checkElectrical(readings))
                return; // latched: latch() ended it as a fault
            if (blindStalled(readings, drivenMs))
            {
                endBlind(BlindEnd::Stall);
                return;
            }
            const Driver::Command closeDirection =
                config.openDirection == Driver::Command::Forward ? Driver::Command::Reverse : Driver::Command::Forward;
            out.command = blind.target == End::Open ? config.openDirection : closeDirection;
            out.duty = toDuty(dutyFraction(config.slowDutyPercent));
        }

        // Research R6: the supply sags about 1.1 V while the motor runs and
        // further when it stalls at the chain's end. The start-up surge is not judged.
        bool Controller::blindStalled(const Readings &readings, uint32_t drivenMs)
        {
            const float sag = blind.restVolts - readings.supplyVolts;
            if (drivenMs < BLIND_SURGE_MS || !(sag >= config.blindSagVolts))
            {
                if (drivenMs >= BLIND_SURGE_MS && sag > blind.deepestSag)
                    blind.deepestSag = sag;
                blind.sag = Pending{};
                return false;
            }
            if (sag > blind.deepestSag)
                blind.deepestSag = sag;
            if (!blind.sag.active)
            {
                blind.sag.active = true;
                blind.sag.sinceMs = readings.nowMs;
            }
            return readings.nowMs - blind.sag.sinceMs >= BLIND_STALL_MS;
        }

        // Spec 033: a blind jog's end isn't reported as a blind close's.
        void Controller::endBlind(BlindEnd why, const Fault &fault)
        {
            if (!blind.jog)
            {
                blindLast.ended++;
                blindLast.why = why;
                blindLast.drivenMs = blind.driving ? last.nowMs - blind.driveStartMs : 0;
                blindLast.restVolts = blind.restVolts;
                blindLast.deepestSag = blind.deepestSag;
                blindLast.fault = fault;
            }
            if (blind.driving)
            {
                driveEndPending = true; // timed from the next step
                deadTimeDone = false;
            }
            blind = Blind{};
            out = Output{};
            resetPending();
            current.driving = false;
            current.moveElapsedMs = 0;
            current.moveTimeoutMs = 0;
            current.fromUnknown = false;
            current.blind = false;
            current.jogging = false;
            current.positionKnown = false;
            // The Hall rail fault is still latched (a blind jog may start before it is).
            current.state = current.fault.kind == FaultKind::None ? State::Unknown : State::Error;
        }
    } // namespace Roof
} // namespace SQM
