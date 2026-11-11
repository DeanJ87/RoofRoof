#include "RoofController.h"

#include "MotionProfile.h"

// Spec 016: a calibration leg (research R1). It is a move like any other:
// stepped by tick(), with the dead time, the latch and every fault check. It
// differs only in its timeout and drive budget (the leg's own limit), its duty
// (legFraction) and its end (the first magnet after leaving the start one).

namespace SQM
{
    namespace Roof
    {
        namespace
        {
            CommandResult refusedLeg(Refusal reason)
            {
                CommandResult result;
                result.accepted = false;
                result.reason = reason;
                return result;
            }

            bool legValid(const Settings &settings, const CalibrationLeg &leg)
            {
                if (leg.target != End::Closed && leg.target != End::Open)
                    return false;
                if (leg.limitMs < MIN_CALIBRATION_LEG_MS || leg.limitMs > MAX_CALIBRATION_LEG_MS)
                    return false;
                if (leg.dutyPercent < settings.slowDutyPercent || leg.dutyPercent > settings.topDutyPercent)
                    return false;
                // Faster than the slow duty only with a point to slow down at.
                if (leg.dutyPercent > settings.slowDutyPercent && leg.slowAfterMs == 0)
                    return false;
                return leg.expect == HallLevel::Unsettled || leg.expect == HallLevel::Low || leg.expect == HallLevel::High;
            }
        } // namespace

        CommandResult Controller::checkCalibrate(const CalibrationLeg &leg, Origin origin) const
        {
            if (origin != Origin::Person)
                return refusedLeg(Refusal::NotAPerson);
            if (current.fault.kind != FaultKind::None)
                return refusedLeg(Refusal::FaultLatched);
            if (move.active)
                return refusedLeg(Refusal::Moving);
            const CommandResult notReady = readinessRefusal();
            if (!notReady.accepted)
                return notReady;
            if (!legValid(config, leg))
                return refusedLeg(Refusal::LegInvalid);
            // constitution I: faster than the slow duty only from a magnet.
            const bool onMagnet = current.hall == HallLevel::High || current.hall == HallLevel::Low;
            if (leg.dutyPercent > config.slowDutyPercent && !onMagnet)
                return refusedLeg(Refusal::PositionUnknown);
            return CommandResult{};
        }

        CommandResult Controller::calibrate(const CalibrationLeg &leg, Origin origin)
        {
            const CommandResult answer = checkCalibrate(leg, origin);
            if (!answer.accepted)
                return answer;
            move = Move{};
            move.active = true;
            move.target = leg.target;
            move.fromUnknown = !current.positionKnown;
            move.calibration = true;
            move.leg = leg;
            current.target = leg.target;
            current.state = leg.target == End::Open ? State::Opening : State::Closing;
            current.fromUnknown = move.fromUnknown;
            current.moveTimeoutMs = 0;
            current.calibrating = true;
            return CommandResult{};
        }

        // At the drive's start (after the dead time). The estimate is kept only
        // when the saved polarity agrees with the direction (the leg leaves the
        // other end's magnet): otherwise a stop part-way leaves the position
        // unknown, never a confident wrong one. Faster than the slow duty only
        // still on a magnet (constitution I), else capped at it.
        void Controller::startLeg(bool leavesOtherEnd, bool onMagnet)
        {
            move.timeoutMs = move.leg.limitMs;
            if (!leavesOtherEnd)
            {
                move.fromUnknown = true;
                current.fromUnknown = true;
            }
            if (!onMagnet && move.leg.dutyPercent > config.slowDutyPercent)
                move.leg.dutyPercent = config.slowDutyPercent;
        }

        // The leg's end: any magnet once the start magnet is left, or the
        // expected level only (the other is the wrong magnet, constitution I).
        bool Controller::checkLegMagnets()
        {
            const bool onMagnet = current.hall == HallLevel::High || current.hall == HallLevel::Low;
            if (!onMagnet)
            {
                if (current.hall == HallLevel::Between)
                    move.leftStart = true;
                return false;
            }
            if (move.startedAtMagnet && !move.leftStart)
                return false;
            if (move.leg.expect != HallLevel::Unsettled && current.hall != move.leg.expect)
            {
                // Measured: the other end than the target (the magnet read is the other end's).
                const End other = move.target == End::Open ? End::Closed : End::Open;
                latch(Fault{FaultKind::WrongMagnet, static_cast<float>(other), static_cast<float>(move.target)});
                return true;
            }
            arrive();
            return true;
        }

        // A calibration leg's budget is its own limit: the settings' budget comes
        // from the travel and speed the run is measuring (research R3).
        uint32_t Controller::budgetLimitMs() const
        {
            return move.calibration ? move.leg.limitMs : driveBudgetMs(config);
        }

        void Controller::endLeg(bool arrived)
        {
            if (!move.calibration)
                return;
            legReport.ended++;
            legReport.arrived = arrived;
            legReport.target = move.target;
            legReport.startLevel = move.startLevel;
            legReport.level = arrived ? current.hall : HallLevel::Unsettled;
            legReport.drivenMs = move.driving ? last.nowMs - move.driveStartMs : 0;
            legReport.dutyMs = move.dutyMs;
            current.calibrating = false;
        }
    } // namespace Roof
} // namespace SQM
