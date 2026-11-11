#include <Arduino.h>

#include "Logger.h"
#include "MotionGuards.h"
#include "RoofControl.h"
#include "RoofLocked.h"
#include "RoofText.h"

// Spec 016: the attended calibration run on the board. Every decision is
// lib/CalibrationLogic's (Calib::Run) and lib/RoofLogic's (the leg); this only
// hands them the readings, the gates and the blockers under the roof's lock,
// right after the controller's tick, and applies the output.

namespace SQM
{
    RoofControl::Outcome RoofControl::startCalibration(const Calib::Request &request, bool sequenceIdle)
    {
        if (lock == nullptr || !started.load())
            return Outcome{false, "notStarted", "The roof controller didn't start: the motor stays off", Roof::State::Unknown};
        Outcome outcome;
        {
            RoofLocked held(lock);
            // Probed under the lock, like a command's (spec 010 FR-001).
            const RoofIo::Blockers now = blockers ? blockers() : RoofIo::Blockers();
            Calib::StartInputs in;
            in.nowMs = millis();
            in.limitMs = settings.roof.calibrationLimitSeconds * 1000;
            in.sequenceIdle = sequenceIdle;
            in.closePending = closePending;
            in.settingsPending = settingsPending;
            in.gates = currentGates();
            in.busy = RoofIo::blockerDecision(Roof::Command::Open, now).action != RoofIo::BlockerAction::Proceed;
            in.busyReason = RoofIo::blockerReason(now);
            in.busyText = RoofIo::blockerText(now);
            in.faultText = RoofIo::describeFault(fault);
            const Calib::Answer answer = calibration.start(controller, request, in);
            calibratingNow.store(calibration.active());
            refreshSnapshot();
            outcome = Outcome{answer.accepted, answer.reason, answer.text, controller.status().state};
        }
        if (outcome.accepted)
            Logger::info(TAG, "Calibration run started by a person (from %s)", Calib::whereName(request.at));
        else
            Logger::warn(TAG, "Calibration run refused: %s", outcome.text.c_str());
        return outcome;
    }

    // A person's stop: stopLocked ends any run.
    RoofControl::Outcome RoofControl::stopCalibration()
    {
        return command(Roof::Command::Stop, Roof::Origin::Person);
    }

    Calib::Report RoofControl::calibrationReport(uint32_t &nowMs) const
    {
        if (lock == nullptr)
        {
            nowMs = millis();
            return Calib::Report();
        }
        RoofLocked held(lock);
        nowMs = millis();
        return calibration.report();
    }

    // Under the lock: a stop (the roof is being stopped by the caller) or the
    // automatic close (its move is left to it) ends the run.
    void RoofControl::endCalibrationFor(bool yieldToClose, const std::string &text)
    {
        if (yieldToClose)
            calibration.yieldToClose();
        else
            calibration.stopped(text);
        calibratingNow.store(false);
        notice = "Calibration run ended: " + calibration.report().endingText;
    }

    // Under the lock, after the controller's tick.
    void RoofControl::stepCalibration(const Roof::Readings &readings)
    {
        if (!calibration.active())
            return;
        Calib::StepInputs in;
        in.readings = readings;
        in.gates = currentGates();
        // The blockers only when a leg is due (they read the network's state).
        if (calibration.wantsLeg(readings.nowMs) && blockers)
        {
            const RoofIo::Blockers now = blockers();
            in.busy = RoofIo::blockerDecision(Roof::Command::Open, now).action != RoofIo::BlockerAction::Proceed;
            in.busyText = RoofIo::blockerText(now);
        }
        in.faultText = RoofIo::describeFault(fault);
        calibration.step(controller, in);
        applyOutput();
        syncFault(RoofIo::FaultCause::Logic);
        calibratingNow.store(calibration.active());
        if (!calibration.active())
            notice = "Calibration run ended: " + calibration.report().endingText;
    }
} // namespace SQM
