#include "CalibrationRun.h"

#include <algorithm>

#include "MotionGuards.h"
#include "RoofText.h"

// Spec 016: the legs of a run, one control step at a time (research R3, R4).

namespace SQM
{
    namespace Calib
    {
        namespace
        {
            bool onMagnet(Roof::HallLevel level)
            {
                return level == Roof::HallLevel::High || level == Roof::HallLevel::Low;
            }

            Roof::End targetOf(LegKind kind)
            {
                return kind == LegKind::SlowOpen || kind == LegKind::TopOpen ? Roof::End::Open : Roof::End::Closed;
            }

            uint32_t boundedBy(uint32_t limitMs, uint32_t measuredMs, double factor)
            {
                const double ms = measuredMs * factor + LEG_EXTRA_MS;
                const uint32_t bound = ms >= limitMs ? limitMs : static_cast<uint32_t>(ms);
                return std::max(bound, Roof::MIN_CALIBRATION_LEG_MS);
            }

            std::string legText(LegKind kind)
            {
                static const char *const TEXTS[] = {"first close", "slow open", "slow close", "top-duty open", "top-duty close"};
                const unsigned index = static_cast<unsigned>(kind);
                return index < 5 ? TEXTS[index] : "leg";
            }
        } // namespace

        const MeasuredLeg *Run::measuredLeg(LegKind kind) const
        {
            for (uint8_t i = 0; i < rep.measured.legCount; ++i)
                if (rep.measured.legs[i].kind == kind)
                    return &rep.measured.legs[i];
            return nullptr;
        }

        Roof::CalibrationLeg Run::legFor(LegKind kind) const
        {
            Roof::CalibrationLeg leg;
            leg.target = targetOf(kind);
            leg.dutyPercent = rep.old.slowDutyPercent;
            leg.limitMs = rep.limitMs;
            const MeasuredLeg *slowOpen = measuredLeg(LegKind::SlowOpen);
            const MeasuredLeg *slowClose = measuredLeg(LegKind::SlowClose);
            const Roof::HallLevel closed = rep.measured.closedLevel;
            const Roof::HallLevel open = rep.measured.openLevel;
            if (kind == LegKind::SlowClose && slowOpen != nullptr)
            {
                leg.limitMs = boundedBy(rep.limitMs, slowOpen->drivenMs, SLOW_CLOSE_FACTOR);
                leg.expect = closed;
            }
            const MeasuredLeg *slow = kind == LegKind::TopOpen ? slowOpen : slowClose;
            if ((kind == LegKind::TopOpen || kind == LegKind::TopClose) && slow != nullptr)
            {
                leg.dutyPercent = rep.old.topDutyPercent;
                leg.limitMs = boundedBy(rep.limitMs, slow->drivenMs, TOP_LEG_FACTOR);
                const double atTopMs = static_cast<double>(slow->drivenMs) * rep.old.slowDutyPercent / rep.old.topDutyPercent;
                leg.slowAfterMs = std::max<uint32_t>(1, static_cast<uint32_t>(atTopMs * SLOW_AFTER_FRACTION));
                leg.expect = kind == LegKind::TopOpen ? open : closed;
            }
            return leg;
        }

        bool Run::wantsLeg(uint32_t nowMs) const
        {
            return active() && rep.phase != Phase::Drive && nowMs - rep.phaseStartMs >= SETTLE_MS && rep.legIndex < legCount;
        }

        void Run::step(Roof::Controller &controller, const StepInputs &in)
        {
            if (!active())
                return;
            rep.nowMs = in.readings.nowMs;
            if (controller.status().fault.kind != Roof::FaultKind::None)
                return finish(Ending::Fault, "Fault on the " + legText(rep.leg) + ": " + in.faultText);
            if (rep.phase == Phase::Drive)
                stepDrive(controller, in);
            else
                stepPause(controller, in);
        }

        void Run::sample(const Roof::Status &status, const StepInputs &in)
        {
            if (!status.driving)
                return;
            samples.add(status.moveElapsedMs, in.readings.hallCounts);
            electrical.addSupply(in.readings.supplyVolts);
            // The start-up surge isn't the running current (spec 003's settle time).
            if (rep.old.currentSenseEnabled && status.moveElapsedMs >= RoofIo::CURRENT_SETTLE_MS)
                electrical.addAmps(in.readings.motorAmps);
        }

        // A leg under way: its readings, the gates, and how it ended.
        void Run::stepDrive(Roof::Controller &controller, const StepInputs &in)
        {
            sample(controller.status(), in);
            const bool opening = targetOf(rep.leg) == Roof::End::Open;
            const RoofIo::GateRefusal open = RoofIo::openRefusal(in.gates);
            if (open != RoofIo::GateRefusal::None && opening)
            {
                controller.command(Roof::Command::Stop, Roof::Origin::Automatic);
                return finish(Ending::Weather, RoofIo::describeGate(open, in.gates));
            }
            if (open != RoofIo::GateRefusal::None && endAfterLeg == Ending::None)
            {
                // A closing leg finishes its close (constitution III), then the run ends.
                endAfterLeg = Ending::Weather;
                endAfterText = RoofIo::describeGate(open, in.gates);
            }
            if (opening && !RoofIo::parkHolds(in.gates))
            {
                controller.command(Roof::Command::Stop, Roof::Origin::Automatic);
                return finish(Ending::Park, RoofIo::describeGate(RoofIo::GateRefusal::MountNotParked, in.gates));
            }
            const Roof::CalibrationReport &report = controller.calibrationReport();
            if (report.ended == legsEnded)
                return;
            legsEnded = report.ended;
            if (!report.arrived)
                return finish(Ending::Stopped, "The " + legText(rep.leg) + " was stopped");
            legEnded(report, in.readings.nowMs);
        }

        void Run::legEnded(const Roof::CalibrationReport &report, uint32_t nowMs)
        {
            Measurements &m = rep.measured;
            if (rep.leg == LegKind::ToClosed)
                m.closedLevel = report.level;
            else if (m.legCount < MAX_MEASURED_LEGS)
            {
                MeasuredLeg &leg = m.legs[m.legCount++];
                leg.kind = rep.leg;
                leg.target = report.target;
                leg.dutyPercent =
                    rep.leg == LegKind::TopOpen || rep.leg == LegKind::TopClose ? rep.old.topDutyPercent : rep.old.slowDutyPercent;
                leg.drivenMs = report.drivenMs;
                leg.dutyMs = report.dutyMs;
                leg.level = report.level;
                leg.electrical = electrical;
                m.between.merge(samples.middle(report.drivenMs));
            }
            // Both ends on one level: no top-duty pass (its end couldn't be told), back to closed only.
            if (rep.leg == LegKind::SlowOpen)
            {
                m.openLevel = report.level;
                if (m.openLevel == m.closedLevel)
                    legCount = std::min<uint8_t>(legCount, rep.legIndex + 2);
            }
            restAtEnd = onMagnet(report.level);
            restEnd = report.target;
            if (endAfterLeg != Ending::None)
                return finish(endAfterLeg, endAfterText);
            rep.legIndex++;
            pause(nowMs);
        }

        // At an end: the readings at rest, the gates, and the next leg once the pause is over.
        void Run::stepPause(Roof::Controller &controller, const StepInputs &in)
        {
            if (restAtEnd)
                (restEnd == Roof::End::Closed ? rep.measured.closedRest : rep.measured.openRest).add(in.readings.hallCounts);
            const RoofIo::GateRefusal open = RoofIo::openRefusal(in.gates);
            if (open != RoofIo::GateRefusal::None)
                return finish(Ending::Weather, RoofIo::describeGate(open, in.gates));
            if (!RoofIo::parkHolds(in.gates))
                return finish(Ending::Park, RoofIo::describeGate(RoofIo::GateRefusal::MountNotParked, in.gates));
            const uint32_t nowMs = in.readings.nowMs;
            if (nowMs - rep.phaseStartMs < SETTLE_MS)
                return;
            if (rep.legIndex >= legCount)
                return finishDone();
            if (!in.busy)
                return startLeg(controller, in.gates, nowMs);
            if (rep.phase != Phase::Wait)
            {
                rep.phase = Phase::Wait;
                waitStartMs = nowMs;
            }
            if (nowMs - waitStartMs >= LEG_WAIT_MS)
                finish(Ending::Busy, in.busyText + ": the " + legText(legs[rep.legIndex]) + " couldn't start within 15 s");
        }

        void Run::startLeg(Roof::Controller &controller, const RoofIo::GateInputs &gates, uint32_t nowMs)
        {
            const LegKind kind = legs[rep.legIndex];
            const Roof::CalibrationLeg leg = legFor(kind);
            // The same gates as a person's open or close (constitution II, III).
            const Roof::Command command = leg.target == Roof::End::Open ? Roof::Command::Open : Roof::Command::Close;
            const RoofIo::GateRefusal gate = RoofIo::gateCommand(command, gates);
            if (gate != RoofIo::GateRefusal::None)
                return finish(Ending::Refused, "The " + legText(kind) + " was refused: " + RoofIo::describeGate(gate, gates));
            const Roof::CommandResult result = controller.calibrate(leg, Roof::Origin::Person);
            if (!result.accepted)
                return finish(Ending::Refused, "The " + legText(kind) + " was refused: " + RoofIo::describeRefusal(result));
            rep.leg = kind;
            rep.phase = Phase::Drive;
            rep.phaseStartMs = nowMs;
            rep.legLimitMs = leg.limitMs;
            legsEnded = controller.calibrationReport().ended;
            samples.clear();
            electrical = Electrical();
        }

        void Run::pause(uint32_t nowMs)
        {
            rep.phase = Phase::Settle;
            rep.phaseStartMs = nowMs;
            rep.legLimitMs = 0;
            if (rep.legIndex < legCount)
                rep.leg = legs[rep.legIndex];
        }
    } // namespace Calib
} // namespace SQM
