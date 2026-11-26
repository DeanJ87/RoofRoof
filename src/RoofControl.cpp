#include "RoofControl.h"

#include <Arduino.h>
#include <cstring>

#include "CloseSequence.h"
#include "CommandCheck.h"
#include "DomeBridge.h"
#include "DriverLogic.h"
#include "FlashGuard.h"
#include "Logger.h"
#include "MotionGuards.h"
#include "MotorDriver.h"
#include "RoofFaultStore.h"
#include "RoofLocked.h"
#include "RoofReadings.h"
#include "RoofSettings.h"
#include "RoofText.h"
#include "SettingsGuard.h"
#include "StepWatchdog.h"

// Commands, settings and the hand-over between tasks. The control task and
// the watchdog are in RoofControlTask.cpp.

namespace SQM
{
    namespace
    {
        bool sameFault(const RoofIo::FaultRecord &a, const RoofIo::FaultRecord &b)
        {
            // Compared bit for bit, so a NaN value equals itself.
            return a.fault.kind == b.fault.kind && a.cause == b.cause && a.atRest == b.atRest && a.blindDone == b.blindDone &&
                   std::memcmp(&a.fault.measured, &b.fault.measured, sizeof(float)) == 0 &&
                   std::memcmp(&a.fault.limit, &b.fault.limit, sizeof(float)) == 0;
        }
    } // namespace

    bool RoofControl::begin(const Config &config, const Board::PinMap &pinsInEffect, bool valid, const std::string &reason)
    {
        lock = xSemaphoreCreateMutex();
        if (lock == nullptr)
        {
            Logger::error(TAG, "No memory for the roof's lock; the motor stays off");
            return false;
        }
        takePins(pinsInEffect, valid, reason);
        settings = settingsOf(config);
        guard.configure(RoofIo::moveGuardSettingsOf(config));
        remoteStopsClose.store(config.autoClose.remoteStop); // spec 014
        controller.applySettings(motionSettings(settings));
        gates.fittedSwitches = settings.fittedSwitches;
        if (!pinsValid)
        {
            // Spec 023 FR-011: nothing attached, read or driven; never stored (no task saves it).
            fault = RoofIo::externalRecord(RoofIo::FaultCause::PinSetup, 0.0f, 0.0f);
            controller.raiseFault(fault.fault);
            refreshSnapshot();
            Logger::error(TAG, "Pin setup invalid: the roof's control task doesn't start; nothing moves");
            return false;
        }
        if (!MotorDriver::begin())
            Logger::error(TAG, "PWM not available; the motor stays off");
        setupPins();
        // Constitution I: a restart never clears the lockout. Latched before the first step.
        restoreFault();
        watch = savedWatch = RoofFaultStore::loadWatch(); // spec 024
#if ROOFROOF_BENCH
        benchStart();
#endif
        refreshSnapshot();
        return startTask();
    }

    void RoofControl::restoreFault()
    {
        RoofIo::FaultRecord stored;
        RoofIo::Decoded found = RoofFaultStore::load(stored);
        if (found != RoofIo::Decoded::None)
        {
            MotorDriver::clearTripMark(); // the lockout is already in NVS
        }
        else if (MotorDriver::tripMarked())
        {
            // The watchdog tripped and the device restarted before its fault
            // was saved (a hung control task): latch it now, save it on the first step.
            stored = RoofIo::externalRecord(RoofIo::FaultCause::Watchdog, 0.0f, static_cast<float>(RoofIo::WATCHDOG_LIMIT_MS));
            found = RoofIo::Decoded::Unreadable; // latched, not yet in NVS
        }
        else
        {
            return;
        }
        controller.raiseFault(stored.fault);
        if (stored.atRest)
            controller.restoreRestRail(); // spec 027
        if (stored.blindDone)
            controller.restoreBlindDone(); // spec 030: one blind close per fault
        fault = stored;
        // A damaged record is replaced by a readable one on the first step.
        if (found == RoofIo::Decoded::Fault)
            savedFault = stored;
        Logger::error(TAG, "Fault restored from before the restart: %s", RoofIo::describeFault(stored).c_str());
    }

    RoofControl::Outcome RoofControl::outcomeOf(const Roof::CommandResult &result)
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

    // Fix 041: with the command's overrides; without them a person's confirmed
    // close passed admission, then was refused here for the park it overrode.
    RoofControl::Outcome RoofControl::runLocked(Roof::Command command, Roof::Origin origin, uint8_t overrides)
    {
        const Outcome pre = precheck(command, origin, overrides);
        if (!pre.accepted)
            return pre;
        return runChecked(command, origin);
    }

    // Under the lock, already checked: the command, the outputs, the fault record.
    RoofControl::Outcome RoofControl::runChecked(Roof::Command command, Roof::Origin origin)
    {
        Outcome outcome = outcomeOf(controller.command(command, origin));
        if (outcome.accepted && (command == Roof::Command::Open || command == Roof::Command::Close))
            moveOrigin = origin; // spec 017: the move's origin in its summary
        applyOutput();
        syncFault(RoofIo::FaultCause::Logic);
        refreshSnapshot();
        return outcome;
    }

    // Why an open or close can't run now (RoofIo::checkCommand: the park and
    // weather gates, the client rule, the motion logic's own checks, without
    // changing anything). Accepted when it can.
    RoofControl::Outcome RoofControl::precheck(Roof::Command command, Roof::Origin origin, uint8_t overrides) const
    {
        const RoofIo::CommandCheck check = checkNow(command, origin, overrides);
        return Outcome{check.accepted, check.reason, check.text, controller.status().state};
    }

    RoofIo::CommandCheck RoofControl::checkNow(Roof::Command command, Roof::Origin origin, uint8_t overrides, bool parkAside) const
    {
        RoofIo::CommandContext context;
        context.overrides = overrides;
        context.parkAside = parkAside;
        context.closePending = closePending;
        context.remoteStopsClose = remoteStopsClose.load();
        context.calibrating = calibration.active(); // spec 016: open and close wait for the run
        return RoofIo::checkCommand(command, origin, currentGates(), controller, context);
    }

    // Under the lock: a stop or abort. Refused only for an Alpaca client
    // while a close runs or waits in weather that isn't safe (RoofIo::checkCommand).
    RoofControl::Outcome RoofControl::stopLocked(Roof::Command command, Roof::Origin origin)
    {
        // An Alpaca client may not stop a close while the weather isn't safe (spec 006).
        const RoofIo::CommandCheck check = checkNow(command, origin);
        if (!check.accepted)
            return Outcome{false, check.reason, check.text, controller.status().state};
        closePending = false; // a stop replaces a waiting close
        guard.stopped();      // spec 027: and ends a wait for the park
        // Counted before the stop is applied, so no snapshot shows the
        // stop without it: the close sequence (spec 005) stands down on it.
        // An Alpaca client's stop stops the roof but doesn't count (spec 006),
        // unless the owner's autoClose.remoteStop made it a person's (spec 014).
        if (RoofIo::countsAsPersonStop(command, origin) || check.standsDown)
            personStops++;
        if (check.standsDown)
            remoteStops++; // spec 014: the stand-down's alert names a remote stop
        // Spec 016: any accepted stop ends a calibration run.
        if (calibration.active())
            endCalibrationFor(false, origin == Roof::Origin::Person ? "Stopped by a person" : "Stopped by a stop command");
        // Applied on this one check: a settings save between two checks can't
        // count a stop that then isn't run.
        return runChecked(command, origin);
    }

    // A command refused by what else is going on: its reason and text.
    RoofControl::Outcome RoofControl::blockedOutcome(const RoofIo::Blockers &now, Roof::State state)
    {
        const std::string reason = RoofIo::blockerReason(now);
        const std::string text = RoofIo::blockerText(now);
        return Outcome{false, reason, reason == RoofIo::HOTSPOT_REASON ? text : text + ": try again in a moment", state};
    }

    // Under the lock. A close's side effects (cancelling a restart, stopping an
    // upload) and its deferral happen only once it would run: past the gates
    // and the motion logic's checks, and not when the roof is already closed
    // or closing (spec 003 FR-029).
    RoofControl::Outcome RoofControl::commandLocked(
        Roof::Command command, Roof::Origin origin, const RoofIo::Blockers &now, uint8_t overrides)
    {
        if (command == Roof::Command::Stop || command == Roof::Command::Abort)
            return stopLocked(command, origin);
        const Outcome pre = precheck(command, origin, overrides);
        if (!pre.accepted)
            return pre;
        const Roof::State state = controller.status().state;
        if (command == Roof::Command::Close && (state == Roof::State::Closed || state == Roof::State::Closing))
            return runLocked(command, origin, overrides); // nothing to win
        const RoofIo::BlockerDecision decision = RoofIo::blockerDecision(command, now);
        if (decision.action == RoofIo::BlockerAction::Refuse)
            return blockedOutcome(now, state);
        if ((decision.cancelRestart || decision.stopFlash) && closeHook)
            closeHook(decision); // a close always wins
        if (decision.action == RoofIo::BlockerAction::DeferClose)
        {
            closePending = true;
            closeOrigin = origin;
            closeOverrides = overrides;
            guard.ran(command, origin, overrides, currentGates()); // spec 027: a confirmed close keeps its override while it waits
            refreshSnapshot();
            return Outcome{true, "", std::string("Close starts once this has stopped: ") + RoofIo::blockerText(now), state, true};
        }
        const Outcome outcome = runLocked(command, origin, overrides);
        if (outcome.accepted)
            guard.ran(command, origin, overrides, currentGates()); // spec 027; spec 039: filtered by origin
        if (command == Roof::Command::Open && outcome.accepted)
        {
            closePending = false; // an open that runs replaces a waiting close
            refreshSnapshot();
        }
        return outcome;
    }

    RoofControl::Outcome RoofControl::command(Roof::Command command, Roof::Origin origin, uint8_t overrides)
    {
        if (lock == nullptr || !started.load())
            return notStarted();
        Outcome outcome;
        std::string overrode; // spec 039: a person's park override, logged after the lock
        {
            RoofLocked held(lock);
            // Probed under the lock (spec 010 FR-001): a platform action
            // registers itself under it too, so one of the two sees the other.
            // Stop and abort never look at blockers: not probed for them.
            const bool stopping = command == Roof::Command::Stop || command == Roof::Command::Abort;
            const RoofIo::Blockers now = blockers && !stopping ? blockers() : RoofIo::Blockers();
            const bool move = command == Roof::Command::Open || command == Roof::Command::Close;
            const std::string parkText = parkOverrideNow(command, origin, overrides); // spec 039
            outcome = move && origin != Roof::Origin::Automatic ? admitLocked(command, origin, overrides, now)
                                                                : commandLocked(command, origin, now);
            if (!parkText.empty() && outcome.accepted)
                overrode = noteParkOverride(parkText);
        }
        if (!overrode.empty())
            Logger::warn(TAG, "%s", overrode.c_str()); // spec 039: outside the lock
        if (!outcome.accepted)
            Logger::warn(TAG, "Command refused: %s", outcome.text.c_str());
        else if (outcome.pending)
            Logger::info(TAG, "%s", outcome.text.c_str());
        return outcome;
    }

    // Under the lock, spec 039: a person's Open or Close sets the park aside
    // for a move that will happen (the park doesn't hold, the roof isn't
    // there yet): the text for the log and the alert, "" otherwise.
    std::string RoofControl::parkOverrideNow(Roof::Command command, Roof::Origin origin, uint8_t overrides) const
    {
        const bool move = command == Roof::Command::Open || command == Roof::Command::Close;
        if (!move || origin != Roof::Origin::Person || (overrides & RoofIo::OverridePark) == 0 || controller.atEnd(command))
            return std::string();
        const RoofIo::GateInputs now = currentGates();
        return RoofIo::parkHolds(now) ? std::string() : RoofIo::parkOverrideText(command, now);
    }

    // Under the lock, spec 039: the accepted override for the main loop's alert.
    std::string RoofControl::noteParkOverride(const std::string &text)
    {
        overrideText = text;
        overrideAlertWanted.store(true);
        return text;
    }

    RoofControl::Outcome RoofControl::clearFault(Roof::Origin origin)
    {
        if (lock == nullptr || !started.load())
            return notStarted();
        bool hadFault = false;
        Outcome outcome;
        {
            RoofLocked held(lock);
            hadFault = controller.status().fault.kind != Roof::FaultKind::None;
            outcome = outcomeOf(controller.clearFault(origin));
#if ROOFROOF_BENCH
            if (benchGuard.tripped() && outcome.accepted)
            {
                // Spec 011: on a powered board the fault comes straight back; say so.
                benchLatch();
                outcome = Outcome{false, "benchSupply", RoofIo::describeFault(fault), controller.status().state};
            }
#endif
            applyOutput();
            syncFault(RoofIo::FaultCause::Logic);
            refreshSnapshot();
        }
        if (outcome.accepted && hadFault)
            Logger::info(TAG, "Fault cleared by a person");
        return outcome;
    }

    void RoofControl::raiseFault(RoofIo::FaultCause cause, float measured, float limit)
    {
        if (lock == nullptr)
            return;
        RoofLocked held(lock);
        latchExternal(RoofIo::externalRecord(cause, measured, limit));
        refreshSnapshot();
    }

    void RoofControl::latchExternal(const RoofIo::FaultRecord &record)
    {
        const bool wasClear = controller.status().fault.kind == Roof::FaultKind::None;
        controller.raiseFault(record.fault);
        applyOutput();
        if (wasClear)
            fault = record; // the first fault latched is the one kept
        syncFault(RoofIo::FaultCause::Logic);
    }

    void RoofControl::applyOutput()
    {
        const Roof::Output out = controller.output();
        if (MotorDriver::apply(Driver::outputsFor(out.command, out.duty)) != MotorDriver::ApplyResult::HeldWatchdog)
            return;
        // The logic wants the driver on but the watchdog has stopped: latch it
        // as a fault (watchdogStopped) rather than hold EN low silently.
        const uint32_t age = RoofIo::stepGapMs(millis(), MotorDriver::watchdogBeatMs());
        const RoofIo::FaultRecord record = RoofIo::externalRecord(
            RoofIo::FaultCause::WatchdogStopped, static_cast<float>(age), static_cast<float>(RoofIo::WATCHDOG_BEAT_LIMIT_MS));
        const bool wasClear = controller.status().fault.kind == Roof::FaultKind::None;
        controller.raiseFault(record.fault);
        if (wasClear)
            fault = record;
        syncFault(RoofIo::FaultCause::Logic);
        const Roof::Output off = controller.output();
        MotorDriver::apply(Driver::outputsFor(off.command, off.duty));
    }

    void RoofControl::syncFault(RoofIo::FaultCause causeIfNew)
    {
        const Roof::Fault &now = controller.status().fault;
        if (now.kind == Roof::FaultKind::None)
        {
            fault = RoofIo::FaultRecord();
            return;
        }
        if (fault.fault.kind == Roof::FaultKind::None)
        {
            fault.fault = now;
            fault.cause = causeIfNew;
            fault.atRest = controller.faultClearsAtRest(); // spec 027: kept over a restart
            // A fault ends a waiting close: clearing the fault later must not
            // run it without a fresh command.
            closePending = false;
        }
        fault.blindDone = controller.blindDone(); // spec 030: saved with the fault
    }

    bool RoofControl::settingsSaveRefused(const Config &current, const Config &next) const
    {
        if (lock == nullptr)
            return false;
        const bool changed = RoofIo::motionSettingsChanged(current, next);
        RoofLocked held(lock);
        return RoofIo::settingsSaveRefused(controller.status().state, changed, calibration.active());
    }

    void RoofControl::updateConfig(const Config &config)
    {
        remoteStopsClose.store(config.autoClose.remoteStop);
        if (lock == nullptr)
            return;
        RoofLocked held(lock);
        pendingSettings = settingsOf(config);
        settingsPending = true;
        guard.configure(RoofIo::moveGuardSettingsOf(config)); // spec 027: applies to the next wait
    }

    void RoofControl::takePendingSettings()
    {
        if (!settingsPending)
            return;
        const Roof::CommandResult taken = controller.applySettings(motionSettings(pendingSettings));
        if (!taken.accepted)
            return; // a move is active: kept pending until it ends
        settings = pendingSettings;
        settingsPending = false;
        gates.fittedSwitches = settings.fittedSwitches;
#if ROOFROOF_BENCH
        benchSettings();
#endif
        applyOutput();
        notice = "Roof settings applied";
    }

    void RoofControl::setSafety(
        const Safety::WeatherVerdict &weather,
        const Safety::ParkResult &park,
        const Safety::CloseHold &hold,
        bool daylightCloses,
        const std::string &daylightText)
    {
        if (lock == nullptr)
            return;
        const std::string weatherText = Safety::describe(weather);
        const std::string parkText = Safety::describe(park);
        RoofLocked held(lock);
        gates.weather = weather.verdict;
        gates.weatherText = weatherText;
        gates.weatherReasons = weather.reasons; // spec 027: what a person may override
        gates.parked = park.parked;
        gates.parkText = parkText;
        gates.ninaGrace = hold.riding;
        gates.parkFreshUntilMs = hold.freshUntilMs;
        gates.daylightCloses = daylightCloses;
        gates.daylightText = daylightText;
        safetyHandedOver = true;
        safetyAtMs = millis();
    }

    RoofIo::HoldReason RoofControl::whileStill(const std::function<void()> &action, bool wait)
    {
        if (lock == nullptr)
        {
            // No lock: begin() failed before the task started, so nothing can move.
            if (action)
                action();
            return RoofIo::HoldReason::None;
        }
        if (xSemaphoreTake(lock, wait ? portMAX_DELAY : 0) != pdTRUE)
            return RoofIo::HoldReason::Moving;
        RoofIo::PlatformState state;
        state.roof = controller.status().state;
        state.closeWaiting = closePending;
        state.calibrating = calibration.active(); // spec 016: held for the whole run
        // Roof and close only: the close sequence's part is the caller's (PlatformGuard).
        const RoofIo::HoldReason reason = RoofIo::platformHold(RoofIo::PlatformAction::FlashWrite, state);
        if (reason == RoofIo::HoldReason::None && action)
            action();
        xSemaphoreGive(lock);
        return reason;
    }

    void RoofControl::setBlockers(std::function<RoofIo::Blockers()> probe, std::function<void(const RoofIo::BlockerDecision &)> onClose)
    {
        if (lock == nullptr)
        {
            blockers = std::move(probe);
            closeHook = std::move(onClose);
            return;
        }
        RoofLocked held(lock);
        blockers = std::move(probe);
        closeHook = std::move(onClose);
    }

    bool RoofControl::faultUnsavedHeld() const
    {
        return fault.fault.kind != Roof::FaultKind::None && !sameFault(fault, savedFault);
    }

    bool RoofControl::moving() const
    {
        return RoofIo::roofMoving(static_cast<Roof::State>(stateNow.load())) || calibratingNow.load();
    }

    Safety::SwitchReading RoofControl::parkSwitch(uint8_t index) const
    {
        const uint8_t raw = index == 0 ? park1Reading.load() : park2Reading.load();
        return RoofIo::parkReading(static_cast<RoofIo::Contact>(raw));
    }

    RoofIo::Snapshot RoofControl::snapshot() const
    {
        if (lock == nullptr)
            return RoofIo::Snapshot();
        RoofLocked held(lock);
        return snap;
    }

    RoofIo::Snapshot RoofControl::snapshotWithChecks() const
    {
        if (lock == nullptr)
            return RoofIo::Snapshot();
        RoofIo::Snapshot s;
        RoofIo::GateInputs now;
        {
            RoofLocked held(lock);
            s = snap;
            now = currentGates();
        }
        // The weather and park as a command's checks would see them now (spec
        // 008), worded outside the lock: the control task never waits for it.
        RoofIo::applyChecks(s, now);
        return s;
    }

    uint32_t RoofControl::stackFreeBytes() const
    {
        return task != nullptr ? uxTaskGetStackHighWaterMark(task) : 0;
    }

    void RoofControl::persistFault()
    {
        static constexpr uint32_t RETRY_AFTER_MS = 5000;
        const uint32_t now = millis();
        if (saveFailedAtMs != 0 && now - saveFailedAtMs < RETRY_AFTER_MS)
            return;
        RoofIo::FaultRecord wanted;
        {
            RoofLocked held(lock);
            // Never while driving: a flash write could hold up the next step.
            // A new fault has already switched the driver off, so it is saved
            // at once; only a cleared fault's removal can wait for the stop.
            // Counted as a flash write until it ends (spec 010): a command
            // decided meanwhile waits (close) or is refused (open).
            // Spec 030: a blind close's done flag is saved in its dead time,
            // before the motor runs (this task steps the roof: no step meanwhile).
            const Roof::Status &now = controller.status();
            const bool blindWaiting = now.blind && !now.driving;
            if (sameFault(fault, savedFault) || now.driving || (RoofIo::roofMoving(now.state) && !blindWaiting) || closePending)
                return;
            // Not under a committed restart: the reset would cut the write off.
            if (blockers && blockers().restartCommitted)
                return;
            wanted = fault;
            FlashGuard::holdShort();
        }
        const bool saved = RoofFaultStore::save(wanted);
        FlashGuard::releaseShort();
        if (!saved)
        {
            saveFailedAtMs = now == 0 ? 1 : now;
            Logger::error(TAG, "Couldn't save the roof's fault in NVS; retrying in 5 s");
            return;
        }
        saveFailedAtMs = 0;
        RoofLocked held(lock);
        savedFault = wanted;
        // A trip's fault is in NVS now (or a person cleared it): the restart
        // marker isn't needed. A trip still waiting for its step keeps it.
        if (!MotorDriver::tripped())
            MotorDriver::clearTripMark();
    }
} // namespace SQM
