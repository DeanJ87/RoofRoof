#include "Logger.h"
#include "RoofControl.h"
#include "RoofLocked.h"

#include <Arduino.h>

// Spec 027 (constitution 7.0.0): an Open or Close refused only for the park
// waits for it; a person's overrides; the snapshot. The decisions are
// lib/RoofIoLogic's MoveGuard; this only feeds it under the roof's lock.

namespace SQM
{
    // Under the lock: an Open or Close from a person, a client or MQTT.
    RoofControl::Outcome RoofControl::admitLocked(
        Roof::Command command, Roof::Origin origin, uint8_t overrides, const RoofIo::Blockers &now)
    {
        const uint8_t allowed = RoofIo::allowedOverrides(command, origin, overrides);
        const RoofIo::CommandCheck check = checkNow(command, origin, allowed);
        const RoofIo::CommandCheck aside = checkNow(command, origin, allowed, true);
        const RoofIo::Admission admission = guard.admit({command, origin, allowed, controller.status()}, check, aside, millis());
        const Roof::State state = controller.status().state;
        switch (admission.action)
        {
        case RoofIo::Admit::Run:
            return commandLocked(command, origin, now, allowed);
        case RoofIo::Admit::Wait:
            refreshSnapshot();
            return Outcome{true, "", admission.text, state, true};
        default:
            refreshSnapshot();
            return Outcome{false, admission.reason, admission.text, state};
        }
    }

    // Under the lock: the waiting command's checks with the park set aside. A
    // waiting Open is also called off by what refuses an open (an upload, the
    // hotspot, a restart); a waiting Close waits for them, as a close does.
    RoofIo::CommandCheck RoofControl::checkWait() const
    {
        const RoofIo::ParkWait &wait = guard.wait();
        if (!wait.waiting())
            return RoofIo::CommandCheck{};
        const RoofIo::CommandCheck check = checkNow(wait.command(), wait.origin(), wait.overrides(), true);
        if (!check.accepted || wait.command() != Roof::Command::Open || !blockers)
            return check;
        const RoofIo::Blockers now = blockers();
        if (RoofIo::blockerDecision(Roof::Command::Open, now).action == RoofIo::BlockerAction::Refuse)
            return RoofIo::CommandCheck{false, RoofIo::blockerReason(now), RoofIo::blockerText(now)};
        return check;
    }

    // Under the lock, every control step: the wait and the overrides in effect.
    void RoofControl::stepGuard(const RoofIo::GateInputs &now)
    {
        const RoofIo::ParkWait &wait = guard.wait();
        // The roof reached that end some other way (the automatic close): nothing left to wait for.
        if (wait.waiting() && controller.atEnd(wait.command()))
            guard.stopped();
        const RoofIo::CommandCheck waitCheck = checkWait();
        const Roof::Command command = wait.command();
        const RoofIo::GuardStep step = guard.step(controller.status(), closePending, now, waitCheck, millis());
        if (step.requestPark)
            parkRequestWanted.store(true);
        if (step.refused)
            notice = std::string("Waiting ") + RoofIo::commandWord(command) + " called off: " + wait.endText();
        if (step.gaveUp)
        {
            waitAlertText = wait.endText();
            waitAlertWanted.store(true);
            notice = waitAlertText;
        }
        if (!step.run)
            return;
        const RoofIo::Blockers blocked = blockers ? blockers() : RoofIo::Blockers();
        const Outcome outcome = commandLocked(command, wait.origin(), blocked, wait.overrides());
        if (!outcome.accepted)
            guard.runRefused(outcome.text);
        notice = std::string("The mount is parked: ") + RoofIo::commandWord(command) +
                 (outcome.accepted ? " started" : " refused: " + outcome.text);
    }

    bool RoofControl::takeWaitAlert(std::string &text)
    {
        if (!waitAlertWanted.exchange(false) || lock == nullptr)
            return false;
        RoofLocked held(lock);
        text = waitAlertText;
        return true;
    }

    bool RoofControl::takeOverrideAlert(std::string &text)
    {
        if (!overrideAlertWanted.exchange(false) || lock == nullptr)
            return false;
        RoofLocked held(lock);
        text = overrideText;
        return true;
    }

    bool RoofControl::takeStaleAlert(std::string &text)
    {
        if (!staleAlertWanted.exchange(false) || lock == nullptr)
            return false;
        RoofLocked held(lock);
        text = staleText;
        return true;
    }

    void RoofControl::refreshSnapshot()
    {
        RoofIo::Snapshot s;
        s.status = controller.status();
        s.fault = fault;
        s.travelMm = settings.roof.travelMm;
        s.supplyVolts = supplyVolts;
        s.minSupplyVolts = settings.roof.minSupplyVolts;
        s.currentSense = settings.roof.currentSenseEnabled;
        s.motorAmps = motorAmps;
        s.dutyPercent = static_cast<uint8_t>((static_cast<uint32_t>(controller.output().duty) * 100u + 127u) / 255u); // spec 046
        s.origin = moveOrigin;
        s.maxCurrentAmps = settings.roof.maxCurrentAmps;
        s.fittedSwitches = settings.fittedSwitches;
        if (!pinsValid)
            s.pinSetupText = pinsReason; // spec 023
        s.switch1 = gates.switch1;
        s.switch2 = gates.switch2;
        s.switchesOnly = settings.switchesOnly;
        s.openings[0] = watch.quiet[0];
        s.openings[1] = watch.quiet[1];
        s.stuckSwitch = gates.stuckSwitch;
        s.closePending = closePending;
        s.calibrating = calibration.active();
        s.personStops = personStops;
        s.remoteStops = remoteStops;
        closeWaitingNow.store(closePending);
        s.maxWatchdogBeatAgeMs = maxBeatAgeMs;
        s.watchdogTrips = watchdogTrips;
        s.maxStepGapMs = maxStepGapMs;
        s.simulation = Bench::SIMULATION;
        // Spec 027: the wait for the park and the overrides.
        s.parkWait = RoofIo::waitView(guard.wait(), millis());
        s.overrideBits = guard.inEffect(); // spec 039: an open's park override too
        s.openOverridable = RoofIo::overridableNow(Roof::Command::Open, currentGates());
        const bool changed = s.status.state != snap.status.state || s.status.fault.kind != snap.status.fault.kind ||
                             s.closePending != snap.closePending || s.calibrating != snap.calibrating ||
                             s.parkWait.waiting != snap.parkWait.waiting || s.overrideBits != snap.overrideBits;
        snap = s;
        stateNow.store(static_cast<uint8_t>(s.status.state));
        if (changed)
            revisionCount.fetch_add(1);
    }
} // namespace SQM
