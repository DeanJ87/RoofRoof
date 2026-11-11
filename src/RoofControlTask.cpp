#include <Arduino.h>
#include <esp_task_wdt.h>

#include "Logger.h"
#include "MotionGuards.h"
#include "MotorDriver.h"
#include "RoofControl.h"
#include "RoofLocked.h"
#include "RoofPins.h"
#include "RoofReadings.h"
#include "RoofText.h"
#include "StepWatchdog.h"

// The roof's control task (every RoofIo::CONTROL_PERIOD_MS) and its watchdog
// timer (spec 003 FR-005 to FR-009, research R1, R3).

namespace SQM
{
    namespace
    {
        uint16_t hallCounts()
        {
            uint16_t samples[RoofIo::HALL_SAMPLES];
            for (uint16_t &sample : samples)
                sample = static_cast<uint16_t>(analogRead(Pins::HALL_SENSOR));
            return RoofIo::medianCounts(samples, RoofIo::HALL_SAMPLES);
        }

        float pinMillivolts(uint8_t pin, uint8_t count = RoofIo::ANALOG_SAMPLES)
        {
            uint32_t samples[RoofIo::MAX_SAMPLES];
            for (uint8_t i = 0; i < count; ++i)
                samples[i] = analogReadMilliVolts(pin);
            return RoofIo::averageMillivolts(samples, count);
        }

        bool levelLow(uint8_t pin)
        {
            return digitalRead(pin) == LOW;
        }
    } // namespace

    void RoofControl::setupPins()
    {
        analogReadResolution(12);
        for (uint8_t pin : {Pins::HALL_SENSOR, Pins::SUPPLY_SENSE, Pins::CURRENT_R, Pins::CURRENT_L})
            analogSetPinAttenuation(pin, ADC_11db);
        // Contacts to ground; GPIO34-39 have no pull-ups, these pins do.
        for (uint8_t pin : {Pins::PARK_A, Pins::PARK_B, Pins::RAIN})
            pinMode(pin, INPUT_PULLUP);
    }

    bool RoofControl::startTask()
    {
        lastStepMs.store(millis());
        if (xTaskCreatePinnedToCore(taskEntry, "roof_motion", TASK_STACK_BYTES, this, TASK_PRIORITY, &task, TASK_CORE) != pdPASS)
        {
            task = nullptr;
            Logger::error(TAG, "Couldn't start the roof's control task; the motor stays off");
            return false;
        }
        const esp_timer_create_args_t timerArgs = {
            .callback = watchdogEntry,
            .arg = this,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "roof_watchdog",
            .skip_unhandled_events = true,
        };
        if (esp_timer_create(&timerArgs, &watchdogTimer) != ESP_OK ||
            esp_timer_start_periodic(watchdogTimer, RoofIo::WATCHDOG_CHECK_MS * 1000ULL) != ESP_OK)
        {
            // Without the watchdog nothing may move (constitution I).
            raiseFault(RoofIo::FaultCause::Watchdog, 0.0f, static_cast<float>(RoofIo::WATCHDOG_LIMIT_MS));
            Logger::error(TAG, "Couldn't start the roof's watchdog; motion locked out");
            return false;
        }
        started.store(true);
        MotorDriver::arm();
        Logger::info(
            TAG,
            "Roof control task started (%u ms steps, watchdog %u ms)",
            static_cast<unsigned>(RoofIo::CONTROL_PERIOD_MS),
            static_cast<unsigned>(RoofIo::WATCHDOG_LIMIT_MS));
        return true;
    }

    void RoofControl::watchdogEntry(void *self)
    {
        RoofControl &roof = *static_cast<RoofControl *>(self);
        MotorDriver::watchdogBeat();
        // The step time first, then the clock: a step stamped in between reads
        // as no gap, never as a huge one.
        const uint32_t last = roof.lastStepMs.load();
        const uint32_t now = millis();
        if (MotorDriver::enabled() && RoofIo::watchdogExpired(now, last))
            MotorDriver::trip();
    }

    void RoofControl::taskEntry(void *self)
    {
        static_cast<RoofControl *>(self)->run();
    }

    void RoofControl::run()
    {
        // A task that hangs outright restarts the device (the EN pull-down holds the driver off).
        esp_task_wdt_add(nullptr);
        TickType_t wake = xTaskGetTickCount();
        const TickType_t period = pdMS_TO_TICKS(RoofIo::CONTROL_PERIOD_MS);
        for (;;)
        {
            step();
            esp_task_wdt_reset();
            persistFault();
            logChanges();
            // A step that ran long starts the next one at once rather than
            // trying to catch up on missed ones.
            if (xTaskDelayUntil(&wake, period) == pdFALSE)
                wake = xTaskGetTickCount();
        }
    }

    RoofControl::StepInputs RoofControl::readInputs(uint32_t nowMs)
    {
        HardwareSettings now;
        {
            RoofLocked held(lock);
            now = settings;
        }
        StepInputs in;
        in.readings.nowMs = nowMs;
        in.readings.hallCounts = hallCounts();
        in.readings.supplyVolts = RoofIo::supplyVolts(pinMillivolts(Pins::SUPPLY_SENSE));
        in.readings.motorAmps = NAN; // "no reading" while current sense is off (spec 002 contract)
        if (now.roof.currentSenseEnabled)
            in.readings.motorAmps = RoofIo::motorAmps(
                pinMillivolts(Pins::CURRENT_R, RoofIo::CURRENT_SAMPLES),
                pinMillivolts(Pins::CURRENT_L, RoofIo::CURRENT_SAMPLES),
                now.roof.currentAmpsPerVolt);
        park1Reading.store(static_cast<uint8_t>(park1.update(levelLow(Pins::PARK_A), nowMs)));
        park2Reading.store(static_cast<uint8_t>(park2.update(levelLow(Pins::PARK_B), nowMs)));
        rainReading.store(static_cast<uint8_t>(rain.update(levelLow(Pins::RAIN), nowMs)));
        return in;
    }

    void RoofControl::step()
    {
        const uint32_t now = millis();
        const uint32_t previous = lastStepMs.exchange(now);
        StepInputs in = readInputs(now);
        in.gapMs = RoofIo::stepGapMs(now, previous);
        RoofLocked held(lock);
        stepLocked(in);
    }

    void RoofControl::stepLocked(const StepInputs &in)
    {
        if (in.gapMs > maxStepGapMs)
            maxStepGapMs = in.gapMs;
        supplyVolts = in.readings.supplyVolts;
        // Read under the output applied since the last step: scaled back by its duty.
        Roof::Readings readings = in.readings;
        readings.motorAmps =
            RoofIo::runningAmps(in.readings.motorAmps, controller.status(), controller.output(), settings.roof.minRunningAmps);
        motorAmps = readings.motorAmps;
        if (MotorDriver::tripped())
        {
            // The watchdog dropped EN: latch it (output off), then let EN be used again.
            ++watchdogTrips;
            latchExternal(RoofIo::externalRecord(
                RoofIo::FaultCause::Watchdog, static_cast<float>(in.gapMs), static_cast<float>(RoofIo::WATCHDOG_LIMIT_MS)));
            MotorDriver::clearTrip();
        }
        takePendingSettings();
        gates.switch1 = static_cast<RoofIo::Contact>(park1Reading.load());
        gates.switch2 = static_cast<RoofIo::Contact>(park2Reading.load());
        checkGates();
        runPendingClose();
        controller.tick(readings);
        applyOutput();
        syncFault(RoofIo::FaultCause::Logic);
        refreshSnapshot();
    }

    void RoofControl::checkGates()
    {
        const Roof::Status &status = controller.status();
        const RoofIo::GateInputs now = currentGates();
        // Constitution II: a close stops as a motion fault when the park is lost.
        if (RoofIo::interlockLost(status, now))
        {
            latchExternal(RoofIo::externalRecord(RoofIo::FaultCause::ParkInterlock, static_cast<float>(RoofIo::interlockBits(now)), 0.0f));
            return;
        }
        // Constitution III: no opening while the weather isn't safe.
        if (RoofIo::openMustStop(status, now))
        {
            controller.command(Roof::Command::Stop, Roof::Origin::Automatic);
            applyOutput();
            notice = "Opening stopped: " + RoofIo::describeGate(RoofIo::GateRefusal::WeatherNotSafe, now);
        }
    }

    void RoofControl::logChanges()
    {
        std::string text;
        {
            RoofLocked held(lock);
            text.swap(notice);
        }
        if (!text.empty())
            Logger::warn(TAG, "%s", text.c_str());
        RoofIo::Snapshot now = snapshot();
        if (now.status.fault.kind != loggedFault && now.status.fault.kind != Roof::FaultKind::None)
            Logger::error(TAG, "Fault: %s", RoofIo::describeFault(now.fault).c_str());
        if (now.status.state != loggedState)
            Logger::info(TAG, "State %s -> %s", Roof::stateName(loggedState), Roof::stateName(now.status.state));
        loggedFault = now.status.fault.kind;
        loggedState = now.status.state;
    }
} // namespace SQM
