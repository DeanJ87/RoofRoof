// The demo roof's jog and teach (spec 019): RoofControl's (src/RoofTeach.cpp)
// glue around the same library. The demo has no blockers.

#include "emulated_roof.h"

#include "RoofText.h"

namespace DemoRoof
{
    Outcome EmulatedRoof::jog(const Calib::JogRequest &request, uint32_t &id, uint32_t &limitMs)
    {
        Calib::Answer answer;
        if (request.id != 0 && request.release)
            answer = Calib::Teach::release(controller, request.id);
        else if (request.id != 0)
            answer = Calib::Teach::hold(controller, request.id);
        else
        {
            Calib::StartInputs in;
            in.nowMs = static_cast<uint32_t>(nowMs);
            in.sequenceIdle = CloseSeq::platformState(sequence.phase()) == RoofIo::SequenceState::Idle;
            in.settingsPending = settingsPending;
            in.gates = gates;
            in.faultText = RoofIo::describeFault(fault);
            answer = teach.jog(controller, request.target, request.attended, calibration.active(), in);
            if (answer.accepted)
                moveOrigin = Roof::Origin::Person;
        }
        id = controller.jogId();
        limitMs = controller.jogLimitMs();
        return Outcome{answer.accepted, answer.reason, answer.text, controller.status().state};
    }

    Outcome EmulatedRoof::teachMark(Calib::Where where)
    {
        const Calib::Answer answer = teach.mark(where, controller.status(), calibration.active());
        return Outcome{answer.accepted, answer.reason, answer.text, controller.status().state};
    }

    Outcome EmulatedRoof::teachConfirmable(const Calib::TeachValues &values) const
    {
        const Calib::Answer answer = teach.checkConfirm(values.highEnd, values.highCounts, values.lowCounts);
        return Outcome{answer.accepted, answer.reason, answer.text, controller.status().state};
    }
} // namespace DemoRoof
