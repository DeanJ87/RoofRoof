// EmulatedRoof: spec 017's diagnostics recorder, stepped as RoofControl does
// (src/RoofControlDiag.cpp), and the status snapshot (RoofControl::snapshot).

#include "emulated_roof.h"

#include "SwitchWatchStep.h"

#include "MotionProfile.h"
#include "RoofAlerts.h"

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

    // Spec 024: as RoofControl::watchSwitches.
    void EmulatedRoof::watchSwitches()
    {
        const Roof::State state = controller.status().state;
        gates.stuckSwitch = RoofIo::stepSwitchWatch(
            watch,
            RoofIo::WatchStep{
                watchedState,
                state,
                calibration.active(),
                static_cast<uint8_t>(parkCfg.switches),
                parkCfg.switchesOnly(),
                gates.switch1,
                gates.switch2});
        watchedState = state;
    }

    RoofIo::Snapshot EmulatedRoof::snapshot() const
    {
        RoofIo::Snapshot s;
        s.status = controller.status();
        s.fault = fault;
        s.travelMm = roofCfg.travelMm;
        s.supplyVolts = plant.supplyNow();
        s.minSupplyVolts = roofCfg.minSupplyVolts;
        s.currentSense = roofCfg.currentSenseEnabled;
        s.motorAmps = motorAmps;
        s.maxCurrentAmps = roofCfg.maxCurrentAmps;
        s.fittedSwitches = gates.fittedSwitches;
        s.switch1 = gates.switch1;
        s.switch2 = gates.switch2;
        s.switchesOnly = parkCfg.switchesOnly();
        s.openings[0] = watch.quiet[0];
        s.openings[1] = watch.quiet[1];
        s.stuckSwitch = gates.stuckSwitch;
        s.personStops = personStops;
        s.calibrating = calibration.active();
        s.maxStepGapMs = STEP_MS;
        s.simulation = "demo";
        RoofIo::applyChecks(s, gates); // spec 008: the weather and park as the checks see them
        s.closePhase = CloseSeq::phaseName(sequence.phase());
        const uint64_t leftMs = sequence.standDownLeftMs(); // spec 026
        s.closeRearmSeconds = leftMs == CloseSeq::NOT_COUNTING ? -1 : static_cast<int32_t>((leftMs + 999) / 1000);
        s.daylight = CloseSeq::daylightStatus(daylightView);
        s.testBox = testBoxState;
        return s;
    }
} // namespace DemoRoof
