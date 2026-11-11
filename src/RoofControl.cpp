#include "RoofControl.h"

#include <Arduino.h>
#include <cstring>

#include "DriverLogic.h"
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
            return a.fault.kind == b.fault.kind && a.cause == b.cause &&
                   std::memcmp(&a.fault.measured, &b.fault.measured, sizeof(float)) == 0 &&
                   std::memcmp(&a.fault.limit, &b.fault.limit, sizeof(float)) == 0;
        }
    } // namespace

    RoofControl::HardwareSettings RoofControl::settingsOf(const Config &config)
    {
        HardwareSettings s;
        s.roof = config.roof;
        s.fittedSwitches = static_cast<uint8_t>(config.park.switches);
        s.rainFitted = config.safety.rainContact;
        return s;
    }

    bool RoofControl::begin(const Config &config)
    {
        lock = xSemaphoreCreateMutex();
        if (lock == nullptr)
        {
            Logger::error(TAG, "No memory for the roof's lock; the motor stays off");
            return false;
        }
        settings = settingsOf(config);
        controller.applySettings(Roof::settingsFromConfig(settings.roof));
        gates.fittedSwitches = settings.fittedSwitches;
        if (!MotorDriver::begin())
            Logger::error(TAG, "PWM not available; the motor stays off");
        setupPins();
        // Constitution I: a restart never clears the lockout. Latched before the first step.
        restoreFault();
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

    RoofControl::Outcome RoofControl::runLocked(Roof::Command command, Roof::Origin origin)
    {
        const Outcome pre = precheck(command, origin);
        if (!pre.accepted)
            return pre;
        Outcome outcome = outcomeOf(controller.command(command, origin));
        applyOutput();
        syncFault(RoofIo::FaultCause::Logic);
        refreshSnapshot();
        return outcome;
    }

    // Why an open or close can't run now: the park and weather gates, then the
    // motion logic's own checks (without changing anything). Accepted when it can.
    RoofControl::Outcome RoofControl::precheck(Roof::Command command, Roof::Origin origin) const
    {
        const RoofIo::GateInputs now = currentGates();
        const RoofIo::GateRefusal gate = RoofIo::gateCommand(command, now);
        if (gate != RoofIo::GateRefusal::None)
            return Outcome{false, RoofIo::gateRefusalName(gate), RoofIo::describeGate(gate, now), controller.status().state};
        const Roof::CommandResult result = controller.check(command, origin);
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

    // Under the lock. A close's side effects (cancelling a restart, stopping an
    // upload) and its deferral happen only once it would run: past the gates
    // and the motion logic's checks, and not when the roof is already closed
    // or closing (spec 003 FR-029).
    RoofControl::Outcome RoofControl::commandLocked(Roof::Command command, Roof::Origin origin, const RoofIo::Blockers &now)
    {
        if (command == Roof::Command::Stop || command == Roof::Command::Abort)
        {
            closePending = false; // a stop replaces a waiting close
            return runLocked(command, origin);
        }
        const Outcome pre = precheck(command, origin);
        if (!pre.accepted)
            return pre;
        const Roof::State state = controller.status().state;
        if (command == Roof::Command::Close && (state == Roof::State::Closed || state == Roof::State::Closing))
            return runLocked(command, origin); // nothing to win
        const RoofIo::BlockerDecision decision = RoofIo::blockerDecision(command, now);
        if (decision.action == RoofIo::BlockerAction::Refuse)
            return Outcome{false, "busy", std::string(RoofIo::blockerText(now)) + ": try again in a moment", state};
        if ((decision.cancelRestart || decision.stopFlash) && closeHook)
            closeHook(decision); // a close always wins
        if (decision.action == RoofIo::BlockerAction::DeferClose)
        {
            closePending = true;
            closeOrigin = origin;
            refreshSnapshot();
            return Outcome{true, "", std::string("Close starts once this has stopped: ") + RoofIo::blockerText(now), state, true};
        }
        const Outcome outcome = runLocked(command, origin);
        if (command == Roof::Command::Open && outcome.accepted)
        {
            closePending = false; // an open that runs replaces a waiting close
            refreshSnapshot();
        }
        return outcome;
    }

    RoofControl::Outcome RoofControl::command(Roof::Command command, Roof::Origin origin)
    {
        if (lock == nullptr || !started.load())
            return Outcome{false, "notStarted", "The roof controller didn't start: the motor stays off", Roof::State::Unknown};
        const RoofIo::Blockers now = blockers ? blockers() : RoofIo::Blockers();
        Outcome outcome;
        {
            RoofLocked held(lock);
            outcome = commandLocked(command, origin, now);
        }
        if (!outcome.accepted)
            Logger::warn(TAG, "Command refused: %s", outcome.text.c_str());
        else if (outcome.pending)
            Logger::info(TAG, "%s", outcome.text.c_str());
        return outcome;
    }

    void RoofControl::runPendingClose()
    {
        if (!closePending)
            return;
        const Outcome pre = precheck(Roof::Command::Close, closeOrigin);
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
        notice = outcome.accepted ? "Waiting close started" : "Waiting close refused: " + outcome.text;
    }

    RoofControl::Outcome RoofControl::clearFault(Roof::Origin origin)
    {
        if (lock == nullptr || !started.load())
            return Outcome{false, "notStarted", "The roof controller didn't start: the motor stays off", Roof::State::Unknown};
        bool hadFault = false;
        Outcome outcome;
        {
            RoofLocked held(lock);
            hadFault = controller.status().fault.kind != Roof::FaultKind::None;
            outcome = outcomeOf(controller.clearFault(origin));
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
            // A fault ends a waiting close: clearing the fault later must not
            // run it without a fresh command.
            closePending = false;
        }
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
        s.maxCurrentAmps = settings.roof.maxCurrentAmps;
        s.fittedSwitches = settings.fittedSwitches;
        s.switch1 = gates.switch1;
        s.switch2 = gates.switch2;
        s.rainFitted = settings.rainFitted;
        s.rain = static_cast<RoofIo::Contact>(rainReading.load());
        s.closePending = closePending;
        closeWaitingNow.store(closePending);
        s.maxWatchdogBeatAgeMs = maxBeatAgeMs;
        s.watchdogTrips = watchdogTrips;
        s.maxStepGapMs = maxStepGapMs;
        const bool changed =
            s.status.state != snap.status.state || s.status.fault.kind != snap.status.fault.kind || s.closePending != snap.closePending;
        snap = s;
        stateNow.store(static_cast<uint8_t>(s.status.state));
        if (changed)
            revisionCount.fetch_add(1);
    }

    bool RoofControl::settingsSaveRefused(const Config &current, const Config &next) const
    {
        if (lock == nullptr)
            return false;
        const bool changed = RoofIo::motionSettingsChanged(current, next);
        RoofLocked held(lock);
        return RoofIo::settingsSaveRefused(controller.status().state, changed);
    }

    void RoofControl::updateConfig(const Config &config)
    {
        if (lock == nullptr)
            return;
        RoofLocked held(lock);
        pendingSettings = settingsOf(config);
        settingsPending = true;
    }

    void RoofControl::takePendingSettings()
    {
        if (!settingsPending)
            return;
        const Roof::CommandResult taken = controller.applySettings(Roof::settingsFromConfig(pendingSettings.roof));
        if (!taken.accepted)
            return; // a move is active: kept pending until it ends
        settings = pendingSettings;
        settingsPending = false;
        gates.fittedSwitches = settings.fittedSwitches;
        applyOutput();
        notice = "Roof settings applied";
    }

    void RoofControl::setSafety(const Safety::WeatherVerdict &weather, const Safety::ParkResult &park)
    {
        if (lock == nullptr)
            return;
        const std::string weatherText = Safety::describe(weather);
        const std::string parkText = Safety::describe(park);
        RoofLocked held(lock);
        gates.weather = weather.verdict;
        gates.weatherText = weatherText;
        gates.parked = park.parked;
        gates.parkText = parkText;
        safetyHandedOver = true;
        safetyAtMs = millis();
    }

    bool RoofControl::moving() const
    {
        return RoofIo::roofMoving(static_cast<Roof::State>(stateNow.load()));
    }

    RoofIo::GateInputs RoofControl::currentGates() const
    {
        return RoofIo::currentGates(gates, safetyHandedOver, millis() - safetyAtMs);
    }

    Safety::SwitchReading RoofControl::parkSwitch(uint8_t index) const
    {
        const uint8_t raw = index == 0 ? park1Reading.load() : park2Reading.load();
        return RoofIo::parkReading(static_cast<RoofIo::Contact>(raw));
    }

    Safety::RainContact RoofControl::rainContact() const
    {
        return RoofIo::rainReading(static_cast<RoofIo::Contact>(rainReading.load()));
    }

    RoofIo::Snapshot RoofControl::snapshot() const
    {
        if (lock == nullptr)
            return RoofIo::Snapshot();
        RoofLocked held(lock);
        return snap;
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
            if (sameFault(fault, savedFault) || controller.status().driving)
                return;
            wanted = fault;
        }
        if (!RoofFaultStore::save(wanted))
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
