#pragma once

// Spec 016: the calibration run's test rig: a controller, the simulated roof
// and a run, stepped every 20 ms in RoofControl::stepLocked's order (the
// gates, the tick, the run). Shared by test_calibration_run and
// test_calibration_document.

#include <string>
#include <unity.h>

#include "CalibrationRun.h"
#include "RoofController.h"
#include "RoofGates.h"
#include "RoofPlant.h"
#include "RoofText.h"

namespace CalibTest
{
    using namespace SQM;
    using namespace SQM::Calib;
    using Roof::FaultKind;

    constexpr uint32_t STEP_MS = 20;
    constexpr uint32_t LIMIT_MS = 180000;

    struct Rig
    {
        Roof::Settings settings;
        Roof::Controller roof;
        RoofPlant::Plant plant;
        Run run;
        RoofIo::GateInputs gates;
        bool busy = false;
        uint32_t nowMs = 1000;

        explicit Rig(float startMm = 0.0f, const Roof::Settings &s = Roof::Settings())
            : settings(s),
              roof(s)
        {
            plant.travelMm = static_cast<float>(s.travelMm);
            plant.positionMm = startMm;
            gates.weather = Safety::Verdict::Safe;
            gates.weatherText = "safe";
            gates.parked = true;
            gates.parkText = "parked";
            for (int i = 0; i < 30; ++i)
                step();
        }

        // RoofControl::stepLocked's order: the gates, the tick, the run.
        void step()
        {
            plant.drive(roof.output(), STEP_MS);
            nowMs += STEP_MS;
            const Roof::Status &st = roof.status();
            if (RoofIo::interlockLost(st, gates))
                roof.raiseFault(Roof::Fault{FaultKind::External, 1.0f, 0.0f});
            if (RoofIo::openMustStop(roof.status(), gates))
                roof.command(Roof::Command::Stop, Roof::Origin::Automatic);
            const Roof::Readings r = plant.readings(nowMs);
            roof.tick(r);
            StepInputs in;
            in.readings = r;
            in.gates = gates;
            in.busy = busy && run.wantsLeg(nowMs);
            in.busyText = "MQTT is connecting";
            in.faultText = RoofIo::describeFault(RoofIo::FaultRecord{roof.status().fault, RoofIo::FaultCause::Logic});
            run.step(roof, in);
        }

        StartInputs startInputs() const
        {
            StartInputs in;
            in.nowMs = nowMs;
            in.limitMs = LIMIT_MS;
            in.gates = gates;
            in.busy = busy;
            in.busyReason = "busy";
            in.busyText = "MQTT is connecting";
            return in;
        }

        Answer start(Where at = Where::Closed, bool topPass = false, uint32_t travelMm = 1800)
        {
            Request req;
            req.attended = true;
            req.at = at;
            req.travelMm = travelMm;
            req.topPass = topPass;
            return run.start(roof, req, startInputs());
        }

        // Steps until the run ends or `maxMs` passes.
        bool finish(uint32_t maxMs = 900000)
        {
            for (uint32_t t = 0; t < maxMs; t += STEP_MS)
            {
                step();
                if (!run.active())
                    return true;
            }
            return false;
        }

        // Steps until the run is driving leg `kind`.
        bool until(LegKind kind, Phase phase = Phase::Drive, uint32_t maxMs = 600000)
        {
            for (uint32_t t = 0; t < maxMs; t += STEP_MS)
            {
                step();
                if (run.report().leg == kind && run.report().phase == phase)
                    return true;
                if (!run.active())
                    return false;
            }
            return false;
        }

        void steps(uint32_t ms)
        {
            for (uint32_t t = 0; t < ms; t += STEP_MS)
                step();
        }

        const Report &rep() const { return run.report(); }
    };

    inline void assertRefused(const char *reason, const Answer &answer)
    {
        TEST_ASSERT_FALSE(answer.accepted);
        TEST_ASSERT_EQUAL_STRING(reason, answer.reason.c_str());
        TEST_ASSERT_FALSE(answer.text.empty());
    }

    inline bool sameSettings(const Roof::Settings &a, const Roof::Settings &b)
    {
        return a.travelMm == b.travelMm && a.speedMmPerSecond == b.speedMmPerSecond && a.timeoutMarginMs == b.timeoutMarginMs &&
               a.hallHighEnd == b.hallHighEnd && a.hallHighCounts == b.hallHighCounts && a.hallLowCounts == b.hallLowCounts;
    }
} // namespace CalibTest
