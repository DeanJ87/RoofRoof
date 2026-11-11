#include "RoofControl.h"

#include <Arduino.h>
#include <cstring>

#include "DriverLogic.h"
#include "Logger.h"
#include "MotorDriver.h"
#include "RoofFaultStore.h"
#include "RoofLocked.h"
#include "RoofSettings.h"
#include "RoofText.h"
#include "SettingsGuard.h"

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
        const RoofIo::Decoded found = RoofFaultStore::load(stored);
        if (found == RoofIo::Decoded::None)
            return;
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

    RoofControl::Outcome RoofControl::command(Roof::Command command, Roof::Origin origin)
    {
        if (lock == nullptr)
            return Outcome{false, "notStarted", "The roof controller didn't start", Roof::State::Unknown};
        Outcome outcome;
        {
            RoofLocked held(lock);
            const RoofIo::GateInputs now = currentGates();
            const RoofIo::GateRefusal gate = RoofIo::gateCommand(command, now);
            if (gate != RoofIo::GateRefusal::None)
            {
                outcome = Outcome{false, RoofIo::gateRefusalName(gate), RoofIo::describeGate(gate, now), controller.status().state};
            }
            else
            {
                outcome = outcomeOf(controller.command(command, origin));
                applyOutput();
                syncFault(RoofIo::FaultCause::Logic);
                refreshSnapshot();
            }
        }
        if (!outcome.accepted)
            Logger::warn(TAG, "Command refused: %s", outcome.text.c_str());
        return outcome;
    }

    RoofControl::Outcome RoofControl::clearFault(Roof::Origin origin)
    {
        if (lock == nullptr)
            return Outcome{false, "notStarted", "The roof controller didn't start", Roof::State::Unknown};
        RoofLocked held(lock);
        const bool hadFault = controller.status().fault.kind != Roof::FaultKind::None;
        Outcome outcome = outcomeOf(controller.clearFault(origin));
        applyOutput();
        syncFault(RoofIo::FaultCause::Logic);
        refreshSnapshot();
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
        MotorDriver::apply(Driver::outputsFor(out.command, out.duty));
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
        s.watchdogTrips = watchdogTrips;
        s.maxStepGapMs = maxStepGapMs;
        const bool changed = s.status.state != snap.status.state || s.status.fault.kind != snap.status.fault.kind;
        snap = s;
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
    }
} // namespace SQM
