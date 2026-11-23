#include <Arduino.h>

#include "Logger.h"
#include "MotionGuards.h"
#include "RoofControl.h"
#include "RoofLocked.h"
#include "RoofText.h"

// Spec 019: jog and teach the ends on the board. Every decision is
// lib/CalibrationLogic's (Calib::Teach) and lib/RoofLogic's (the jog and its
// keep-alive deadline, kept by the control step); this only hands them the
// gates and the blockers under the roof's lock and applies the output.

namespace SQM
{
    RoofControl::Outcome RoofControl::jog(
        const Calib::JogRequest &request, const std::function<bool()> &sequenceIdle, uint32_t &id, uint32_t &limitMs)
    {
        if (lock == nullptr || !started.load())
            return notStarted();
        Outcome outcome;
        {
            RoofLocked held(lock);
            Calib::Answer answer;
            if (request.id != 0 && request.release)
                answer = Calib::Teach::release(controller, request.id);
            else if (request.id != 0)
                answer = Calib::Teach::hold(controller, request.id);
            else
            {
                // Probed under the lock, like a command's (spec 010 FR-001).
                const RoofIo::Blockers now = blockers ? blockers() : RoofIo::Blockers();
                Calib::StartInputs in;
                in.nowMs = millis();
                in.sequenceIdle = sequenceIdle();
                in.closePending = closePending;
                in.settingsPending = settingsPending;
                in.gates = currentGates();
                in.busy = RoofIo::blockerDecision(Roof::Command::Open, now).action != RoofIo::BlockerAction::Proceed;
                in.busyReason = RoofIo::blockerReason(now);
                in.busyText = RoofIo::blockerText(now);
                in.faultText = RoofIo::describeFault(fault);
                answer = teach.jog(controller, request.target, request.attended, calibration.active(), in);
                if (answer.accepted)
                    moveOrigin = Roof::Origin::Person;
            }
            applyOutput();
            refreshSnapshot();
            id = controller.jogId();
            limitMs = controller.jogLimitMs();
            outcome = Outcome{answer.accepted, answer.reason, answer.text, controller.status().state};
        }
        if (request.id == 0)
        {
            if (outcome.accepted)
                Logger::info(TAG, "Jog toward %s started by a person", Roof::endName(request.target));
            else
                Logger::warn(TAG, "Jog refused: %s", outcome.text.c_str());
        }
        return outcome;
    }

    RoofControl::Outcome RoofControl::teachMark(Calib::Where where)
    {
        if (lock == nullptr)
            return Outcome{false, "notStarted", "The roof controller didn't start", Roof::State::Unknown};
        RoofLocked held(lock);
        const Calib::Answer answer = teach.mark(where, controller.status(), calibration.active());
        return Outcome{answer.accepted, answer.reason, answer.text, controller.status().state};
    }

    RoofControl::Outcome RoofControl::teachConfirmable(const Calib::TeachValues &values) const
    {
        if (lock == nullptr)
            return Outcome{false, "notStarted", "The roof controller didn't start", Roof::State::Unknown};
        RoofLocked held(lock);
        const Calib::Answer answer = teach.checkConfirm(values.highEnd, values.highCounts, values.lowCounts);
        return Outcome{answer.accepted, answer.reason, answer.text, controller.status().state};
    }

    void RoofControl::teachClear()
    {
        if (lock == nullptr)
            return;
        RoofLocked held(lock);
        teach.clear();
    }

    Calib::TeachReport RoofControl::teachReport() const
    {
        if (lock == nullptr)
            return Calib::TeachReport();
        RoofLocked held(lock);
        return teach.report(controller);
    }
} // namespace SQM
