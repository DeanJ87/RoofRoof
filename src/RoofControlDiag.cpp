#include "RoofControl.h"

#include "MotionProfile.h"
#include "MotorDriver.h"
#include "RoofLocked.h"

// Spec 017: the advanced diagnostics log's attach point, the step's sample and
// the readers' bounded copies (research R1, R2, R4, R7). Every access to
// `diagLog` is under the roof's lock.

namespace SQM
{
    void RoofControl::attachDiag(Diag::MoveLog *log)
    {
        if (lock == nullptr || log == nullptr)
            return;
        RoofLocked held(lock);
        diagLog = log;
        stepUsNow.store(0);
        maxStepUsNow.store(0);
        diagTimed.store(true);
    }

    Diag::MoveLog *RoofControl::detachDiag()
    {
        if (lock == nullptr)
            return nullptr;
        RoofLocked held(lock);
        Diag::MoveLog *log = diagLog;
        diagLog = nullptr;
        diagTimed.store(false);
        return log;
    }

    // Under the lock, at the end of the step: what the step has just done.
    void RoofControl::recordDiag(const StepInputs &in)
    {
        Diag::Sample sample;
        sample.nowMs = in.readings.nowMs;
        sample.status = controller.status();
        sample.output = controller.output();
        sample.supplyVolts = in.readings.supplyVolts;
        sample.motorAmps = settings.roof.currentSenseEnabled ? motorAmps : NAN;
        sample.origin = moveOrigin;
        const Roof::Settings &s = controller.settings();
        sample.slowDuty = Roof::toDuty(Roof::dutyFraction(s.slowDutyPercent));
        sample.topDuty = Roof::toDuty(Roof::dutyFraction(s.topDutyPercent));
        diagLog->step(sample);
    }

    // The control task only writes these.
    void RoofControl::noteStepTime(int64_t us)
    {
        const uint32_t value = us < 0 ? 0 : static_cast<uint32_t>(us);
        stepUsNow.store(value, std::memory_order_relaxed);
        if (value > maxStepUsNow.load(std::memory_order_relaxed))
            maxStepUsNow.store(value, std::memory_order_relaxed);
    }

    bool RoofControl::diagLive(Diag::RoofLive &out) const
    {
        if (lock == nullptr)
            return false;
        RoofLocked held(lock);
        out.status = controller.status();
        out.output = controller.output();
        out.pins = MotorDriver::logicalOutputs();
        out.settings = controller.settings();
        out.supplyVolts = supplyVolts;
        out.motorAmps = motorAmps;
        out.budgetUsedMs[0] = controller.budgetUsedMs(Roof::End::Closed);
        out.budgetUsedMs[1] = controller.budgetUsedMs(Roof::End::Open);
        out.watchdogTrips = watchdogTrips;
        out.maxGapMs = maxStepGapMs;
        out.maxBeatAgeMs = maxBeatAgeMs;
        if (diagLog == nullptr)
            return false;
        out.log = diagLog->live();
        return true;
    }

    bool RoofControl::diagMove(uint32_t id, Diag::Move &out) const
    {
        if (lock == nullptr)
            return false;
        RoofLocked held(lock);
        return diagLog != nullptr && diagLog->copy(id, out);
    }

    bool RoofControl::diagLatest(Diag::Summary &out) const
    {
        if (lock == nullptr)
            return false;
        RoofLocked held(lock);
        return diagLog != nullptr && diagLog->latest(out);
    }

    uint8_t RoofControl::diagIds(uint32_t (&out)[Diag::MAX_MOVES]) const
    {
        if (lock == nullptr)
            return 0;
        RoofLocked held(lock);
        return diagLog != nullptr ? diagLog->ids(out) : 0;
    }

    uint32_t RoofControl::diagFinished() const
    {
        if (lock == nullptr)
            return 0;
        RoofLocked held(lock);
        return diagLog != nullptr ? diagLog->finished() : 0;
    }

    RoofIo::HoldReason RoofControl::diagClear()
    {
        if (lock == nullptr)
            return RoofIo::HoldReason::None;
        RoofLocked held(lock);
        if (RoofIo::roofMoving(controller.status().state))
            return RoofIo::HoldReason::Moving;
        if (closePending)
            return RoofIo::HoldReason::CloseWaiting;
        if (diagLog != nullptr)
            diagLog->clear();
        return RoofIo::HoldReason::None;
    }
} // namespace SQM
