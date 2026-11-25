// EmulatedRoof: the roof's commands (see emulated_roof.h). RoofControl's
// checkNow, commandLocked and stopLocked (src/RoofControl.cpp) and, spec 027
// (constitution 7.0.0), src/RoofControlGuard.cpp's admitLocked and stepGuard:
// an Open or Close refused only for the park waits for it, through the
// firmware's own RoofIo::MoveGuard. The demo has no blockers (no upload or
// MQTT connect to wait for), so nothing here defers a close.

#include "emulated_roof.h"

#include "CommandCheck.h"
#include "DomeBridge.h"
#include "RoofAlerts.h"
#include "RoofText.h"

namespace DemoRoof
{
    Outcome EmulatedRoof::outcomeOf(const Roof::CommandResult &result) const
    {
        Outcome outcome;
        outcome.accepted = result.accepted;
        if (!result.accepted)
        {
            outcome.reason = Roof::refusalName(result.reason);
            outcome.text = RoofIo::describeRefusal(result);
        }
        outcome.state = controller.status().state;
        return outcome;
    }

    // RoofControl::checkNow: the gates, the client rule and the motion logic's
    // own checks, with a person's overrides and (spec 027) the park set aside.
    RoofIo::CommandCheck EmulatedRoof::checkNow(Roof::Command command, Roof::Origin origin, uint8_t overrides, bool parkAside) const
    {
        RoofIo::CommandContext context;
        context.overrides = overrides;
        context.parkAside = parkAside;
        context.remoteStopsClose = remoteStopsClose;
        context.calibrating = calibration.active();
        return RoofIo::checkCommand(command, origin, gates, controller, context);
    }

    // RoofControl's commandLocked and stopLocked: the checks, then the command.
    Outcome EmulatedRoof::runChecked(Roof::Command command, Roof::Origin origin, uint8_t overrides)
    {
        const RoofIo::CommandCheck check = checkNow(command, origin, overrides, false);
        if (!check.accepted)
            return Outcome{false, check.reason, check.text, controller.status().state};
        const bool stopping = command == Roof::Command::Stop || command == Roof::Command::Abort;
        if (stopping)
            guard.stopped(); // spec 027: a stop ends a wait for the park
        // The close sequence stands down on a person's stop (spec 005), and on a
        // remote stop the owner's autoClose.remoteStop counts as one (spec 014).
        if (stopping && (RoofIo::countsAsPersonStop(command, origin) || check.standsDown))
            personStops++;
        if (stopping && check.standsDown)
            remoteStops++;
        if (stopping)
            calibration.stopped(origin == Roof::Origin::Person ? "Stopped by a person" : "Stopped by a stop command");
        const Outcome outcome = outcomeOf(controller.command(command, origin));
        if (outcome.accepted && !stopping)
        {
            moveOrigin = origin;
            guard.ran(command, RoofIo::allowedOverrides(command, origin, overrides), gates); // spec 027
        }
        syncFault();
        return outcome;
    }

    Outcome EmulatedRoof::command(Roof::Command command, Roof::Origin origin, uint8_t overrides)
    {
        const bool move = command == Roof::Command::Open || command == Roof::Command::Close;
        return move && origin != Roof::Origin::Automatic ? admit(command, origin, overrides) : runChecked(command, origin);
    }

    // RoofControl::admitLocked: an Open or Close from a person, a client or
    // MQTT runs, waits for the park (accepted, pending) or is refused.
    Outcome EmulatedRoof::admit(Roof::Command command, Roof::Origin origin, uint8_t overrides)
    {
        const uint8_t allowed = RoofIo::allowedOverrides(command, origin, overrides);
        const RoofIo::CommandCheck check = checkNow(command, origin, allowed, false);
        const RoofIo::CommandCheck aside = checkNow(command, origin, allowed, true);
        const RoofIo::Admission admission = guard.admit({command, origin, allowed}, check, aside, static_cast<uint32_t>(nowMs));
        const Roof::State state = controller.status().state;
        switch (admission.action)
        {
        case RoofIo::Admit::Run:
            return runChecked(command, origin, allowed);
        case RoofIo::Admit::Wait:
            return Outcome{true, "", admission.text, state, true};
        default:
            return Outcome{false, admission.reason, admission.text, state};
        }
    }

    // RoofControl::stepGuard, each control step, with main.cpp handleSafety's
    // part: a park request goes to the simulated N.I.N.A. and a wait that gave
    // up raises the critical "mount not parked" alert (CloseOnUnsafe::parkWaitFailed).
    void EmulatedRoof::stepGuard()
    {
        const RoofIo::ParkWait &wait = guard.wait();
        if (wait.waiting() && controller.atEnd(wait.command()))
            guard.stopped(); // the roof reached that end some other way: the wait ends quietly
        RoofIo::CommandCheck waitCheck;
        if (wait.waiting())
            waitCheck = checkNow(wait.command(), wait.origin(), wait.overrides(), true);
        const Roof::Command command = wait.command();
        const RoofIo::GuardStep step = guard.step(controller.status(), false, gates, waitCheck, static_cast<uint32_t>(nowMs));
        if (step.requestPark && !parkCfg.ninaUrl.empty())
            parkMount();
        if (step.gaveUp)
            critical.push_back(CloseSeq::parkWaitAlert(wait.endText()));
        if (!step.run)
            return;
        const Outcome outcome = runChecked(command, wait.origin(), wait.overrides());
        if (!outcome.accepted)
            guard.runRefused(outcome.text);
    }

    // RoofControlGuard's snapshot: an Open override or a confirmed close in effect.
    uint8_t EmulatedRoof::overrideBits() const
    {
        return static_cast<uint8_t>(guard.openOverride() | (guard.closeParkOverride() ? RoofIo::OverridePark : 0));
    }

    // RoofControl::automaticClose: withdrawn if a person stopped the roof since the decision.
    Outcome EmulatedRoof::automaticClose(uint32_t personStopsSeen)
    {
        if (personStops != personStopsSeen)
            return Outcome{
                false,
                CloseSeq::PERSON_STOP_REASON,
                "A person stopped the roof: the automatic close is withdrawn",
                controller.status().state};
        guard.closeWins();                      // spec 027: a waiting Open is called off
        calibration.yieldToClose(controller);   // spec 016: the automatic close always wins
        Calib::Teach::yieldToClose(controller); // spec 019: a jog is stopped first
        return runChecked(Roof::Command::Close, Roof::Origin::Automatic);
    }

    Outcome EmulatedRoof::clearFault()
    {
        const Outcome outcome = outcomeOf(controller.clearFault(Roof::Origin::Person));
        syncFault();
        return outcome;
    }

    // WebServer's domeStatus: spec 027's wait reads opening or closing.
    Alpaca::DomeStatus EmulatedRoof::domeStatus() const
    {
        return RoofIo::domeStatus(controller.status(), false, true, RoofIo::waitView(guard.wait(), static_cast<uint32_t>(nowMs)));
    }

    // WebServer::AlpacaBackend::domeCommand: the client origin, the same checks.
    Alpaca::DomeAnswer EmulatedRoof::domeCommand(Alpaca::DomeAction action)
    {
        const RoofIo::DomeDispatch dispatch = RoofIo::domeDispatch(action);
        const Outcome outcome = command(dispatch.command, dispatch.origin);
        return RoofIo::domeAnswer(dispatch.command, outcome.accepted, outcome.reason, outcome.text, fault);
    }
} // namespace DemoRoof
