#include "CloseOnUnsafe.h"

#include "Logger.h"
#include "RoofControl.h"
#include "RoofText.h"
#include "SafetyInputs.h"
#include "SafetyPoller.h"

namespace SQM
{
    void CloseOnUnsafe::handle(SafetyInputs &safety, RoofControl &roof)
    {
        // One consistent copy of the roof: its state, a waiting close and a
        // person's stops come from the same moment (spec 003's lock).
        const RoofIo::Snapshot snap = roof.snapshot();
        CloseSeq::Inputs in;
        in.nowMs = SafetyPoller::nowMs();
        CloseSeq::applySafety(in, safety.weather().verdict, safety.park());
        CloseSeq::applyRoof(in, snap.status, snap.closePending);
        in.personStops = snap.personStops;
        sequence.setParkTimeoutMs(parkTimeoutMs.load());

        const CloseSeq::Decision decision = sequence.step(in);
        if (decision.action == CloseSeq::Action::None && decision.event == CloseSeq::Event::None && sequence.phase() == loggedPhase)
            return; // the usual pass: nothing to do

        CloseSeq::Reasons reasons;
        reasons.park = Safety::describe(safety.park());
        reasons.fault = RoofIo::describeFault(snap.fault);
        reasons.roof = snap.status.state;
        const std::string weather = Safety::describe(safety.weather());
        const CloseSeq::Decision answer = carryOut(decision, safety, roof, reasons.refusal);
        report(decision, reasons, weather);
        report(answer, reasons, weather);
        if (sequence.phase() != loggedPhase)
        {
            Logger::info(TAG, "Close sequence: %s -> %s", CloseSeq::phaseName(loggedPhase), CloseSeq::phaseName(sequence.phase()));
            loggedPhase = sequence.phase();
        }
    }

    CloseSeq::Decision CloseOnUnsafe::carryOut(
        const CloseSeq::Decision &decision, SafetyInputs &safety, RoofControl &roof, std::string &refusal)
    {
        switch (decision.action)
        {
        case CloseSeq::Action::RequestPark:
            Logger::info(TAG, "Asking N.I.N.A. to park the mount");
            safety.requestPark();
            break;
        case CloseSeq::Action::Close:
        {
            // Through spec 003's gates: the park at the start, the interlock throughout.
            const RoofControl::Outcome outcome = roof.command(Roof::Command::Close, Roof::Origin::Automatic);
            refusal = outcome.text;
            if (outcome.accepted)
            {
                Logger::info(TAG, "Closing the roof: the mount is parked");
                return sequence.closeAnswered(CloseSeq::CloseAnswer::Started);
            }
            const bool parkLost = outcome.reason == RoofIo::gateRefusalName(RoofIo::GateRefusal::MountNotParked);
            Logger::warn(TAG, "Close refused: %s", outcome.text.c_str());
            return sequence.closeAnswered(parkLost ? CloseSeq::CloseAnswer::ParkRefused : CloseSeq::CloseAnswer::Refused);
        }
        case CloseSeq::Action::Stop:
            roof.command(Roof::Command::Stop, Roof::Origin::Automatic);
            break;
        case CloseSeq::Action::None:
            break;
        }
        return CloseSeq::Decision{};
    }

    void CloseOnUnsafe::report(const CloseSeq::Decision &decision, const CloseSeq::Reasons &reasons, const std::string &weather)
    {
        if (decision.event == CloseSeq::Event::None)
            return;
        if (!CloseSeq::isCritical(decision.event))
        {
            if (decision.event == CloseSeq::Event::Started)
                Logger::info(TAG, "Close sequence started: %s", weather.c_str());
            else
                Logger::info(TAG, "Close sequence: %s", CloseSeq::eventName(decision.event));
            return;
        }
        const CloseSeq::AlertText text = CloseSeq::alertFor(decision, CloseSeq::reasonFor(decision, reasons));
        Logger::error(TAG, "%s: %s", text.title.c_str(), text.message.c_str());
        if (!decision.alert || !alertSink)
            return; // already alerted in this episode
        Alerts::Alert alert;
        alert.type = Alerts::AlertType::Roof;
        alert.level = Alerts::AlertLevel::Wake;
        alert.title = text.title;
        alert.message = text.message;
        alertSink(alert);
    }
} // namespace SQM
