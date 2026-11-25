#include "RoofController.h"

#include "MotionProfile.h"

// Spec 030 (constitution 8.0.0, I): the blind close. With the Hall sensor
// dead (a Hall rail fault latched) the automatic close may drive the roof
// towards closed once, on time alone, at the slow duty. No magnet can end it:
// the time limit, a stall seen as supply sag, a stop or a fault does.

namespace SQM
{
    namespace Roof
    {
        bool Controller::blindCloseReady() const
        {
            return config.blindClose && current.fault.kind == FaultKind::HallRail && !blindRan && !blind.active;
        }

        CommandResult Controller::blindClose(Origin origin)
        {
            CommandResult result;
            if (origin != Origin::Automatic || !blindCloseReady())
                result.reason = blind.active && origin == Origin::Automatic ? Refusal::None : Refusal::BlindUnavailable;
            else if (!settingsValid)
                result.reason = Refusal::SettingsInvalid;
            else if (!supplyReadable(last.supplyVolts))
                result = CommandResult{false, Refusal::SupplyUnreadable, last.supplyVolts, config.minSupplyVolts};
            else if (!(last.supplyVolts >= config.minSupplyVolts))
                result = CommandResult{false, Refusal::SupplyLow, last.supplyVolts, config.minSupplyVolts};
            else if (config.currentSenseEnabled && !currentReadable(last.motorAmps))
                result = CommandResult{false, Refusal::CurrentUnreadable, last.motorAmps, config.maxCurrentAmps};
            result.accepted = result.reason == Refusal::None;
            if (!result.accepted || blind.active)
                return result;
            blind = Blind{};
            blind.active = true;
            blind.limitMs = unknownMoveTimeoutMs(config);
            // Counted from the request: a restart in the middle runs no second one.
            blindRan = true;
            // A full dead time from the next step, whatever ran before: the
            // owner saves the done flag meanwhile (nothing drives yet).
            driveEndPending = true;
            deadTimeDone = false;
            current.state = State::Closing;
            current.target = End::Closed;
            current.fromUnknown = true;
            current.blind = true;
            current.blindReady = false;
            current.moveTimeoutMs = blind.limitMs;
            return result;
        }

        void Controller::stepBlind(const Readings &readings)
        {
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
            out.command = config.openDirection == Driver::Command::Forward ? Driver::Command::Reverse : Driver::Command::Forward;
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

        void Controller::endBlind(BlindEnd why, const Fault &fault)
        {
            blindLast.ended++;
            blindLast.why = why;
            blindLast.drivenMs = blind.driving ? last.nowMs - blind.driveStartMs : 0;
            blindLast.restVolts = blind.restVolts;
            blindLast.deepestSag = blind.deepestSag;
            blindLast.fault = fault;
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
            current.positionKnown = false;
            current.state = State::Error; // the Hall rail fault is still latched
        }
    } // namespace Roof
} // namespace SQM
