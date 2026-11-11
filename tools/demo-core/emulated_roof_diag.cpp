// EmulatedRoof: spec 017's diagnostics recorder, stepped as RoofControl does
// (src/RoofControlDiag.cpp).

#include "emulated_roof.h"

#include "MotionProfile.h"

namespace DemoRoof
{
    // RoofControl::recordDiag (spec 017).
    void EmulatedRoof::recordDiag(const Roof::Readings &in)
    {
        Diag::Sample sample;
        sample.nowMs = static_cast<uint32_t>(nowMs);
        sample.status = controller.status();
        sample.output = controller.output();
        sample.supplyVolts = in.supplyVolts;
        sample.motorAmps = roofCfg.currentSenseEnabled ? motorAmps : NAN;
        sample.origin = moveOrigin;
        const Roof::Settings &s = controller.settings();
        sample.slowDuty = Roof::toDuty(Roof::dutyFraction(s.slowDutyPercent));
        sample.topDuty = Roof::toDuty(Roof::dutyFraction(s.topDutyPercent));
        diagLog->step(sample);
    }

    Diag::RoofLive EmulatedRoof::diagLive() const
    {
        Diag::RoofLive live;
        live.status = controller.status();
        live.output = controller.output();
        live.pins = Driver::outputsFor(live.output.command, live.output.duty);
        live.settings = controller.settings();
        live.supplyVolts = plant.supplyNow();
        live.motorAmps = motorAmps;
        live.budgetUsedMs[0] = controller.budgetUsedMs(Roof::End::Closed);
        live.budgetUsedMs[1] = controller.budgetUsedMs(Roof::End::Open);
        if (diagLog != nullptr)
            live.log = diagLog->live();
        return live;
    }
} // namespace DemoRoof
