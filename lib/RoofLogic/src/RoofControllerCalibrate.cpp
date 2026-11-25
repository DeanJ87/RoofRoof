#include "RoofController.h"

#include <algorithm>

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
            if (move.active || blind.active)
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

        // Spec 019: a jog is a slow leg toward `target` that stops at any magnet
        // after leaving its start one, bounded by jogLimitMs() (research R1).
        CalibrationLeg Controller::jogLeg(End target) const
        {
            CalibrationLeg leg;
            leg.target = target;
            leg.dutyPercent = config.slowDutyPercent;
            leg.limitMs = jogLimitMs();
            return leg;
        }

        uint32_t Controller::jogLimitMs() const
        {
            return std::min(JOG_MAX_MS, unknownMoveTimeoutMs(config));
        }

        // Spec 033 (constitution 9.0.0, I): the Hall sensor can't place the
        // roof (its rail fault latched and no other, or a reading at a rail or
        // not settled): a person's jog then runs blind.
        bool Controller::hallBlind() const
        {
            if (current.fault.kind != FaultKind::None)
                return current.fault.kind == FaultKind::HallRail;
            return current.hall == HallLevel::Unsettled || current.hall == HallLevel::Rail;
        }

        // Spec 033: `ignoreMagnets` (a person's override) runs it blind too.
        CommandResult Controller::checkJog(End target, Origin origin, bool ignoreMagnets) const
        {
            if (origin != Origin::Person || !(ignoreMagnets || hallBlind()))
                return checkCalibrate(jogLeg(target), origin);
            if (current.fault.kind != FaultKind::None && current.fault.kind != FaultKind::HallRail)
                return refusedLeg(Refusal::FaultLatched);
            if (move.active || blind.active)
                return refusedLeg(Refusal::Moving);
            return blindReadiness();
        }

        // A jog never keeps an estimate: stopped part-way, the position is
        // unknown; at a magnet it is that magnet's end. Spec 033: a blind jog
        // runs as the blind close does, toward `target`, with the jog's limit
        // and its keep-alive; the Hall fault stays latched.
        CommandResult Controller::jog(End target, Origin origin, bool ignoreMagnets)
        {
            const bool blindJog = origin == Origin::Person && (ignoreMagnets || hallBlind());
            const CommandResult answer = blindJog ? checkJog(target, origin, ignoreMagnets) : calibrate(jogLeg(target), origin);
            if (!answer.accepted)
                return answer;
            if (blindJog)
            {
                startBlind(target, jogLimitMs());
                blind.jog = true;
                // The magnets read at rest afterwards aren't believed until a
                // move reaches one (or new settings): one may have shifted.
                doubtMagnets = doubtMagnets || ignoreMagnets;
            }
            else
            {
                move.jog = true;
                move.fromUnknown = true;
            }
            jogHoldPending = true; // the first step stamps the deadline
            current.fromUnknown = true;
            current.positionKnown = false; // until it stops at a magnet
            current.jogging = true;
            ++jogCount;
            return CommandResult{};
        }

        bool Controller::jogRunning(uint32_t id) const
        {
            return ((move.active && move.jog) || (blind.active && blind.jog)) && id == jogCount;
        }

        bool Controller::holdJog(uint32_t id)
        {
            if (!jogRunning(id))
                return false;
            jogHoldPending = true;
            return true;
        }

        bool Controller::releaseJog(uint32_t id)
        {
            if (!jogRunning(id))
                return false;
            if (blind.active)
                endBlind(BlindEnd::Stopped);
            else
                stopMove(true);
            return true;
        }

        // The deadline is taken from the step's clock, never a caller's: a
        // delayed renewal can only end a jog sooner (constitution I).
        bool Controller::jogExpired(uint32_t nowMs)
        {
            if (jogHoldPending)
            {
                jogHoldUntilMs = nowMs + JOG_HOLD_MS;
                jogHoldPending = false;
            }
            return static_cast<int32_t>(nowMs - jogHoldUntilMs) >= 0;
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
        // from the travel and speed the run is measuring (research R3). A jog's
        // is every move's, counted across jogs (spec 019).
        uint32_t Controller::budgetLimitMs() const
        {
            return move.calibration && !move.jog ? move.leg.limitMs : driveBudgetMs(config);
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
            current.jogging = false;
        }
    } // namespace Roof
} // namespace SQM
