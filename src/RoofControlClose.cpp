#include "CloseSequence.h"
#include "Logger.h"
#include "RoofControl.h"
#include "RoofLocked.h"
#include "RoofText.h"

// The automatic close (spec 005) and, spec 030, its blind close and its
// N.I.N.A.-lost close; a close waiting for an upload or MQTT connect.

namespace SQM
{
    RoofControl::Outcome RoofControl::withdrawnLocked() const
    {
        return Outcome{
            false, CloseSeq::PERSON_STOP_REASON, "A person stopped the roof: the automatic close is withdrawn", controller.status().state};
    }

    RoofControl::Outcome RoofControl::automaticClose(uint32_t personStopsSeen, uint8_t overrides)
    {
        if (lock == nullptr || !started.load())
            return notStarted();
        Outcome outcome;
        {
            RoofLocked held(lock);
            const RoofIo::Blockers now = blockers ? blockers() : RoofIo::Blockers();
            if (personStops != personStopsSeen)
                outcome = withdrawnLocked();
            else
            {
                guard.closeWins(); // spec 027: a waiting Open is called off
                // Spec 016: the automatic close always wins; a closing leg carries on as its close.
                if (calibration.active())
                    endCalibrationFor(true, "");
                // Spec 019: a person's jog is stopped first; the close then has its normal checks.
                if (Calib::Teach::yieldToClose(controller))
                {
                    applyOutput();
                    refreshSnapshot();
                    notice = "Jog stopped: the automatic close takes over";
                }
                outcome = commandLocked(Roof::Command::Close, Roof::Origin::Automatic, now, overrides);
            }
        }
        if (!outcome.accepted)
            Logger::warn(TAG, "Command refused: %s", outcome.text.c_str());
        else if (outcome.pending)
            Logger::info(TAG, "%s", outcome.text.c_str());
        return outcome;
    }

    RoofControl::Outcome RoofControl::automaticBlindClose(uint32_t personStopsSeen, uint8_t overrides)
    {
        if (lock == nullptr || !started.load())
            return notStarted();
        Outcome outcome;
        {
            RoofLocked held(lock);
            outcome = personStops != personStopsSeen ? withdrawnLocked() : blindLocked(overrides);
        }
        if (!outcome.accepted)
            Logger::warn(TAG, "Blind close refused: %s", outcome.text.c_str());
        else
            Logger::warn(TAG, "Blind close started: the Hall sensor is dead, the roof closes on time alone");
        return outcome;
    }

    // Under the lock (spec 030): the park gate as for every close, then the
    // controller's blind close. Anything else going on (an upload, a restart)
    // is stopped or waited for as a close does; the sequence tries again.
    RoofControl::Outcome RoofControl::blindLocked(uint8_t overrides)
    {
        const Roof::State state = controller.status().state;
        const uint8_t allowed = RoofIo::allowedOverrides(Roof::Command::Close, Roof::Origin::Automatic, overrides);
        const RoofIo::GateInputs now = currentGates();
        const RoofIo::GateRefusal gate = RoofIo::gateCommand(Roof::Command::Close, now, allowed);
        if (gate != RoofIo::GateRefusal::None)
            return Outcome{false, RoofIo::gateRefusalName(gate), RoofIo::describeGate(gate, now), state};
        const RoofIo::Blockers blocked = blockers ? blockers() : RoofIo::Blockers();
        const RoofIo::BlockerDecision decision = RoofIo::blockerDecision(Roof::Command::Close, blocked);
        if ((decision.cancelRestart || decision.stopFlash) && closeHook)
            closeHook(decision); // a close always wins
        if (decision.action != RoofIo::BlockerAction::Proceed)
            return blockedOutcome(blocked, state);
        guard.closeWins();
        const Outcome outcome = outcomeOf(controller.blindClose(Roof::Origin::Automatic));
        if (outcome.accepted)
        {
            moveOrigin = Roof::Origin::Automatic;
            guard.ran(Roof::Command::Close, allowed, now); // the park re-check, N.I.N.A.-lost included
        }
        applyOutput();
        syncFault(RoofIo::FaultCause::Logic);
        refreshSnapshot();
        return outcome;
    }

    void RoofControl::runPendingClose()
    {
        if (!closePending)
            return;
        const Outcome pre = precheck(Roof::Command::Close, closeOrigin, closeOverrides);
        if (!pre.accepted)
        {
            closePending = false;
            notice = "Waiting close dropped: " + pre.text;
            return;
        }
        const RoofIo::Blockers now = blockers ? blockers() : RoofIo::Blockers();
        const RoofIo::BlockerDecision decision = RoofIo::blockerDecision(Roof::Command::Close, now);
        if ((decision.cancelRestart || decision.stopFlash) && closeHook)
            closeHook(decision);
        if (decision.action != RoofIo::BlockerAction::Proceed)
            return;
        closePending = false;
        const Outcome outcome = runLocked(Roof::Command::Close, closeOrigin);
        if (outcome.accepted)
            guard.ran(Roof::Command::Close, closeOverrides, currentGates());
        notice = outcome.accepted ? "Waiting close started" : "Waiting close refused: " + outcome.text;
    }
} // namespace SQM
